/*
 * Copyright (c) 2010-2013 Stefan Bolte <portix@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef DISABLE_HSTS
#include <stdio.h>
#include <string.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include "dwb.h"
#include "util.h"
#include "hsts.h"
#include "gnutls/gnutls.h"
#include "gnutls/x509.h"

/*
 * This file contains an HSTS (HTTP Strict Transport Security) implementation
 * for the dwb browser. It works by registering a session interface with soup
 * and rewriting relevant requests when they are queued, and listening for
 * hsts headers. The approach was inspired by the HSTS implementation in thje
 * midori browser.
 *
 * Current Features:
 * + Enforces HSTS as specified in [RFC6797]
 * + Loading and saving of the cache
 * + Enforce strict ssl verification on known hsts hosts
 * + Bootstrap whitelist (automatically converted from the chromium project)
 * + Add support for certificate pinning a la Chromium
 *
 * TODO:
 * + Handle UTF-8 BOM in loading code
 * + Periodic saving of database to mitigate loss of information in event of crash
 *
 * Problems:
 * 1. The implementation doesn't consider mixed content, which should be
 *    blocked according to RFC 6797 12.4
 */

#define HSTS_HEADER_NAME "Strict-Transport-Security"

/* The HSTSEntry data structure represents a known host in the HSTS database
 *
 * Members:
 * expiry      - the expiry of the rule, represented as microseconds since January 1, 1970 UTC.
 * sub_domains - whether the rule applies to sub_domains
 */
typedef struct _HSTSEntry {
    gint64 expiry;
    gboolean sub_domains;
} HSTSEntry;

/* Allocate a new HSTSEntry and initialise it. It is initialised to have
 * maximum expiry (effectively indefinite life) and not to apply to sub
 * domains.
 */
static HSTSEntry *
hsts_entry_new()
{
    HSTSEntry *entry = dwb_malloc(sizeof(HSTSEntry));
    entry->expiry = G_MAXINT64;
    entry->sub_domains = false;
    return entry;
}

/* Allocates and initialises a new HSTSEntry to the given values.
 * Params:
 * max_age     - number of seconds the rule should live.
 * sub_domains - whether the rule applies to sub_domains
 */
static HSTSEntry *
hsts_entry_new_from_val(gint64 max_age, gboolean sub_domains)
{
    HSTSEntry *entry = hsts_entry_new();
    entry->expiry = g_get_real_time();
    if(max_age > (G_MAXINT64 - entry->expiry)/G_USEC_PER_SEC)
        entry->expiry = G_MAXINT64;
    else
        entry->expiry += max_age*G_USEC_PER_SEC;
    entry->sub_domains = sub_domains;
    return entry;
}

/* Frees the HSTSEntry
 */
static void
hsts_entry_free(HSTSEntry *entry)
{
    g_free(entry);
}

/* The HSTSPinEntry data structure represents a host with a static set of
 * allowed and forbidden SPKIs hashes.
 */
typedef struct _HSTSPinEntry {
    GHashTable *good_certs;
    GHashTable *bad_certs;
    gboolean sub_domains;
} HSTSPinEntry;

/* Allocates and initialises a new HSTSPinEntry
 */
static HSTSPinEntry *
hsts_pin_entry_new()
{
    HSTSPinEntry *entry = dwb_malloc(sizeof(HSTSPinEntry));
    entry->good_certs = NULL;
    entry->bad_certs = NULL;
    entry->sub_domains = false;
    return entry;
}

/* Frees the HSTSPinEntry, it is safe to pass NULL
 */
static void
hsts_pin_entry_free(HSTSPinEntry *entry)
{
    if(entry == NULL)
        return;

    if(entry->good_certs != NULL)
        g_hash_table_destroy(entry->good_certs);
    if(entry->bad_certs != NULL)
        g_hash_table_destroy(entry->bad_certs);
    g_free(entry);
}

/*
 * HSTSProvider works by registering as a SoupSessionFeature and rewriting all
 * http requests into https requests for known hosts. However this means that
 * HSTSProvider has to be implement the SoupSessionFeatureInterface and hence
 * all the boilerplate gobject code in the following.
 *
 */

/*
 * Type macros.
 */
