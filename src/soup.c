/*
 * Copyright (c) 2010-2014 Stefan Bolte <portix@gmx.net>
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

#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <sys/file.h>
#include "dwb.h"
#include "entry.h"
#include "util.h"
#include "domain.h"
#include "scripts.h"
#include "soup.h"
#include "js.h"
#define DWB_SOUP_CHECK_EXPIRATION(multiplier) \
    (s_expiration = (s_expiration != LONG_MIN && s_expiration != LONG_MAX && LONG_MAX / (multiplier) > s_expiration) ? \
        s_expiration * (multiplier) : -1)


    
static SoupCookieJar *s_jar;
static guint s_changed_id;
static SoupCookieJar *s_tmp_jar;
static long int s_expiration;

const char *
dwb_soup_get_host(WebKitWebFrame *frame)
{
    SoupMessage *msg = dwb_soup_get_message(frame);
    if (msg == NULL)
        return NULL;

    SoupURI *uri = soup_message_get_uri(msg);
    return soup_uri_get_host(uri);
}
const char *
dwb_soup_get_domain(WebKitWebFrame *frame)
{
    const char *host = dwb_soup_get_host(frame);
    if (host == NULL)
        return NULL;
    return domain_get_base_for_host(host);
}

/*{{{*/
static void
dwb_soup_clear_jar(SoupCookieJar *jar) 
{
    GSList *all_cookies = soup_cookie_jar_all_cookies(jar);

    for (GSList *l = all_cookies; l; l=l->next) 
        soup_cookie_jar_delete_cookie(jar, l->data);

    soup_cookies_free(all_cookies);
}/*}}}*/

/* dwb_soup_allow_cookie(GList *, const char, CookieStorePolicy) {{{*/
static DwbStatus
dwb_soup_allow_cookie_simple(GList **whitelist, const char *filename, CookieStorePolicy policy) 
{
    GSList *list = domain_get_cookie_domains(CURRENT_WEBVIEW());
    char *domain;

    if (list == NULL)
        return STATUS_ERROR;

    for (GSList *l = list; l; l=l->next) 
    {
        domain = l->data;
        if ( *whitelist == NULL || g_list_find_custom(*whitelist, domain, (GCompareFunc) g_strcmp0) == NULL ) 
        {
            if (dwb_confirm(dwb.state.fview, "Allow %s cookies for domain %s [y/n]", policy == COOKIE_ALLOW_PERSISTENT ? "persistent" : "session", domain)) 
            {
                *whitelist = g_list_append(*whitelist, g_strdup(domain));
                util_file_add(filename, domain, true, -1);
            }
        }
    }
    CLEAR_COMMAND_TEXT();

    /* Only the first domain ist allocated */
    FREE0(list->data);

    g_slist_free(list);

    return STATUS_OK;
}
const char *
soup_get_header_from_request(WebKitNetworkRequest *request, const char *name) 
{
    SoupMessage *msg = webkit_network_request_get_message(request);
    if (msg != NULL) 
        return soup_message_headers_get_one(msg->request_headers, name);

    return NULL;
}
SoupMessage *
dwb_soup_get_message(WebKitWebFrame *frame) 
{
    WebKitWebDataSource *ds = webkit_web_frame_get_data_source(frame);
    if (ds == NULL) 
        return NULL;
    WebKitNetworkRequest *request = webkit_web_data_source_get_request(ds);
    if (request == NULL)
        return NULL;
    return webkit_network_request_get_message(request);
}
const char *
soup_get_header(GList *gl, const char *name) 
{
    WebKitWebFrame *frame = webkit_web_view_get_main_frame(WEBVIEW(gl));
    WebKitWebDataSource *data = webkit_web_frame_get_data_source(frame);
    WebKitNetworkRequest *request = webkit_web_data_source_get_request(data);
    return soup_get_header_from_request(request, name);
}

void
dwb_soup_allow_cookie_tmp() 
{
    const char *domain;

    GSList *last_cookies = domain_get_cookie_domains(CURRENT_WEBVIEW());
    if (last_cookies == NULL)
        return;

    for (GSList *l = last_cookies; l; l=l->next) 
    {
        domain = l->data;
        if ( g_list_find_custom(dwb.fc.cookies_session_allow, domain, (GCompareFunc)g_strcmp0)  == NULL ) 
            dwb.fc.cookies_session_allow = g_list_append(dwb.fc.cookies_session_allow, g_strdup(domain));
    }

    dwb_reload(dwb.state.fview);

    FREE0(last_cookies->data);
    g_slist_free(last_cookies);
}

