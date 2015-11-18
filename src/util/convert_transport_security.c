/*
 * Copyright (c) 2012 Adam Ehlers Nyholm Thomsen
 * Copyright (c) 2013 Stefan Bolte <portix@gmx.net>
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
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <json.h>

/* Converts the static .certs and .json whitelist to a header file of the
 * apropriate type.
 *
 * Warning: This file is slightly non portable as it uses getline. */


/* Indicates whether a given pinset included a list of good certificates and/or
 * a list of bad certificates. */
typedef enum _has_certs {
    HAS_GOOD_CERTS = 1,
    HAS_BAD_CERTS  = 2,
} has_certs;

/* Whether a certificate is a good certificate or a bad certificate */
typedef enum _cert_type {
    GOOD_CERT,
    BAD_CERT,
} cert_type;

/* Maps pinset name to has_certs.
 */
GHashTable *pins;

#define cert_filename             "transport_security_state_static.certs"
#define json_filename             "transport_security_state_static.json"
#define certificate_begin         "-----BEGIN CERTIFICATE-----"
#define certificate_end           "-----END CERTIFICATE-----"
#define sha1_prefix               "sha1/"
#define cert_template             "static const char s_hsts_cert_hash_%s[] = \"%s\";\n"

#define cert_list_template_begin "static const char * const s_hsts_cert_list_%s_%s[] = {\n"
#define cert_list_template_entry "    s_hsts_cert_hash_%s,\n"
#define cert_list_template_end   "    NULL,\n};\n\n"

#define entry_list_begin  "static const HSTSPreloadEntry s_hsts_preload[] = {\n"
#define entry_list_end    "};\n"
#define entry_list_length "static const size_t s_hsts_preload_length = %zu\n;"

const char *gboolean_to_string(gboolean val){
    return val ? "true" : "false";
}
const char *cert_type_to_string(cert_type val){
    return val == GOOD_CERT ? "good" : "bad";
}
void print_has_certs(const char *name, has_certs cert_status, cert_type val){
    if(cert_status & ((val == GOOD_CERT) ? HAS_GOOD_CERTS : HAS_BAD_CERTS)) {
        printf("s_hsts_cert_list_%s_%s", cert_type_to_string(val), name);
    } else
        printf("NULL");
}
void print_entry_list_entry(const char *host, const char *pin_name, gboolean hsts, gboolean sub_domains){
    has_certs cert_status = pin_name != NULL ? *((has_certs *)g_hash_table_lookup(pins, pin_name)) : 0;
    char *host_safe = g_strescape(host, "");
    printf("    {\"%s\", ", host);
    g_free(host_safe);
    print_has_certs(pin_name, cert_status, GOOD_CERT);
    printf(", ");
    print_has_certs(pin_name, cert_status, BAD_CERT);
    printf(", ");
    printf("%s, %s},\n", gboolean_to_string(hsts), gboolean_to_string(sub_domains));
}

/* The ID size should be 20, but give some room for changes */
#define MAX_ID_SIZE 4096 

/* Parse the certificate file and for each certificate print the base64 encoded
 * certificate key id, to be used in pinsets */