#define HSTS_TYPE_PROVIDER                (hsts_provider_get_type ())
#define HSTS_PROVIDER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), HSTS_TYPE_PROVIDER, HSTSProvider))
#define HSTS_IS_PROVIDER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HSTS_TYPE_PROVIDER))
#define HSTS_PROVIDER_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), HSTS_TYPE_PROVIDER, HSTSProviderClass))
#define HSTS_IS_PROVIDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), HSTS_TYPE_PROVIDER))
#define HSTS_PROVIDER_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), HSTS_TYPE_PROVIDER, HSTSProviderClass))
#define HSTS_PROVIDER_GET_PRIVATE(o)      (G_TYPE_INSTANCE_GET_PRIVATE ((o), HSTS_TYPE_PROVIDER, HSTSProviderPrivate)) 

/* The HSTSProvider public interface
 */
typedef struct _HSTSProvider
{
    GObject parent_instance;
} HSTSProvider;

/* The private members of the HSTSProvider
 */
typedef struct _HSTSProviderPrivate
{
    GHashTable *domains, *pin_domains;
} HSTSProviderPrivate;

/* The class members of the HSTSProvider
 */
typedef struct _HSTSProviderClass
{
    GObjectClass parent_class;

    /* The following static variables are used to do case insensitive comparisons
     * of directive names, as specified in RFC 6797 6.1 2.
     */
    gchar *directive_max_age;
    gchar *directive_sub_domains;
} HSTSProviderClass;

/* Prototypes of various functions, some are needed for glib magic. This is not an exhaustive
 * list of the hsts_provider functions.
 */
static void     hsts_provider_init                (HSTSProvider      *self);
static void     hsts_provider_class_init          (HSTSProviderClass *klass);
static void     hsts_provider_base_class_init     (HSTSProviderClass *klass);
static void     hsts_provider_base_class_finalize (HSTSProviderClass *klass);
static gpointer hsts_provider_parent_class = NULL;
static void hsts_provider_session_feature_init(SoupSessionFeatureInterface *feature_interface, gpointer interface_data);
static void hsts_provider_finalize (GObject *object);

/* GLib essential function. This basically declares the existence of the
 * HSTSProvider class to GLib and gives it various information about it. This
 * rather cumbersome function is needed to get dynamic class members(ie.
 * setting the base_* options).
 */
GType
hsts_provider_get_type (void)
{
    static volatile gsize g_define_type_id__volatile = 0;
    if (g_once_init_enter (&g_define_type_id__volatile))
    {
        GTypeInfo info;
        info.class_size = sizeof(HSTSProviderClass);
        info.base_init  = (GBaseInitFunc) hsts_provider_base_class_init;
        info.base_finalize = (GBaseFinalizeFunc) hsts_provider_base_class_finalize;
        info.class_init = (GClassInitFunc) hsts_provider_class_init;
        info.class_finalize = NULL;
        info.class_data = NULL;
        info.instance_size = sizeof(HSTSProvider);
        info.n_preallocs = 0;
        info.instance_init = (GInstanceInitFunc) hsts_provider_init;
        info.value_table = NULL;

        GType g_define_type_id = g_type_register_static (G_TYPE_OBJECT, g_intern_static_string ("HSTSProvider"), &info, 0);

        const GInterfaceInfo g_implement_interface_info = {
            (GInterfaceInitFunc) hsts_provider_session_feature_init, NULL, NULL
        };
        g_type_add_interface_static (g_define_type_id, SOUP_TYPE_SESSION_FEATURE, &g_implement_interface_info);
        g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }
    return g_define_type_id__volatile;
}

/* Initialise the dynamic class members of HSTSProvider
 */
static void
hsts_provider_base_class_init (HSTSProviderClass *klass)
{
    klass->directive_max_age = g_utf8_casefold("max-age", -1);
    klass->directive_sub_domains = g_utf8_casefold("includeSubDomains", -1);
}

/* Finalise(free) the dynamic class members of HSTSProvider
 */
static void
hsts_provider_base_class_finalize (HSTSProviderClass *klass)
{
    g_free(klass->directive_max_age);
    g_free(klass->directive_sub_domains);
}

/* Initialise the HSTSProvider class
 */
