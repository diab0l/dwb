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

#include "private.h"

static JSValueRef 
inject_api_callback(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc)
{
    JSObjectRef api_function = NULL;
    if (argc > 0 && (api_function = js_value_to_function(ctx, argv[0], exc)))
    {
        char *body = scripts_get_body(ctx, api_function, exc);
        if (body == NULL)
            return NULL; 
        if (argc > 1)
        {
            JSValueRef args [] = { js_context_change(ctx, ctx, argv[1], exc) };
            scripts_include(ctx, "local", body, false, false, 1, args, NULL);
        }
        else 
        {
            scripts_include(ctx, "local", body, false, false, 0, NULL, NULL);
        }
        g_free(body);
        
    }
    return NULL;
}

static void
load_string_resource(WebKitWebView *wv, WebKitWebFrame *frame, WebKitWebResource *resource, WebKitNetworkRequest *request, WebKitNetworkResponse *response, gpointer *data) 
{
    const char *uri = webkit_network_request_get_uri(request);
    if (!(g_str_has_prefix(uri, "dwb-chrome:") 
            || g_str_has_prefix(uri, "data:image/gif;base64,") 
            || g_str_has_prefix(uri, "data:image/png;base64,") 
            || g_str_has_prefix(uri, "data:image/jpeg;base64,") 
            || g_str_has_prefix(uri, "data:image/svg;base64,") )
            || webkit_web_view_get_main_frame(wv) != frame)
        webkit_network_request_set_uri(request, "about:blank");
}

static gboolean 
load_string_navigation(WebKitWebView *wv, WebKitWebFrame *frame, WebKitNetworkRequest *request, WebKitWebNavigationAction *action,
    WebKitWebPolicyDecision *policy, GList *gl) 
{
    if (frame == webkit_web_view_get_main_frame(wv))
    {
        const char *uri = webkit_network_request_get_uri(request);
        if (!g_str_has_prefix(uri, "dwb-chrome:"))
        {
            g_signal_handlers_disconnect_by_func(wv, load_string_navigation, NULL);
            g_signal_handlers_disconnect_by_func(wv, load_string_resource, NULL);
        }
    }
    return false;
}

static void
load_string_status(WebKitWebFrame *frame, GParamSpec *param, JSObjectRef deferred)
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) 
        return;
    WebKitLoadStatus status = webkit_web_frame_get_load_status(frame);
    gboolean is_main_frame = webkit_web_view_get_main_frame(webkit_web_frame_get_web_view(frame)) == frame;
    if (status == WEBKIT_LOAD_FINISHED)
        deferred_resolve(ctx, deferred, deferred, 0, NULL, NULL);
    else if (status == WEBKIT_LOAD_COMMITTED
            && is_main_frame
            && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(frame), "dwb_load_string_api")))
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(frame);
        JSStringRef api_name = JSStringCreateWithUTF8CString("dwb");
        JSObjectRef api_function = JSObjectMakeFunctionWithCallback(wctx, api_name, inject_api_callback);
        JSObjectSetProperty(wctx, JSContextGetGlobalObject(wctx), api_name, api_function, kJSDefaultAttributes, NULL);
        JSStringRelease(api_name);
    }
    else if (status == WEBKIT_LOAD_FAILED)
        deferred_reject(ctx, deferred, deferred, 0, NULL, NULL);
    if (status == WEBKIT_LOAD_FINISHED || status == WEBKIT_LOAD_FAILED)
    {
        g_signal_handlers_disconnect_by_func(frame, load_string_status, deferred);
        g_object_steal_data(G_OBJECT(frame), "dwb_load_string_api");
    }
    JSValueUnprotect(ctx, deferred);
    scripts_release_global_context();
}
/**
 * Loads a string in a frame or a webview
 *
 * @name loadString
 * @memberOf WebKitWebFrame.prototype
 * @function 
 *
 * @param {String} content
 *      The string to load
 * @param {String} [mimeType]
 *      The MIME-type, if omitted or null <i>text/html</i> is assumed.
 * @param {String} [encoding]
 *      The character encoding, if omitted or null <i>UTF-8</i> is assumed.
 * @param {String} [baseUri]
 *      The base uri, if present it must either use the uri-scheme <i>dwb-chrome:</i>
 *      or <i>file:</i>, otherwise the request will be ignored. 
 * @param {boolean} [externalSources]
 *      Whether external sources, e.g. scripts, are allowed, defaults to false. If
 *      external resources are forbidden the function <b>dwb</b> can be called
 *      in the webcontext to execute functions in the scripting context 
 *
 * @returns {Deferred}
 *      A deferred that will be resolved if the webview has finished loading the
 *      string and rejected if an error occured
 *
 * @since 1.3
 * */

