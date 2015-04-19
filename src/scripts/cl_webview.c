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

static GList *
find_webview(JSObjectRef o) 
{
    if (dwb.state.fview == NULL) {
        return NULL;
    }

    for (GList *r = dwb.state.fview; r; r=r->next)
        if (VIEW(r)->script_wv == o)
            return r;
    for (GList *r = dwb.state.fview->prev; r; r=r->prev)
        if (VIEW(r)->script_wv == o)
            return r;
    return NULL;
}
/* wv_status_cb {{{*/
/** 
 * Callback that will be called if the load-status changes, return true to stop
 * the emission
 *
 * @callback WebKitWebView#loadUriCallback 
 * @param {WebKitWebView} wv The webview which loaded the uri
 * */
static gboolean 
wv_status_cb(CallbackData *c) 
{
    WebKitLoadStatus status = webkit_web_view_get_load_status(WEBKIT_WEB_VIEW(c->gobject));
    if (status == WEBKIT_LOAD_FINISHED || status == WEBKIT_LOAD_FAILED) 
        return true;
    return false;
}/*}}}*/

/* wv_load_uri {{{*/
/**
 * Load an uri in a webview
 * @name loadUri
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * @param {String} uri 
 *      The uri to load
 * @param {WebKitWebView#loadUriCallback} [callback] 
 *      A callback function that will be called when the load status changes,
 *      return true to stop the emission
 *
 * @returns {Boolean}
 *      true if the uri was loaded
 * */
static JSValueRef 
wv_load_uri(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) 
        return JSValueMakeBoolean(ctx, false);

    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL) 
    {
        char *uri = js_value_to_char(ctx, argv[0], -1, exc);
        if (uri == NULL)
            return false;
        webkit_web_view_load_uri(wv, uri);
        g_free(uri);
        if (argc > 1)  
            make_callback(ctx, this, G_OBJECT(wv), "notify::load-status", argv[1], wv_status_cb, exc);

        return JSValueMakeBoolean(ctx, true);
    }
    return JSValueMakeBoolean(ctx, false);
}/*}}}*/

/**
 * Stops any ongoing loading
 *
 * @name stopLoading
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * */
static JSValueRef 
wv_stop_loading(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);;
    if (wv != NULL)
        webkit_web_view_stop_loading(wv);
    return NULL;
}

/**
 * Loads a history item, can be used to navigate forward/backwards in history
 *
 * @name history
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * @param {Number} steps 
 *      Number of steps, pass a negative value to go back in history
 * */
/* wv_history {{{*/
static JSValueRef 
wv_history(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("webview.history: missing argument."));
        return NULL;
    }
    double steps = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(steps)) {
        WebKitWebView *wv = JSObjectGetPrivate(this);
        if (wv != NULL)
            webkit_web_view_go_back_or_forward(wv, (int)steps);
    }
    return NULL;
}/*}}}*/

/**
 * Reloads the current site
 *
 * @name reload
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * */
/* wv_reload {{{*/
static JSValueRef 
wv_reload(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL)
        webkit_web_view_reload(wv);
    return NULL;
}/*}}}*/

/* wv_inject {{{*/
static JSValueRef 
wv_inject(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL) 
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(webkit_web_view_get_main_frame(wv));
        return inject(ctx, wctx, function, this, argc, argv, exc);
    }
    return NIL;
}/*}}}*/
#if WEBKIT_CHECK_VERSION(1, 10, 0) && CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)

