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

#include "script_private.h"

static void 
set_request(JSContextRef ctx, SoupMessage *msg, JSValueRef val, JSValueRef *exc)
{
    char *content_type = NULL, *body = NULL;
    JSObjectRef data = JSValueToObject(ctx, val, exc);
    if (data == NULL)
        return;
    content_type = js_get_string_property(ctx, data, "contentType");
    if (content_type != NULL)
    {
        body = js_get_string_property(ctx, data, "data");
        if (body != NULL) {
            soup_message_set_request(msg, content_type, SOUP_MEMORY_COPY, body, strlen(body));
        }
    }
    g_free(content_type);
    g_free(body);
}
static JSValueRef 
get_message_data(SoupMessage *msg) 
{
    const char *name, *value;
    SoupMessageHeadersIter iter;
    JSObjectRef o = NULL, ho;
    JSValueRef ret = NIL;
    JSStringRef s;

    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) {
        return NIL;
    }

    o = JSObjectMake(ctx, NULL, NULL);
    js_set_object_property(ctx, o, "body", msg->response_body->data, NULL);

    ho = JSObjectMake(ctx, NULL, NULL);

    soup_message_headers_iter_init(&iter, msg->response_headers);
    while (soup_message_headers_iter_next(&iter, &name, &value)) 
        js_set_object_property(ctx, ho, name, value, NULL);

    s = JSStringCreateWithUTF8CString("headers");
    JSObjectSetProperty(ctx, o, s, ho, kJSDefaultProperty, NULL);
    JSStringRelease(s);
    ret = o;

    scripts_release_global_context();

    return ret;
}

/**
 * Callback called when a response from a request was retrieved
 *
 * @callback net~onResponse
 *
 * @param {Object} data 
 * @param {String} data.body 
 *      The message body
 * @param {Object} data.he 
 *      An object that contains all response headers
 * @param {SoupMessage} message The soup message
 * */
static void
request_callback(SoupSession *session, SoupMessage *message, JSObjectRef function) 
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        if (message->response_body->data != NULL) 
        {
            ScriptContext *sctx = scripts_get_context();
            JSValueRef o = get_message_data(message);
            JSValueRef vals[] = { o, make_object_for_class(ctx, sctx->classes[CLASS_GOBJECT], G_OBJECT(message), true)  };
            scripts_call_as_function(ctx, function, function, 2, vals);
        }
        JSValueUnprotect(ctx, function);
        scripts_release_global_context();
    }
}
/** 
 * The webkit session
 *
 * @name session 
 * @memberOf net
 * @type SoupSession
 * @readonly
 *
 * */

static JSValueRef 
net_get_webkit_session(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return scripts_get_context()->session;
}

/** 
 * Sends a http-request
 * @name sendRequest
 * @memberOf net
 * @function
 *
 * @param {String} uri          
 *      The uri the request will be sent to.
 * @param {net~onResponse} callback   
 *      A callback that will be called when the request is finished
 * @param {String} [method]     The http request method, default GET
 * @param {Object} [data]       An object if method is POST
 * @param {String} data.contentType The content type
 * @param {String} data.data    The data that will be sent with the request
 *
 * @returns {Boolean}
 *      true if the request was sent
 * */
static JSValueRef 
net_send_request(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gboolean ret = -1;
    char *method = NULL, *uri = NULL;
    SoupMessage *msg;
    JSObjectRef function;
    if (argc < 2) 
        return JSValueMakeNumber(ctx, -1);

    uri = js_value_to_char(ctx, argv[0], -1, exc);
    if (uri == NULL) 
        return JSValueMakeNumber(ctx, -1);

    function = js_value_to_function(ctx, argv[1], exc);
    if (function == NULL)
        goto error_out;

    if (argc > 2) 
        method = js_value_to_char(ctx, argv[2], -1, exc);

    msg = soup_message_new(method == NULL ? "GET" : method, uri);
    if (msg == NULL)
        goto error_out;
    if (argc > 3 && method != NULL && !g_ascii_strcasecmp("POST", method)) 
        set_request(ctx, msg, argv[3], exc);

    JSValueProtect(ctx, function);
    soup_session_queue_message(webkit_get_default_session(), msg, (SoupSessionCallback)request_callback, function);
    ret = 0;

error_out: 
    g_free(uri);
    g_free(method);
    return JSValueMakeNumber(ctx, ret);
}/*}}}*/

/** 
 * Sends a http-request synchronously
 * @name sendRequestSync
 * @memberOf net
 * @function
 *
 * @param {String} uri          The uri the request will be sent to.
 * @param {String} [method]     The http request method, default GET
 *
 * @returns {Object}
 *      Object that contains the response body, the response headers and the
 *      http status code of the request.  
 * */