static JSValueRef 
frame_load_string(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *content = NULL, 
         *mime_type = NULL, 
         *encoding = NULL, 
         *base_uri = NULL;

    if (argc == 0)
        return NIL;
    WebKitWebFrame *frame = JSObjectGetPrivate(this);
    g_return_val_if_fail(frame != NULL, NIL);

    JSObjectRef deferred = NULL;
    gboolean forbid_resources = true;
    gboolean create_api_function = false;
    WebKitWebView *wv = webkit_web_frame_get_web_view(frame);

    deferred = deferred_new(ctx);
    JSValueProtect(ctx, deferred);
    content = js_value_to_char(ctx, argv[0], -1, exc);
    if (content == NULL)
        return NULL;
    if (argc > 1)
    {
        mime_type = js_value_to_char(ctx, argv[1], -1, exc);
        if (argc > 2)
        {
            encoding = js_value_to_char(ctx, argv[2], -1, exc);
            if (argc > 3)
                base_uri = js_value_to_char(ctx, argv[3], -1, exc);
            if (argc > 4)
                forbid_resources = ! JSValueToBoolean(ctx, argv[4]);
        }
    }
    create_api_function = base_uri != NULL && g_str_has_prefix(base_uri, "dwb-chrome:") && forbid_resources;
    if (forbid_resources && create_api_function)
    {
        g_signal_connect(wv, "navigation-policy-decision-requested", G_CALLBACK(load_string_navigation), NULL);
        g_signal_connect(wv, "resource-request-starting", G_CALLBACK(load_string_resource), NULL);
    }
    g_object_set_data(G_OBJECT(frame), "dwb_load_string_api", GINT_TO_POINTER(create_api_function));

    webkit_web_frame_load_string(frame, content, mime_type, encoding, base_uri ? base_uri : "");
    g_signal_connect(frame, "notify::load-status", G_CALLBACK(load_string_status), deferred);

    g_free(content);
    g_free(mime_type);
    g_free(encoding);
    g_free(base_uri);
    return deferred ? deferred : NIL;
}
/*}}}*/

/* frame_get_domain {{{*/
/** 
 * The domain name of the frame which is the effective second level domain
 *
 * @name domain
 * @memberOf WebKitWebFrame.prototype
 * @type String
 *
 * */
static JSValueRef 
frame_get_domain(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(object);
    if (frame == NULL)
        return NIL;

    const char *domain = dwb_soup_get_domain(frame);
    if (domain == NULL)
        return NIL;
    return js_char_to_value(ctx, domain);
}/*}}}*/

/* frame_get_host {{{*/
/** 
 * The host name of the frame
 *
 * @name host
 * @memberOf WebKitWebFrame.prototype
 * @type String
 *
 * */
static JSValueRef 
frame_get_host(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(object);
    if (frame == NULL)
        return NIL;

    const char *host = dwb_soup_get_host(frame);
    if (host == NULL)
        return NIL;
    return js_char_to_value(ctx, host);
}/*}}}*/

/** 
 * The DOMDocument of the frame
 *
 * @name document
 * @memberOf WebKitWebFrame.prototype
 * @type DOMObject
 *
 * */
static JSValueRef 
frame_get_document(JSContextRef ctx, JSObjectRef self, JSStringRef js_name, JSValueRef* exception) {
    WebKitWebFrame *frame = JSObjectGetPrivate(self);
    if (frame == NULL)
        return NIL;
    WebKitDOMDocument *doc = webkit_web_frame_get_dom_document(frame);
    return scripts_make_object(ctx, G_OBJECT(doc));
    
}

/* frame_inject {{{*/

/**
 * Injects javascript code into a frame or webview 
 *
 * @name inject
 * @memberOf WebKitWebFrame.prototype
 * @function 
 * @example 
 * //!javascript
 * function injectable() {
 *    var text = exports.text;
 *    document.body.innerHTML = text;
 * }
 * signals.connect("documentLoaded", function(wv) {
 *    wv.inject(injectable, { text : "foo", number : 37 }, 3);
 * });
 *
 * @param {String|Function} code
 *      The script to inject, either a string or a function. If it is a function
 *      the body will be wrapped inside a new function.
 * @param {Object} arg
 *      If the script isn’t injected into the global scope the script is wrapped
 *      inside a function. arg then is accesible via arguments in the injected
 *      script, or by the variable <i>exports</i>, optional
 * @param {Number} [line]
 *      Starting line number, useful for debugging. If linenumber is greater
 *      than 0 error messages will be printed to stderr, optional.
 * @param {Boolean} [global]
 *      true to inject it into the global scope, false to encapsulate it in a
 *      function, default false
 * @returns {String}
 *      The return value of the script. If the script is injected globally
 *      inject always returns null. The return value is always converted to a
 *      string. To return objects call JSON.parse on the return value.
 * */
static JSValueRef 
frame_inject(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(this);
    if (frame != NULL) 
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(frame);
        return inject(ctx, wctx, function, this, argc, argv, exc);
    }
    return NIL;
}/*}}}*/

void 
frame_initialize(ScriptContext *sctx) {
    /** 
     * Represents a frame or an iframe
     *
     * @name WebKitWebFrame
     * @augments GObject
     * @class 
     *      Represents a frame or iframe. Due to same origin policy it
     *      is not possible to inject scripts from a WebKitWebView into iframes with a
     *      different domain. For this purpose the frame object can be used.
     * */
    JSStaticFunction frame_functions[] = { 
        { "inject",          frame_inject,             kJSDefaultAttributes },
        { "loadString",      frame_load_string,        kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };
    JSStaticValue frame_values[] = {
        { "host", frame_get_host, NULL, kJSDefaultAttributes }, 
        { "document", frame_get_document, NULL, kJSDefaultAttributes }, 
        { "domain", frame_get_domain, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };

    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitWebFrame";
    cd.staticFunctions = frame_functions;
    cd.staticValues = frame_values;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_FRAME] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_FRAME] = scripts_create_constructor(sctx->global_context, "WebKitWebFrame", sctx->classes[CLASS_FRAME], NULL, NULL);
}