DwbStatus
dwb_soup_allow_cookie(GList **whitelist, const char *filename, CookieStorePolicy policy) 
{
    DwbStatus ret = STATUS_ERROR;
    int length;
    const char *domain;
    SoupCookie *c;
    GSList *asked = NULL, *allowed = NULL;

    GSList *last_cookies = soup_cookie_jar_all_cookies(s_tmp_jar);
    if (last_cookies == NULL) 
        return dwb_soup_allow_cookie_simple(whitelist, filename, policy);


    for (GSList *l = last_cookies; l; l=l->next) 
    {
        c = l->data;
        domain = soup_cookie_get_domain(c);
        if ( *whitelist == NULL || g_list_find_custom(*whitelist, domain, (GCompareFunc) g_strcmp0) == NULL ) 
        {
            /* only ask once, if it was already prompted for this domain and allowed it will be handled
             * in the else clause */
            if (g_slist_find_custom(asked, domain, (GCompareFunc)g_strcmp0))
                continue;

            if (dwb_confirm(dwb.state.fview, "Allow %s cookies for domain %s [y/n]", policy == COOKIE_ALLOW_PERSISTENT ? "persistent" : "session", domain)) 
            {
                *whitelist = g_list_append(*whitelist, g_strdup(domain));
                util_file_add(filename, domain, true, -1);
                allowed = g_slist_prepend(allowed, soup_cookie_copy(c));
            }
            asked = g_slist_prepend(asked, (char*)domain);
            CLEAR_COMMAND_TEXT();
        }
        else 
            allowed = g_slist_prepend(allowed, soup_cookie_copy(c));
    }

    length = g_slist_length(allowed);
    if (length > 0) 
        ret = STATUS_OK;

    if (policy == COOKIE_STORE_PERSISTENT) 
        dwb_soup_save_cookies(allowed);

    /* Save all cookies to the jar */
    for (GSList *l = allowed; l; l=l->next)
        soup_cookie_jar_add_cookie(s_jar, l->data);

    dwb_reload(dwb.state.fview);

    /* soup_cookie_jar_add_cookie steals the cookie, it must no be freed */
    g_slist_free(allowed);
    g_slist_free(asked);
    soup_cookies_free(last_cookies);
    return ret;
}/*}}}*/

/* dwb_soup_clean() {{{*/
void 
dwb_soup_clean() 
{
    dwb_soup_clear_jar(s_tmp_jar);
}/*}}}*/

/* dwb_soup_get_host_from_request(WebKitNetworkRequest ) {{{*/
const char *
dwb_soup_get_host_from_request(WebKitNetworkRequest *request) 
{
    const char *host = NULL;
    SoupMessage *msg = webkit_network_request_get_message(request);
    if (msg != NULL) 
    {
        SoupURI *suri = soup_message_get_uri(msg);
        if (suri != NULL)
            host = soup_uri_get_host(suri);
    }
    return host;
}/*}}}*/

/* dwb_soup_save_cookies(cookies) {{{*/
void 
dwb_soup_save_cookies(GSList *cookies) 
{
    int fd = open(dwb.files[FILES_COOKIES], 0);
    SoupCookieJar *jar;
    SoupDate *date;

    flock(fd, LOCK_EX);
    jar = soup_cookie_jar_text_new(dwb.files[FILES_COOKIES], false);
    for (GSList *l=cookies; l; l=l->next) 
    {
        date = soup_cookie_get_expires(l->data);
        if (date && !soup_date_is_past(date))
            soup_cookie_jar_add_cookie(jar, soup_cookie_copy(l->data));
    }
    g_object_unref(jar);

    flock(fd, LOCK_UN);
    close(fd);
}/*}}}*/