static void
hsts_provider_class_init (HSTSProviderClass *klass)
{
    hsts_provider_parent_class = g_type_class_peek_parent (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (HSTSProviderPrivate));

    object_class->finalize = hsts_provider_finalize;
}

/* Initialise an HSTSProvider instance
 */
static void
hsts_provider_init (HSTSProvider *provider)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE (provider);

    priv->domains = g_hash_table_new_full((GHashFunc)g_str_hash, (GEqualFunc)g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)hsts_entry_free);
    priv->pin_domains = g_hash_table_new_full((GHashFunc)g_str_hash, (GEqualFunc)g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)hsts_pin_entry_free);
}

/* Finalise an HSTSProvider instance
 */
static void
hsts_provider_finalize (GObject *object)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE (object);

    g_hash_table_destroy(priv->domains);
    g_hash_table_destroy(priv->pin_domains);

    G_OBJECT_CLASS (hsts_provider_parent_class)->finalize (object);
}

/* Remove an entry from the known hosts, this doesn't remove superdomains of
 * host with the includeSubDomains directive. So the host might still be
 * affected by the HSTS code
 */
static void
hsts_provider_remove_entry(HSTSProvider *provider, const char *host)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);
    
    gchar *canonical = g_hostname_to_unicode(host);
    g_hash_table_remove(priv->domains, canonical);
    g_free(canonical);
}

/* Adds the host to the known host, if it already exists it replaces it with
 * the information contained in entry. As specified in 8.1 [RFC6797] it won't
 * add ip addresses as hosts.
 */
static void
hsts_provider_add_entry(HSTSProvider *provider, const char *host, HSTSEntry *entry)
{
    if(g_hostname_is_ip_address(host))
        return;

    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);

    g_hash_table_replace(priv->domains, g_hostname_to_unicode(host), entry);
}

/* Adds the host to hosts for which a certificate black or whitelist has been
 * specified.
 */
static void
hsts_provider_add_pin_entry(HSTSProvider *provider, const char *host, HSTSPinEntry *entry)
{
    if(g_hostname_is_ip_address(host))
        return;

    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);

    g_hash_table_replace(priv->pin_domains, g_hostname_to_unicode(host), entry);
}

/* Checks whether host is currently a known host or it is a sub domain of a
 * known host which covers sub domains.
 *
 * Beware: An ip address will return false, as specified in 8.3 [RFC6797]
 */
static gboolean
hsts_provider_should_secure_host(HSTSProvider *provider, const char *host)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);

    if(g_hostname_is_ip_address(host))
        return false;

    gchar *canonical = g_hostname_to_unicode(host);
    gboolean result = false;
    if(strlen(canonical) > 0) /* Don't match empty strings as per. 8.3 [RFC6797] */
    {
        gchar *cur = canonical;
        gboolean sub_domain = false; /* Indicates whether host is a proper sub domain of cur */
        gunichar dot = g_utf8_get_char(".");
        while(cur != NULL)
        {
            HSTSEntry *entry = g_hash_table_lookup(priv->domains, cur);
            if(entry != NULL)
            {
                if(g_get_real_time() > entry->expiry) /* Remove expired entries */
                    hsts_provider_remove_entry(provider, cur);
                else if(!sub_domain || entry->sub_domains) 
                {  /* If either host == cur or host is a proper sub domain of
                      cur and the cur entry covers sub domains. */
                    result = true;
                    break;
                }
            }

            sub_domain = true;
            cur = g_utf8_strchr(cur, -1, dot);
            /* Since canonical is in canonical form, it doesn't end with a .
             * and hence there's no problem with the following: */
            if(cur != NULL)
                cur = g_utf8_next_char(cur); 
        }
    }
    g_free(canonical);

    return result;
}

/* Checks whether there is relevant information for host in the certificate
 * white- and blacklist, if so it returns the relevant entry. Else it returns
 * NULL.
 */