cairo_surface_t *
wv_to_surface(JSContextRef ctx, WebKitWebView *wv, unsigned int argc, const JSValueRef *argv, JSValueRef *exc)
{
    cairo_surface_t *sf, *scaled_surface = NULL; 
    cairo_t *cr;
    int w, h; 
    gboolean keep_aspect = false;
    double aspect, new_width, new_height, width, height;
    double sw, sh;

    if (argc > 1)
    {
        width = JSValueToNumber(ctx, argv[0], exc);
        height = JSValueToNumber(ctx, argv[1], exc);
        if (!isnan(width) && !isnan(height)) 
        {
            if (argc > 2 && JSValueIsBoolean(ctx, argv[2])) 
                keep_aspect = JSValueToBoolean(ctx, argv[2]);

            if (keep_aspect && (width <= 0 || height <= 0))
                return NULL;

            sf = webkit_web_view_get_snapshot(wv);
            w = cairo_image_surface_get_width(sf);
            h = cairo_image_surface_get_height(sf);

            aspect = (double)w/h;
            new_width = width;
            new_height = height;

            if (width <= 0 || keep_aspect)
                new_width = height * aspect;
            if ((width > 0 && height <= 0) || keep_aspect)
                new_height = width / aspect;
            if (keep_aspect) 
            {
                if (new_width > width) 
                {
                    new_width = width;
                    new_height = new_width / aspect;
                }
                else if (new_height > height) 
                {
                    new_height = height;
                    new_width = new_height * aspect;
                }
            }

            if (width <= 0 || height <= 0)
                sw = sh = MIN(width / w, height / h);
            else 
            {
                sw = width / w;
                sh = height / h;
            }

            scaled_surface = cairo_surface_create_similar_image(sf, CAIRO_FORMAT_RGB24, new_width, new_height);
            cr = cairo_create(scaled_surface);

            cairo_save(cr);
            cairo_scale(cr, sw, sh);

            cairo_set_source_surface(cr, sf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

            cairo_destroy(cr);
            cairo_surface_destroy(sf);
        }
    }
    else 
    {
        scaled_surface = webkit_web_view_get_snapshot(wv);
    }
    return scaled_surface;
}

cairo_status_t 
write_png64(GString *buffer, const unsigned char *data, unsigned int length)
{
    char *base64 = g_base64_encode(data, length);
    if (base64 != NULL)
    {
        g_string_append(buffer, base64);
        g_free(base64);
        return CAIRO_STATUS_SUCCESS;
    }
    return CAIRO_STATUS_WRITE_ERROR;
}
/** 
 * Renders a webview to a base64 encoded png
 *
 * @name toPng64
 * @memberOf WebKitWebView.prototype
 * @function 
 * @type String
 * @requires webkitgtk >= 1.10
 * @example
 * var png = tabs.current.toPng64(250, 250, true); 
 * tabs.current.inject(function() {
 *     var img = document.createElement("img");
 *     img.src = "data:image/png;base64," + arguments[0];
 *     document.body.appendChild(img);
 * }, png);
 *
 *
 * @param {Number} width
 *      The width of the png, if width is < 0 and height is > 0 the image will have the same aspect ratio as the original webview, optional.
 * @param {Number} height
 *      The height of the png, if height is < 0 and width is > 0 the image will have the same aspect ratio as the original webview, optional, mandatory if width is set.
 * @param {Boolean} keepAspect
 *      Whether to keep the ascpect ratio, if set to true the new image will have the same aspect ratio as the original webview, width and height are taken as maximum sizes and must both be > 0, optional.
 *
 * @returns A base64 encoded png-String
 *
 * */
static JSValueRef 
wv_to_png64(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv;
    cairo_status_t status = -1;
    cairo_surface_t *scaled_surface;
    JSValueRef result = NIL;

    wv = JSObjectGetPrivate(this);
    g_return_val_if_fail(wv != NULL, NIL);

    scaled_surface = wv_to_surface(ctx, wv, argc, argv, exc);
    if (scaled_surface != NULL)
    {
        GString *s = g_string_new(NULL);
        status = cairo_surface_write_to_png_stream(scaled_surface, (cairo_write_func_t)write_png64, s);
        cairo_surface_destroy(scaled_surface);

        if (status == CAIRO_STATUS_SUCCESS)
            result = js_char_to_value(ctx, s->str);
        g_string_free(s, true);
    }
    return result;
}
/** 
 * Renders a webview to a png file
 *
 * @name toPng
 * @memberOf WebKitWebView.prototype
 * @function 
 * @type Number
 * @requires webkitgtk >= 1.10
 *
 *
 * @param {String} filename
 *      The filename for the png.
 * @param {Number} width
 *      The width of the png, if width is < 0 and height is > 0 the image will have the same aspect ratio as the original webview, optional.
 * @param {Number} height
 *      The height of the png, if height is < 0 and width is > 0 the image will have the same aspect ratio as the original webview, optional, mandatory if width is set.
 * @param {Boolean} keepAspect
 *      Whether to keep the ascpect ratio, if set to true the new image will have the same aspect ratio as the original webview, width and height are taken as maximum sizes and must both be > 0, optional.
 *
 * @returns A cairo_status_t, 0 on success, -1 if an error occured
 * */
static JSValueRef 
wv_to_png(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv;
    cairo_status_t status = -1;
    cairo_surface_t *scaled_surface;
    char *filename;
    if (argc < 1 || (wv = JSObjectGetPrivate(this)) == NULL || (JSValueIsNull(ctx, argv[0])) || (filename = js_value_to_char(ctx, argv[0], -1, NULL)) == NULL) 
    {
        return JSValueMakeNumber(ctx, status);
    }
    scaled_surface = wv_to_surface(ctx, wv, argc-1, argc > 1 ? argv + 1 : NULL, exc);
    if (scaled_surface != NULL)
    {
        status = cairo_surface_write_to_png(scaled_surface, filename);
        cairo_surface_destroy(scaled_surface);
    }
    else 
    {
        return JSValueMakeNumber(ctx, -1);
    }
    return JSValueMakeNumber(ctx, status);
}
#endif
/** 
 * Whether text is selected in the webview
 *
 * @name hasSelection
 * @memberOf WebKitWebView.prototype
 * @type Boolean
 * */
static JSValueRef 
wv_has_selection(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv != NULL) 
    {
        return JSValueMakeBoolean(ctx, webkit_web_view_has_selection(wv));
    }
    return JSValueMakeBoolean(ctx, false);
}/*}}}*/
/** 
 * The last search string of the webview
 *
 * @name lastSearch
 * @memberOf WebKitWebView.prototype
 * @type String
 * */