static JSValueRef 
net_send_request_sync(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *method = NULL, *uri = NULL;
    SoupMessage *msg;
    guint status;
    JSValueRef val;
    JSObjectRef o;
    JSStringRef js_key;
    JSValueRef js_value;

    if (argc < 1) 
        return NIL;

    uri = js_value_to_char(ctx, argv[0], -1, exc);
    if (uri == NULL) 
        return NIL;

    if (argc > 1) 
        method = js_value_to_char(ctx, argv[1], -1, exc);

    msg = soup_message_new(method == NULL ? "GET" : method, uri);
    if (argc > 2)
        set_request(ctx, msg, argv[2], exc);

    status = soup_session_send_message(webkit_get_default_session(), msg);
    val = get_message_data(msg);

    js_key = JSStringCreateWithUTF8CString("status");
    js_value = JSValueMakeNumber(ctx, status);

    o = JSValueToObject(ctx, val, exc);
    JSObjectSetProperty(ctx, o, js_key, js_value, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly, exc);

    JSStringRelease(js_key);
    return o;
}

/* net_domain_from_host {{{*/
/**
 * Gets the base domain name from a hostname where the base domain name is the
 * effective second level domain name, e.g. for www.example.com it will be
 * example.com, for www.example.co.uk it will be example.co.uk.
 *
 * @name domainFromHost 
 * @memberOf net
 * @function
 *
 * @param {String} hostname A hostname
 *
 * @returns {String} 
 *      The effective second level domain
 *
 * */
static JSValueRef 
net_domain_from_host(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("domainFromHost: missing argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    char *host = js_value_to_char(ctx, argv[0], -1, exc);
    const char *domain = domain_get_base_for_host(host);
    if (domain == NULL)
        return NIL;

    JSValueRef ret = js_char_to_value(ctx, domain);
    g_free(host);
    return ret;
}/*}}}*//*}}}*/

/**
 * Parses an uri to a {@link SoupUri} object
 *
 * @name parseUri 
 * @memberOf net
 * @function
 *
 * @param {String} uri The uri to parse
 *
 * @returns {@link SoupUri} 
 *      A parsed uri or null if the uri isn't valid according to RFC 3986.
 *
 * */
static JSValueRef 
net_parse_uri(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    if (argc > 0)
    {
        char *uri = js_value_to_char(ctx, argv[0], -1, NULL);
        if (uri != NULL)
        {
            SoupURI *suri = soup_uri_new(uri);
            if (suri != NULL)
            {
                ret = suri_to_object(ctx, suri, exc);
                soup_uri_free(suri);
            }
            g_free(uri);
        }
    }
    return ret;
}
/**
 * Gets all cookies from the cookie jar. 
 *
 * @name allCookies 
 * @memberOf net
 * @function
 * @since 1.5
 *
 * @returns {Array[{@link Cookie}]}
 *      An array of {@link Cookie|cookies}
 *
 * */
static JSValueRef 
net_all_cookies(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;
    GSList *cookies = dwb_soup_get_all_cookies();
    if (cookies != NULL)
    {
        guint l = g_slist_length(cookies), i=0;
        JSValueRef *args = g_malloc(l * sizeof (JSValueRef));
        for (GSList *l = cookies; l; l=l->next, i++)
        {
            args[i] = scripts_make_cookie(l->data);
        }
        ret = JSObjectMakeArray(ctx, i, args, exc);
        g_free(args);
        g_slist_free(cookies);
    }
    else 
        ret = JSObjectMakeArray(ctx, 0, NULL, exc);
    return ret;
}

JSObjectRef 
net_initialize(JSContextRef ctx) {
    /**
     * @namespace 
     *      Static object for network related tasks
     * @name net
     * @static
     * @example
     * //!javascript
     *
     * var net = namespace("net");
     * */

    JSObjectRef global_object = JSContextGetGlobalObject(ctx);

    JSStaticValue net_values[] = {
        { "session",          net_get_webkit_session, NULL,   kJSDefaultAttributes },
        { 0, 0, 0,  0 }, 
    };
    JSStaticFunction net_functions[] = { 
        { "sendRequest",      net_send_request,         kJSDefaultAttributes },
        { "sendRequestSync",  net_send_request_sync,         kJSDefaultAttributes },
        { "domainFromHost",   net_domain_from_host,         kJSDefaultAttributes },
        { "parseUri",         net_parse_uri,         kJSDefaultAttributes },
        { "allCookies",       net_all_cookies,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("net", net_functions, net_values, NULL);
    JSObjectRef ret =  scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "net", NULL);
    JSClassRelease(klass);
    return ret;
}