/* dwb_test_cookie_allowed(const char *)     return:  gboolean{{{*/
static gboolean 
dwb_soup_test_cookie_allowed(GList *list, SoupCookie *cookie) 
{
    g_return_val_if_fail(cookie != NULL, false);
    g_return_val_if_fail(cookie->domain != NULL, false);
    for (GList *l = list; l; l=l->next) 
    {
        if (l->data && soup_cookie_domain_matches(cookie, l->data)) 
            return true;
    }
    return false;
}/*}}}*/

/* dwb_soup_set_cookie_accept_policy {{{*/
DwbStatus 
dwb_soup_set_cookie_accept_policy(const char *policy) 
{
    int apo = 37;
    DwbStatus ret = STATUS_OK;
    if (policy == NULL || ! g_ascii_strcasecmp(policy, "always")) 
        apo = SOUP_COOKIE_JAR_ACCEPT_ALWAYS;

    if (! g_ascii_strcasecmp(policy, "nothirdparty"))
        apo = SOUP_COOKIE_JAR_ACCEPT_NO_THIRD_PARTY;
    else if (! g_ascii_strcasecmp(policy, "never"))
        apo = SOUP_COOKIE_JAR_ACCEPT_NEVER;

    if (apo == 37) 
    {
        dwb_set_error_message(dwb.state.fview, "Invalid value for cookies-accept-policy: %d, using 0", policy);
        apo = 0;
        ret = STATUS_ERROR;
    }
    soup_cookie_jar_set_accept_policy(s_jar, apo);
    return ret;
}/*}}}*/

/* dwb_soup_get_cookie_store_policy(const char *policy) {{{*/
CookieStorePolicy 
dwb_soup_get_cookie_store_policy(const char *policy) 
{
    if (policy == NULL)
        return COOKIE_STORE_SESSION;
    if (! g_ascii_strcasecmp(policy, "persistent")) 
        return COOKIE_STORE_PERSISTENT;
    else if (! g_ascii_strcasecmp(policy, "never")) 
        return COOKIE_STORE_NEVER;
    else
        return COOKIE_STORE_SESSION;
}/*}}}*/

void
dwb_soup_cookie_save(SoupCookie *cookie)
{
    g_signal_handler_block(s_jar, s_changed_id);
    soup_cookie_jar_add_cookie(s_jar, soup_cookie_copy(cookie));
    g_signal_handler_unblock(s_jar, s_changed_id);

}
void
dwb_soup_cookie_delete(SoupCookie *cookie)
{
    g_signal_handler_block(s_jar, s_changed_id);
    soup_cookie_jar_delete_cookie(s_jar, cookie);
    g_signal_handler_unblock(s_jar, s_changed_id);
}
GSList *
dwb_soup_get_all_cookies(void)
{
    return soup_cookie_jar_all_cookies(s_jar);
}
/*dwb_soup_cookie_changed_cb {{{*/
static void 
dwb_soup_cookie_changed_cb(SoupCookieJar *jar, SoupCookie *old, SoupCookie *new_cookie, gpointer *p) 
{
    SoupDate *date;
    time_t max_time;

    if (new_cookie) 
    {
        /**
         * Emitted when a cookie will be added to the session cookie jar
         * @event addCookie
         * @memberOf signals
         * @param {signals~onAddCookie} callback
         *      Callback function that will be called when the signal is emitted
         * @since 1.5
         * */
        /**
         * Callback when a cookie is added to the session cookie jar. If
         * expiration of the cookie is in the future and persistent cookies are
         * allowed or the cookie is whitelisted it will also be added to
         * the persistent cookie jar. 
         *
         * @callback signals~onAddCookie
         * @since 1.5
         * @param {Cookie}  cookie 
         *      The copy of the cookie that will be added to the jar
         *
         * @returns {Boolean}
         *      If the function returns true the original cookie won't be added to the
         *      jar. To change a cookie this function must return true and the
         *      copy of the cookie can be added to the jar using {@link Cookie.save|save}
         *
         * @example
         * // Make the cookie a session cookie
         * Signal.connect("addCookie", function(cookie) {
         *      cookie.setMaxAge(-1);
         *      cookie.save();
         *      return true;
         * });
         * */
        if (EMIT_SCRIPT(ADD_COOKIE))
        {
            ScriptSignal sig = { .jsobj = scripts_make_cookie(soup_cookie_copy(new_cookie)), SCRIPTS_SIG_META(NULL, ADD_COOKIE, 0) };
            if (scripts_emit(&sig))
            {
                g_signal_handler_block(jar, s_changed_id);
                soup_cookie_jar_delete_cookie(jar, new_cookie);
                g_signal_handler_unblock(jar, s_changed_id);
                return;
            }
        }

        /* Check if this is a super-cookie */
        if (new_cookie->domain) 
        {
            const char *base = new_cookie->domain;

            if (*base == '.')
                base++;

            if (domain_get_tld(base) == NULL) 
            {
                fprintf(stderr, "Site tried to set super-cookie @ TLD %s (base %s)\n", new_cookie->domain, base);
                return;
            }
        }

        if (dwb.state.cookie_store_policy == COOKIE_STORE_PERSISTENT || dwb_soup_test_cookie_allowed(dwb.fc.cookies_allow, new_cookie)) 
        {
            if (s_expiration <= 0) 
                return;
            date = soup_cookie_get_expires(new_cookie);
            // session cookie
            if (!date) 
                return;
            max_time = soup_date_to_time_t(date) - time(NULL);
            if (max_time > 0)
                soup_cookie_set_max_age(new_cookie, MIN(s_expiration, max_time));
        } 
        else 
        { 
            soup_cookie_jar_add_cookie(s_tmp_jar, soup_cookie_copy(new_cookie));

            if (dwb.state.cookie_store_policy == COOKIE_STORE_NEVER && !dwb_soup_test_cookie_allowed(dwb.fc.cookies_session_allow, new_cookie) ) 
            {
                g_signal_handler_block(jar, s_changed_id);
                soup_cookie_jar_delete_cookie(jar, new_cookie);
                g_signal_handler_unblock(jar, s_changed_id);
            }
            else 
            {
                soup_cookie_set_max_age(new_cookie, -1);
            }
        }
    }
}/*}}}*/