gboolean parse_certs(const char *filename)
{
    FILE *file = g_fopen(filename, "r");
    char *line = NULL;
    size_t line_size = 0;
    size_t buffer_size = 4096;
    size_t buffer_used = 0;
    unsigned char *buffer = g_malloc(sizeof(unsigned char)*buffer_size);
    while(getline(&line, &line_size, file) >= 0)
    {
        g_strstrip(line);
        size_t len = strlen(line);
        if(len == 0 || line[0] == '#')
            continue; /* Ignore comments and pure whitespace lines */
        char *name = g_strdup(line);
        char *key_id_base64;

        if(getline(&line, &line_size, file) < 0)
        {
            fprintf(stderr, "Unexpected end of file while parsing %s\n", name);
            return FALSE;
        }
        g_strstrip(line);
        if(g_str_has_prefix(line, certificate_begin))
        {
            /* If it is a certificate entry: base64 decode the certificate,
             * load it using gnutls, and compute the base64 encoded key id.
             */
            gint state = 0;
            guint save = 0;
            buffer_used = 0;
            ssize_t read;
            while((read = getline(&line, &line_size, file)) >= 0 && !g_str_has_prefix(line, certificate_end))
            {
                /* Read certificate line by line and base64 decode */
                g_strstrip(line);
                size_t len = strlen(line);
                gboolean to_realloc = FALSE;
                while(len > buffer_size - buffer_used - 3) 
                {
                    to_realloc = TRUE;
                    buffer_size *= 2;
                }
                if(to_realloc)
                {
                    /* Increase buffer_size if it is a long certificate -- this
                     * should never really happen */
                    fprintf(stderr, "Warning: Increasing buffer size to %zd\n", buffer_size);
                    buffer = g_realloc(buffer, buffer_size);
                }
                buffer_used += g_base64_decode_step(line, len, &buffer[buffer_used], &state, &save);
            }
            if(read < 0)
            {
                fprintf(stderr, "Unexpected end of file while parsing base64 certificate of %s\n", name);
                return FALSE;
            }
            gnutls_datum_t binary;
            binary.data = buffer;
            binary.size = buffer_used;

            /* Load the certificate and compute the key id */
            gnutls_x509_crt_t cert;
            gnutls_x509_crt_init(&cert);
            int err;
            if((err = gnutls_x509_crt_import(cert, &binary, GNUTLS_X509_FMT_DER)) != GNUTLS_E_SUCCESS)
            {
                fprintf(stderr, "Error while decoding certificate of %s, error was %d, is it perhaps PEM encoded?\n", name, err);
                return FALSE;
            }
            unsigned char key_id[MAX_ID_SIZE];
            size_t key_id_size = MAX_ID_SIZE;
            if((err = gnutls_x509_crt_get_key_id(cert, 0, key_id, &key_id_size)) != GNUTLS_E_SUCCESS)
            {
                fprintf(stderr, "Couldn't retrieve the key id for the certificate of %s, error was %d\n", name, err);
                return FALSE;
            }
            if(key_id_size != 20)
            {
                /* This might be problematic, I don't know */
                fprintf(stderr, "Warning: Key id for %s isn't 20 bytes long, this means it probably isn't a sha-1 hash...\n", name);
            }
            key_id_base64 = g_base64_encode(key_id, key_id_size);
            gnutls_x509_crt_deinit(cert);
        }
        else if(g_str_has_prefix(line, sha1_prefix))
        {
            /* If it is given as a sha-1 hash directly */
            key_id_base64 = g_strdup(&line[strlen(sha1_prefix)]);
        }
        else
        {
            fprintf(stderr, "Unrecognised line: %s\n", line);
            return FALSE;
        }

        printf(cert_template, name, key_id_base64);

        g_free(name);
        g_free(key_id_base64);
    }
    printf("\n");
    g_free(buffer);
    free(line);
    fclose(file);
    return TRUE;
}


/* Writes a list of certificate id names
 * Params:
 * name - The name of the pinset
 * type - Whether it is a list of good or bad certificates
 * list - The json_array of certificate id names
 */
gboolean write_cert_list(const char *name, cert_type type, has_certs *certs, json_object *list)
{
    if(list == NULL)
        return TRUE;
    if(!json_object_is_type(list, json_type_array))
        return FALSE;
    int len = json_object_array_length(list);
    printf(cert_list_template_begin, cert_type_to_string(type), name);
    int i;
    for(i = 0; i < len; i++)
    {
        printf(cert_list_template_entry, json_object_get_string(json_object_array_get_idx(list, i)));
    }
    printf(cert_list_template_end);
    *certs |= (type == GOOD_CERT) ? HAS_GOOD_CERTS : HAS_BAD_CERTS;
    return TRUE;
}

/* Allocates a new has_certs enum and initializes it to 0(No certificates) */
has_certs *has_certs_new()
{
    has_certs *var = g_malloc(sizeof(has_certs));
    *var = 0;
    return var;
}

/* For each pinset check whether it has a list of good certificates and if so
 * print, and do likewise for the bad certificates */
gboolean handle_pinsets(json_object *pinsets)
{
    int len = json_object_array_length(pinsets), i;
    for(i = 0; i < len; i++)
    {
        json_object *pin_list = json_object_array_get_idx(pinsets, i);
        if(pin_list == NULL || !json_object_is_type(pin_list, json_type_object))
        {
            fprintf(stderr, "pinset %d is not of type object\n", i);
            return FALSE;
        }
        json_object *name_obj, *good_hashes, *bad_hashes;
        if(json_object_object_get_ex(pin_list, "name", &name_obj) == FALSE || !json_object_is_type(name_obj, json_type_string))
        {
            fprintf(stderr, "Couldn't get name from pinset %d\n", i);
            return FALSE;
        }
        const char *name = json_object_get_string(name_obj);

        json_object_object_get_ex(pin_list, "static_spki_hashes", &good_hashes);
        json_object_object_get_ex(pin_list, "bad_static_spki_hashes", &bad_hashes);
        has_certs *certs = has_certs_new();
        if(!write_cert_list(name, GOOD_CERT, certs, good_hashes) ||
                !write_cert_list(name, BAD_CERT, certs, bad_hashes))
        {
            fprintf(stderr, "Couldn't parse hash lists for pinset %s\n", name);
            return FALSE;
        }

        g_hash_table_insert(pins, g_strdup(name), certs);
    }
    return TRUE;
}