static JSValueRef 
wv_last_search(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl != NULL) {
        if (VIEW(gl)->status->search_string != NULL) {
            JSValueRef val = js_char_to_value(ctx, VIEW(gl)->status->search_string);
            return val;
        }
    }
    return NIL;
}/*}}}*/
/** 
 * Whether the webview has already loaded the first site. loadDeferred can only
 * be false if 'load-on-focus' is set to true
 *
 * @name loadDeferred
 * @memberOf WebKitWebView.prototype
 * @type Boolean
 * */
static JSValueRef 
wv_load_deferred(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) {
    GList *gl = find_webview(object);
    gboolean deferred = false;
    if (gl != NULL) {
        deferred = VIEW(gl)->status->deferred;
    }
    return JSValueMakeBoolean(ctx, deferred);
}
/* wv_get_main_frame {{{*/
/** 
 * The main frame
 *
 * @name mainFrame
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebFrame
 * */
static JSValueRef 
wv_get_main_frame(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv != NULL) 
    {
        WebKitWebFrame *frame = webkit_web_view_get_main_frame(wv);
        return scripts_make_object(ctx, G_OBJECT(frame));
    }
    return NIL;
}/*}}}*/

/** 
 * The focused frame
 *
 * @name focusedFrame
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebFrame
 * */
/* wv_get_focused_frame {{{*/
static JSValueRef 
wv_get_focused_frame(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv != NULL) 
    {
        WebKitWebFrame *frame = webkit_web_view_get_focused_frame(wv);
        return scripts_make_object(ctx, G_OBJECT(frame));
    }
    return NIL;
}/*}}}*/

/* wv_get_all_frames {{{*/
/** 
 * All frames of a webview, including the main frame
 *
 * @name allFrames
 * @memberOf WebKitWebView.prototype
 * @type Array[{@link WebKitWebFrame}]
 * */
static JSValueRef 
wv_get_all_frames(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    JSValueRef ret = NIL;
    int argc = 0, n = 0;
    GSList *frames = NULL;
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    for (GSList *l = VIEW(gl)->status->frames; l; l=l->next) {
        WebKitWebFrame *frame = g_weak_ref_get(l->data);
        if (frame != NULL) {
            frames = g_slist_append(frames, frame);
            argc++;
        }
    }

    if (argc > 0) {
        JSValueRef argv[argc];

        for (GSList *sl = frames; sl; sl=sl->next) {
            WebKitWebFrame *frame = sl->data;
            if (frame != NULL) {
                argv[n++] = scripts_make_object(ctx, G_OBJECT(sl->data));
            }
        }
        ret = JSObjectMakeArray(ctx, argc, argv, exception);
    }
    g_slist_free_full(frames, (GDestroyNotify)g_object_unref);

    return ret;
}/*}}}*/

/** 
 * The tabnumber of the webview, starting at 0
 * 
 * @name number
 * @memberOf WebKitWebView.prototype
 * @type Number
 *
 * */
/* wv_get_number {{{*/
static JSValueRef 
wv_get_number(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = dwb.state.views;
    for (int i=0; gl; i++, gl=gl->next) 
    {
        if (object == VIEW(gl)->script_wv) 
            return JSValueMakeNumber(ctx, i); 
    }
    return JSValueMakeNumber(ctx, -1); 
}/*}}}*/

/** 
 * The main widget for tab labels, used for coloring tabs, child of gui.tabBox.
 *
 * @name tabWidget
 * @memberOf WebKitWebView.prototype
 * @type GtkEventBox
 * */