void
dwb_soup_sync_cookies() 
{
    int fd = open(dwb.files[FILES_COOKIES], 0);
    if (fd == -1)
    {
        perror("open");
        return;
    }

    flock(fd, LOCK_EX);
    SoupDate *date;
    if ( unlink(dwb.files[FILES_COOKIES]) == -1)
    {
        perror("unlink");
    }


    SoupCookieJar *j = soup_cookie_jar_text_new(dwb.files[FILES_COOKIES], false);
    GSList *all_cookies = soup_cookie_jar_all_cookies(s_jar);
    GSList *deleted = NULL;
    if (all_cookies != NULL)
    {
        for (GSList *l = all_cookies; l; l=l->next) 
        {
            date = soup_cookie_get_expires(l->data);
            // keep session cookies and valid cookies in the jar
            if (!date || !soup_date_is_past(date))
                soup_cookie_jar_add_cookie(j, l->data);
            else 
                deleted = g_slist_prepend(deleted, l->data);
        }
        g_signal_handler_block(s_jar, s_changed_id);
        for (GSList *l = deleted; l; l=l->next)
        {
            soup_cookie_jar_delete_cookie(s_jar, l->data);
        }
        g_signal_handler_unblock(s_jar, s_changed_id);
        g_slist_free(deleted);
        g_slist_free(all_cookies);
    }


    g_object_unref(j);

    flock(fd, LOCK_UN);
    close(fd);
}

void 
dwb_soup_clear_cookies() 
{
    dwb_soup_clear_jar(s_tmp_jar);
    dwb_soup_clear_jar(s_jar);
}

/* dwb_soup_init_cookies {{{*/
void
dwb_soup_init_cookies(SoupSession *s) 
{
    SoupDate *date;

    s_jar = soup_cookie_jar_new(); 
    s_tmp_jar = soup_cookie_jar_new();

    dwb_soup_set_cookie_accept_policy(GET_CHAR("cookies-accept-policy"));
    SoupCookieJar *old_cookies = soup_cookie_jar_text_new(dwb.files[FILES_COOKIES], true);

    GSList *l = soup_cookie_jar_all_cookies(old_cookies);
    for (; l; l=l->next ) 
    {
        date = soup_cookie_get_expires(l->data);
        if (date && !soup_date_is_past(date))
            soup_cookie_jar_add_cookie(s_jar, soup_cookie_copy(l->data)); 
        else 
            soup_cookie_jar_delete_cookie(old_cookies, l->data);
    }

    soup_cookies_free(l);
    g_object_unref(old_cookies);

    soup_session_add_feature(s, SOUP_SESSION_FEATURE(s_jar));
    s_changed_id = g_signal_connect(s_jar, "changed", G_CALLBACK(dwb_soup_cookie_changed_cb), NULL);
}/*}}}*/