static HSTSPinEntry *
hsts_provider_has_cert_pin(HSTSProvider *provider, const char *host)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);

    if(g_hostname_is_ip_address(host))
        return NULL;

    HSTSPinEntry *result = NULL;
    gchar *canonical = g_hostname_to_unicode(host);
    if(strlen(canonical) > 0) /* Don't match empty strings as per. 8.3 [RFC6797] */
    {
        gchar *cur = canonical;
        gboolean sub_domain = false; /* Indicates whether host is a proper sub domain of cur */
        gunichar dot = g_utf8_get_char(".");
        while(cur != NULL)
        {
            result = g_hash_table_lookup(priv->pin_domains, cur);
            if(result != NULL && (!sub_domain || result->sub_domains))
                /* If either host == cur or host is a proper sub domain of
                   cur and the cur entry covers sub domains. */
                break;
            result = NULL;

            sub_domain = true;
            cur = g_utf8_strchr(cur, -1, dot);
            /* Since canonical is in canonical form, it doesn't end with a .
             * and hence there's no problem with the following: */
            if(cur != NULL)
                cur = g_utf8_next_char(cur); 
        }
    }
    g_free(canonical);

    return result;
}

/* Parse an HSTS header and add it to the known hosts.
 * Returns whether or not the header was valid.
 */
static gboolean
hsts_provider_parse_header(HSTSProvider *provider, const char *host, const char *header)
{
    GHashTable *directives = soup_header_parse_semi_param_list(header);
    
    HSTSProviderClass *klass = g_type_class_ref(HSTS_TYPE_PROVIDER);
    gint64 max_age = -1;
    gboolean sub_domains = false;
    gboolean success = true;

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, directives);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        /* We have to jump through hoops here to be able to do the
         * comparison in a case-insensitive manner, as specified in
         * RFC 6797 6.1
         */
        gchar *key_ci = g_utf8_casefold(key, -1);
        if (g_utf8_collate(key_ci, klass->directive_max_age) == 0)
        {
            if(value == NULL)
            {
                success = false;
                break;
            }
            else
            {
                gchar *endptr;
                max_age = g_ascii_strtoll(value, &endptr, 10);
                if(endptr == value || max_age < 0)
                {
                    success = false;
                    break;
                }
            }
        }
        else if (g_utf8_collate(key_ci, klass->directive_sub_domains) == 0)
        {
            if(value != NULL)
            {
                success = false;
                break;
            }
            else
                sub_domains = true;
        }
        g_free(key_ci);
    }
    g_type_class_unref(klass);
    if(success)
    {
        if(max_age != 0)
            hsts_provider_add_entry(provider, host, hsts_entry_new_from_val(max_age, sub_domains));
        else /* max_age = 0 indicates remove header */
            hsts_provider_remove_entry(provider, host);
    }

    soup_header_free_param_list(directives);
    return success;
}

/* Processes the headers of msg and looks for a valid HSTS, if found it adds it
 * as a known host according to the information specified in the header.
 */
static void
hsts_process_hsts_header (SoupMessage *msg, gpointer user_data)
{
    GTlsCertificate *certificate;
    GTlsCertificateFlags errors;
    /* Only read HSTS headers sent over a properly validated https connection
     * as specified in 8.1 [RFC6797]
     */
    SoupURI *uri = soup_message_get_uri(msg);
    const char *host = soup_uri_get_host(uri);
    if(!g_hostname_is_ip_address(host) &&
            soup_message_get_https_status(msg, &certificate, &errors) &&
            errors == 0){
        HSTSProvider *provider = user_data;

        SoupMessageHeaders *hdrs;
        g_object_get(G_OBJECT(msg), SOUP_MESSAGE_RESPONSE_HEADERS, &hdrs, NULL);

        SoupMessageHeadersIter iter;
        soup_message_headers_iter_init(&iter, hdrs);
        const char *name, *value;
        while(soup_message_headers_iter_next(&iter, &name, &value))
        {
            if(strcmp(name, HSTS_HEADER_NAME) == 0)
            {
                /* It is not exactly clear to me what the correct behavior is
                 * if multiple headers are present.  There seems to be some
                 * relevant information in 8.1 [RFC6797].
                 */
                if(hsts_provider_parse_header(provider, host, value))
                    break;
            }
        }
        /* FIXME: Possible memory leak, Investigate whether hdrs should be
         * cleaned up?
         * g_object_unref(hdrs);  <-- This makes GLib complain so that clearly
         * isn't the right approach. */
    }
}

/* Contains case folded versions of true and false used for comparisons in
 * parse_line */
static char *parser_true, *parser_false;