/* For each entry convert it into the structure of an HSTSPreloadEntry and
 * print it as c code on stdout.
 */
gboolean handle_entries(json_object *entries)
{
    int len = json_object_array_length(entries);
    printf(entry_list_begin);
    int i;
    for(i = 0; i < len; i++)
    {
        json_object *entry = json_object_array_get_idx(entries, i);
        if(entry == NULL || !json_object_is_type(entry, json_type_object))
        {
            fprintf(stderr, "Entry %d wasn't a json object\n", i);
            return FALSE;
        }

        /* Get hostname */
        json_object *name_obj;
        if(json_object_object_get_ex(entry, "name", &name_obj) == FALSE ||
                !json_object_is_type(name_obj, json_type_string))
        {
            fprintf(stderr, "Couldn't process name from entry %d\n", i);
            return FALSE;
        }
        const char *name = json_object_get_string(name_obj);
        char *host = g_hostname_to_unicode(name);

        /* Get whether to enable hsts for host */
        json_object *mode;
       	json_object_object_get_ex(entry, "mode", &mode);
        gboolean hsts = mode != NULL;
        if(hsts && strcmp(json_object_get_string(mode), "force-https") != 0)
        {
            fprintf(stderr, "Unknown mode for entry %s: %s", name, json_object_get_string(mode));
        }

        /* Get sub domains directive */
        json_object *include_subdomains;
       	json_object_object_get_ex(entry, "include_subdomains", &include_subdomains);
        gboolean sub_domains =  include_subdomains != NULL &&
            json_object_get_boolean(include_subdomains);
        if(include_subdomains != NULL && !json_object_is_type(include_subdomains, json_type_boolean))
        {
            fprintf(stderr, "include_subdomains for entry %s wasn't of type boolean\n", name);
            return FALSE;
        }

        /* Get pins directive */
        json_object *entry_pins;
        const char *pin_name = NULL;
        if(json_object_object_get_ex(entry, "pins", &entry_pins) == TRUE)
        {
            if(!json_object_is_type(entry_pins, json_type_string))
            {
                fprintf(stderr, "non string pins entry for %s\n", name);
                return FALSE;
            }
            pin_name = json_object_get_string(entry_pins);
            if(g_hash_table_lookup(pins, pin_name) == NULL)
            {
                fprintf(stderr, "unrecognised pin name in entry for %s\n", name);
            }
        }

        print_entry_list_entry(host, pin_name, hsts, sub_domains);
        g_free(host);
    }
    size_t length = len;
    printf(entry_list_end);
    printf(entry_list_length, length);
    return TRUE;
}

/* Parse the json file and print the relevant c code */
gboolean parse_json(const char *filename)
{
    /* Read and parse the file */
    char *file;
    if(!g_file_get_contents(filename, &file, NULL, NULL))
    {
        fprintf(stderr, "Couldn't read JSON file: %s\n", filename);
        return FALSE;
    }

    json_object *json = json_tokener_parse(file);
    if(json == NULL)
    {
        fprintf(stderr, "There was an error while parsing %s\n", filename);
        return FALSE;
    }

    /* Parse and handle the pinsets entry */
    json_object *pinsets;
    pins = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free); 
    if(json_object_object_get_ex(json, "pinsets", &pinsets) == FALSE || !json_object_is_type(pinsets, json_type_array) ||
            !handle_pinsets(pinsets))
    {
        fprintf(stderr, "Error while handling pinsets\n");
        return FALSE;
    }

    /* Parse and handle the list of hostnames */
    json_object *entries;
    if(json_object_object_get_ex(json, "entries", &entries) == FALSE || !json_object_is_type(entries, json_type_array) ||
            !handle_entries(entries))
    {
        fprintf(stderr, "Error while handling entries\n");
        return FALSE;
    }
    
    g_free(file);
    g_hash_table_destroy(pins);
    json_object_put(json);
    return TRUE;
}

int main(){
    gnutls_global_init();
    if(!parse_certs(cert_filename))
        return -1;
    if(!parse_json(json_filename))
        return -1;
    gnutls_global_deinit();
    return 0;
}