/* dwb_init_proxy{{{*/
void 
dwb_soup_init_proxy() 
{
    const char *proxy;
    gboolean use_proxy = GET_BOOL("proxy");
    if ( !(proxy =  g_getenv("http_proxy")) && !(proxy =  GET_CHAR("proxy-url")) )
        return;

    if (dwb.misc.proxyuri)
        g_free(dwb.misc.proxyuri);

    dwb.misc.proxyuri = g_strrstr(proxy, "://") ? g_strdup(proxy) : g_strdup_printf("http://%s", proxy);
    SoupURI *uri = soup_uri_new(dwb.misc.proxyuri);
    g_object_set(dwb.misc.soupsession, "proxy-uri", use_proxy ? uri : NULL, NULL); 
    soup_uri_free(uri);
}/*}}}*/

DwbStatus
dwb_soup_set_cookie_expiration(const char *expiration_string) 
{
    if (g_regex_match_simple("\\s*[0-9]+\\s*d", expiration_string, 0, 0))
    {
        s_expiration = strtol(expiration_string, NULL, 10);
        if (DWB_SOUP_CHECK_EXPIRATION(86400) != -1)
        {
            return STATUS_OK;
        }
    }
    else if (g_regex_match_simple("\\s*[0-9]+\\s*h", expiration_string, 0, 0))
    {
        s_expiration = strtol(expiration_string, NULL, 10);
        if (DWB_SOUP_CHECK_EXPIRATION(3600) != -1)
        {
            return STATUS_OK;
        }
    }
    else if (g_regex_match_simple("\\s*[0-9]+\\s*m", expiration_string, 0, 0))
    {
        s_expiration = strtol(expiration_string, NULL, 10);
        if (DWB_SOUP_CHECK_EXPIRATION(60) != -1)
        {
            return STATUS_OK;
        }
    }
    else if (g_regex_match_simple("\\s*[0-9]+", expiration_string, 0, 0))
    {
        s_expiration = strtol(expiration_string, NULL, 10);
        if (DWB_SOUP_CHECK_EXPIRATION(1) != -1)
        {
            return STATUS_OK;
        }
    }
    s_expiration = 0;
    return STATUS_ERROR;
}

/* dwb_soup_init_session_features() {{{*/
DwbStatus 
dwb_soup_init_session_features() 
{
#ifdef WITH_LIBSOUP_2_38
    gboolean cert = GET_BOOL("ssl-use-system-ca-file");
    g_object_set(dwb.misc.soupsession, 
            SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, cert, NULL);
#else 
    char *cert = GET_CHAR("ssl-ca-cert");
    if (cert != NULL && g_file_test(cert, G_FILE_TEST_EXISTS)) 
    {
        g_object_set(dwb.misc.soupsession, 
                SOUP_SESSION_SSL_CA_FILE, cert, NULL);
    }
#endif
    g_object_set(dwb.misc.soupsession, SOUP_SESSION_SSL_STRICT, GET_BOOL("ssl-strict"), NULL);
    return STATUS_OK;
}/*}}}*/

/* dwb_soup_end(() {{{*/
void
dwb_soup_end() 
{
    g_object_unref(s_tmp_jar);
    g_object_unref(s_jar);
    g_free(dwb.misc.proxyuri);
}/*}}}*/

/* dwb_soup_init() {{{*/
void 
dwb_soup_init() 
{
    dwb.misc.soupsession = webkit_get_default_session();
    dwb_soup_init_proxy();
    dwb_soup_init_cookies(dwb.misc.soupsession);
    dwb_soup_init_session_features();
#if 0
    SoupLogger *sl = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
    soup_session_add_feature(dwb.misc.soupsession, sl);
#endif
}/*}}}*/