/* Parses a line from a known hosts file and if it is correctly parsed it is
 * added to the known hosts in provider */
static void
parse_line(HSTSProvider *provider, const char *line, gint64 now)
{
    /* Ignore comments */
    if(g_utf8_get_char(line) == g_utf8_get_char("#"))
        return;

    char **split = g_strsplit(line, "\t", -1);
    if(g_strv_length(split) == 3)
    {
        char *host = split[0], *sub_domains = split[1], *expires = split[2];
        HSTSEntry *entry = hsts_entry_new();
        gboolean success = true;
        
        if(g_utf8_collate(parser_true, sub_domains) == 0)
            entry->sub_domains = true;
        else if(g_utf8_collate(parser_false, sub_domains) == 0)
            entry->sub_domains = false;
        else
            success = false;

        char *end;
        entry->expiry = g_ascii_strtoll(expires, &end, 10);
        if(expires == end || entry->expiry < now)
            success = false;

        if(success)
            hsts_provider_add_entry(provider, host, entry);
        else
            hsts_entry_free(entry);
    }

    g_strfreev(split);
}

/* Represents an entry in the preloaded HSTS database.
 *
 * Members:
 * host        - the host of the entry
 * good_certs  - a null terminated array of base64 encoded key ids of the good certificates, if NULL it is treated as the empty array
 * bad_certs   - a null terminated array of base64 encoded key ids of the bad certificates, if NULL it is treated as the empty array
 * hsts        - if true the host is added to the database of known HSTS hosts
 * sub_domains - indicates whether this entry applies to sub_domains
 *
 */
typedef struct _HSTSPreloadEntry {
    const char *host;
    const char * const *good_certs;
    const char * const *bad_certs;
    gboolean hsts;
    gboolean sub_domains;
} HSTSPreloadEntry;

#include "hsts_preload.h"

/* Allocates and fills a hash set of certificates
 */
static void
fill_cert_set(GHashTable **cert_set, const char * const *certs)
{
    if(certs == NULL)
        return;
    if(*cert_set == NULL)
        *cert_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *hash_set = *cert_set;
    while(*certs != NULL)
    {
        g_hash_table_add(hash_set, g_strdup(*certs));
        certs++;
    }
}

/* Loads the default database built into dwb
 */
static void
load_default_database(HSTSProvider *provider)
{
    const HSTSPreloadEntry *entry = s_hsts_preload;
    size_t i;
    for(i=0; i < s_hsts_preload_length; i++)
    {
        if(entry->hsts)
        {
            HSTSEntry *hsts_entry = hsts_entry_new();
            hsts_entry->sub_domains = entry->sub_domains;
            hsts_provider_add_entry(provider, entry->host, hsts_entry);
        }
        if(entry->good_certs != NULL || entry->bad_certs != NULL)
        {
            HSTSPinEntry *hsts_pin_entry = hsts_pin_entry_new();
            hsts_pin_entry->sub_domains = entry->sub_domains;
            fill_cert_set(&hsts_pin_entry->good_certs, entry->good_certs);
            fill_cert_set(&hsts_pin_entry->bad_certs, entry->bad_certs);
            hsts_provider_add_pin_entry(provider, entry->host, hsts_pin_entry);
        }
        entry++;
    }
}

/* Reads a database of known hosts from filename. filename is a utf-8 encoded
 * file, which on each line contains the following tab separated fields:
 *
 * host        - is the known host
 * sub domains - is either true or false compared case-insensitively and
 *               indicates whether the entry applies to sub domains of the
 *               given host
 * expiry      - Expiry time given as the number of microseconds since
 *               January 1, 1970 UTF. Encoded as a decimal.
 *
 * Lines which start with a '#' are treated as comments. Only \n and \r are
 * recognised as line separators.
 */