static JSValueRef 
wv_get_tab_widget(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, CLASS_SECURE_WIDGET, G_OBJECT(VIEW(gl)->tabevent), true);
}
/** 
 * Horizontal box, child of wv.tabWidget.
 *
 * @name tabBox
 * @memberOf WebKitWebView.prototype
 * @type GtkBox
 * */
static JSValueRef 
wv_get_tab_box(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, CLASS_SECURE_WIDGET, G_OBJECT(VIEW(gl)->tabbox), true);
}
/** 
 * Text label of a tab, child of wv.tabBox.
 *
 * @name tabLabel
 * @memberOf WebKitWebView.prototype
 * @type GtkLabel
 * */
static JSValueRef 
wv_get_tab_label(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, CLASS_SECURE_WIDGET, G_OBJECT(VIEW(gl)->tablabel), true);
}
/** 
 * Favicon widget, child of wv.tabBox
 *
 * @name tabIcon
 * @memberOf WebKitWebView.prototype
 * @type GtkImage
 * */
static JSValueRef 
wv_get_tab_icon(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, CLASS_SECURE_WIDGET, G_OBJECT(VIEW(gl)->tabicon), true);
}

/** 
 * The parent widget of every webview, it is used for scrolling the webview
 *
 * @name scrolledWindow
 * @memberOf WebKitWebView.prototype
 * @type GtkScrolledWindow
 * */
static JSValueRef 
wv_get_scrolled_window(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, CLASS_SECURE_WIDGET, G_OBJECT(VIEW(gl)->scroll), true);
}

/** 
 * The history of the webview
 *
 * @name historyList
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebBackForwardList
 * */
static JSValueRef 
wv_get_history_list(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv == NULL)
        return NIL;
    return scripts_make_object(ctx, G_OBJECT(webkit_web_view_get_back_forward_list(wv)));
}


void 
webview_initialize(ScriptContext *sctx) {
    /**
     * A GtkWidget that shows the webcontent
     *
     * @name WebKitWebView
     * @augments GObject
     * @class GtkWidget that shows webcontent
     * @borrows WebKitWebFrame#inject as prototype.inject
     * @borrows WebKitWebFrame#loadString as prototype.loadString
     * @borrows WebKitWebFrame#document as prototype.document
     * */
    JSStaticFunction wv_functions[] = { 
        { "loadUri",         wv_load_uri,             kJSDefaultAttributes },
        { "stopLoading",         wv_stop_loading,        kJSDefaultAttributes },
        { "history",         wv_history,             kJSDefaultAttributes },
        { "reload",          wv_reload,             kJSDefaultAttributes },
        { "inject",          wv_inject,             kJSDefaultAttributes },
#if WEBKIT_CHECK_VERSION(1, 10, 0) && CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)
        { "toPng",           wv_to_png,             kJSDefaultAttributes },
        { "toPng64",         wv_to_png64,             kJSDefaultAttributes },
#endif
        { 0, 0, 0 }, 
    };
    JSStaticValue wv_values[] = {
        { "loadDeferred",  wv_load_deferred, NULL, kJSDefaultAttributes }, 
        { "lastSearch",    wv_last_search, NULL, kJSDefaultAttributes }, 
        { "hasSelection",  wv_has_selection, NULL, kJSDefaultAttributes }, 
        { "mainFrame",     wv_get_main_frame, NULL, kJSDefaultAttributes }, 
        { "focusedFrame",  wv_get_focused_frame, NULL, kJSDefaultAttributes }, 
        { "allFrames",     wv_get_all_frames, NULL, kJSDefaultAttributes }, 
        { "number",        wv_get_number, NULL, kJSDefaultAttributes }, 
        { "tabWidget",     wv_get_tab_widget, NULL, kJSDefaultAttributes }, 
        { "tabBox",        wv_get_tab_box, NULL, kJSDefaultAttributes }, 
        { "tabLabel",      wv_get_tab_label, NULL, kJSDefaultAttributes }, 
        { "tabIcon",       wv_get_tab_icon, NULL, kJSDefaultAttributes }, 
        { "historyList",    wv_get_history_list, NULL, kJSDefaultAttributes }, 
        { "scrolledWindow",wv_get_scrolled_window, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };

    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitWebView";
    cd.staticFunctions = wv_functions;
    cd.staticValues = wv_values;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_WEBVIEW] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_WEBVIEW] = scripts_create_constructor(sctx->global_context, "WebKitWebView", sctx->classes[CLASS_WEBVIEW], NULL, NULL);

}