static gboolean
hsts_provider_load(HSTSProvider *provider, const char *filename)
{

    load_default_database(provider);

    gchar *contents;
    gsize length = 0;
    if(!g_file_get_contents(filename, &contents, &length, NULL))
        return false;

    gboolean success = false;
    if(g_utf8_validate(contents, length, NULL))
    {
        parser_true = g_utf8_casefold("true", -1);
        parser_false = g_utf8_casefold("false", -1);

        gint64 now = g_get_real_time();
        /* TODO: Handle UTF-8 BOM */
        gchar *line = contents, *p = contents;
        gunichar r = g_utf8_get_char("\r"), n = g_utf8_get_char("\n");
        while(*p)
        {
            gunichar c = g_utf8_get_char(p);
            if(c == r || c == n) 
            {
                /* \r\n is treated as two lines but it doesn't since empty
                 * lines are ignored */
                gchar *next = g_utf8_next_char(p);
                *p = '\0'; /* null terminate line */
                parse_line(provider, line, now);
                line = next;
                p = next;
            }
            else
                p = g_utf8_next_char(p);
        }

        success = true;
        g_free(parser_true);
        g_free(parser_false);
    }
    g_free(contents);
    return success;
}

/* Saves the database of known hosts to filename in the format specified for
 * hsts_provider_load */
static void
hsts_provider_save(HSTSProvider *provider, const char *filename)
{
    HSTSProviderPrivate *priv = HSTS_PROVIDER_GET_PRIVATE(provider);
    FILE *file = g_fopen(filename, "w");
    fprintf(file, "# dwb hsts database\n");

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, priv->domains);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const char *host = (const char *)key;
        const HSTSEntry *entry = (HSTSEntry *)value;
        /* TODO: assert MAX_LONG_LONG >= G_MAXINT64 */
        long long expiry = entry->expiry;
        fprintf(file, "%s\t%s\t%lld\n", host, entry->sub_domains ? "true" : "false", expiry);
    }
    fclose(file);
}

/* This callback is called when a new message is put on the session queue. It
 * investigates whether the message is intended for a known host and if so it
 * switches URI scheme to HTTPS.
 */
static void
hsts_provider_request_queued (SoupSessionFeature *feature,
                              SoupSession *session,
                              SoupMessage *msg)
{
    HSTSProvider *provider = HSTS_PROVIDER (feature);

    SoupURI *uri = soup_message_get_uri(msg);
    if(soup_uri_get_scheme(uri) == SOUP_URI_SCHEME_HTTP &&
            hsts_provider_should_secure_host(provider, soup_uri_get_host(uri)))
    {
        soup_uri_set_scheme(uri, SOUP_URI_SCHEME_HTTPS);
        /* Only change port if it explicitly references port 80 as specified in
         * 8.3 [RFC6797]. */
        if(soup_uri_get_port(uri) == 80) 
            soup_uri_set_port(uri, 443);
        soup_session_requeue_message(session, msg);
    }

    /* Only look for HSTS headers sent over https */
    if(soup_uri_get_scheme(uri) == SOUP_URI_SCHEME_HTTPS)
    {
        soup_message_add_header_handler (msg, "got-headers",
                                         HSTS_HEADER_NAME,
                                         G_CALLBACK (hsts_process_hsts_header),
                                         feature);
    }
}


/* This callback is called when a new message is started, that is right before
 * data is sent but after a connection has been made. This callback might be
 * called multiple times for the same message. It is used to check the HTTPS
 * certificates according to the relevant HSTS directives and certificate
 * pinnings.*/
static void
hsts_provider_request_started (SoupSessionFeature *feature,
                               SoupSession *session,
                               SoupMessage *msg,
                               SoupSocket *socket)
{
    HSTSProvider *provider = HSTS_PROVIDER (feature);

    const char *host = soup_uri_get_host(soup_message_get_uri(msg));
    gboolean cancel = false;
    if(hsts_provider_should_secure_host(provider, host))
    {
        GTlsCertificate *certificate;
        GTlsCertificateFlags errors;
        if(!(soup_message_get_https_status(msg, &certificate, &errors) &&
                errors == 0))
            /* If host is known HSTS host the standard specifies that we should ensure strict ssl handling */
            cancel = true;
    }
    HSTSPinEntry *entry;
    GTlsCertificate *certificate;
    GTlsCertificateFlags errors;
    if(!cancel && soup_message_get_https_status(msg, &certificate, &errors) && (entry = hsts_provider_has_cert_pin(provider, host)) != NULL)
    {
        /* If we are connecting over HTTPS to a host with a certificate black/whitelist */
        /* If there is no whitelist assume the certificate chain is good */
        gboolean is_good = entry->good_certs != NULL ? false : true; /* Whether a certificate on the chain is found in the whitelist */
        gboolean is_bad = false; /* Whether a certificate in the chain is on the blacklist */
        GTlsCertificate *cur = certificate;
        while(cur != NULL)
        {
            /* Check each certificate in the chain */

            /* First import the certificate into gnutls */
            GByteArray *cert_bytes;
            g_object_get(G_OBJECT(cur), "certificate", &cert_bytes, NULL);
            
            gnutls_datum_t data;
            data.data = cert_bytes->data;
            data.size = cert_bytes->len;

            gnutls_x509_crt_t cert;
            gnutls_x509_crt_init(&cert);

            /* Then try to get the key_id and check that against the black/white lists */
            int err;
            unsigned char key_id[1024];
            size_t key_id_size = 1024;

            if((err = gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_DER)) == GNUTLS_E_SUCCESS &&
                    (err = gnutls_x509_crt_get_key_id(cert, 0, key_id, &key_id_size)) == GNUTLS_E_SUCCESS
                    )
            {
                
                char *key_id_base64 = g_base64_encode(key_id, key_id_size);
                is_good = is_good || 
                    (entry->good_certs != NULL && g_hash_table_lookup(entry->good_certs, key_id_base64));
                is_bad  = is_bad  ||
                    (entry->bad_certs != NULL && g_hash_table_lookup(entry->bad_certs, key_id_base64));
                g_free(key_id_base64);
            }
            else
            {
                printf("HSTS: Warning: Problems getting certificate key id for a certificate of %s\n", host);
            }

            /* Cleanup */
            gnutls_x509_crt_deinit(cert);
            g_byte_array_unref(cert_bytes);
            cur = g_tls_certificate_get_issuer(cur);
        }
        /* If we aren't explicitly on the whitelist or a certificate is on the
         * blacklist, cancel the message. Said simpler a certificate is
         * accepted only if it has at least one certificate in it's chain on
         * the whitelist and none on the blacklist
         */
        if(!is_good || is_bad)
            cancel = true;
    }
    if(cancel)
        soup_session_cancel_message(session, msg, SOUP_STATUS_SSL_FAILED);
}

/* Removes added callbacks on message unqueue
 */
static void
hsts_provider_request_unqueued (SoupSessionFeature *feature,
                                  SoupSession *session,
                                  SoupMessage *msg)
{
    g_signal_handlers_disconnect_by_func (msg, hsts_process_hsts_header, feature);
}

/* Initialise the SoupSessionFeature interface.
 */
static void
hsts_provider_session_feature_init (SoupSessionFeatureInterface *feature_interface,
				      gpointer interface_data)
{
    feature_interface->request_queued = hsts_provider_request_queued;
    feature_interface->request_started = hsts_provider_request_started;
    feature_interface->request_unqueued = hsts_provider_request_unqueued;
}

/* Indicates whether hsts has been initialised */
static gboolean s_init = false;
static HSTSProvider *s_provider;

gboolean
hsts_running()
{
    return s_init && GET_BOOL("hsts");
}

/* Activates hsts */
void
hsts_activate()
{
    if(!hsts_init())
        return;
    soup_session_add_feature(dwb.misc.soupsession, SOUP_SESSION_FEATURE(s_provider));
}

/* Deactivates hsts */
void
hsts_deactivate()
{
    if(!s_init)
        return;
    soup_session_remove_feature(dwb.misc.soupsession, SOUP_SESSION_FEATURE(s_provider));
}

/* Save current hsts lists */
void
hsts_save()
{
    if(hsts_running())
        hsts_provider_save(s_provider, dwb.files[FILES_HSTS]);
}

/* Initialises the hsts implementation */
gboolean
hsts_init()
{
    if(s_init)
        return true;
    if(!GET_BOOL("hsts"))
        return false;

    s_provider = g_object_new(HSTS_TYPE_PROVIDER, NULL);
    s_init = true;

    hsts_provider_load(s_provider, dwb.files[FILES_HSTS]);
    hsts_activate();

    return true;
}

/* Finalises the hsts implementation */
void
hsts_end()
{
    hsts_save();
    hsts_deactivate();

    if(s_init)
    {
        g_object_unref(s_provider);
        s_init = false;
    }
}
#endif
