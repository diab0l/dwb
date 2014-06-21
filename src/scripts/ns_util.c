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

#define IS_KEY_EVENT(X) (((int)(X)) == GDK_KEY_PRESS || ((int)(X)) == GDK_KEY_RELEASE)
#define IS_BUTTON_EVENT(X) (((int)(X)) == GDK_BUTTON_PRESS || ((int)(X)) == GDK_BUTTON_RELEASE \
                            || ((int)(X)) == GDK_2BUTTON_PRESS || ((int)(X)) == GDK_3BUTTON_PRESS)

typedef struct PathCallback_s 
{
    JSObjectRef callback; 
    gboolean dir_only;
} PathCallback;

static PathCallback * 
path_callback_new(JSContextRef ctx, JSObjectRef object, gboolean dir_only)
{
    g_return_val_if_fail(object != NULL, NULL);
    PathCallback *pc = g_malloc(sizeof(PathCallback));
    pc->callback = object;
    JSValueProtect(ctx, object);
    pc->dir_only = dir_only;
    return pc;
}
static void
path_callback_free(PathCallback *pc)
{
    g_return_if_fail(pc != NULL);

    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSValueUnprotect(ctx, pc->callback);
    }
    scripts_release_global_context();

    g_free(pc);
}
/**
 * Callback that will be called when <i>Return</i> or <i>Escape</i> was pressed after {@link util.pathComplete} was invoked.
 *
 * @callback util~onPathComplete 
 *
 * @param {String} path The path or <i>null</i> if <i>Escape</i> was pressed.
 *
 * @since 1.3
 * */
static gboolean 
path_completion_callback(GtkEntry *entry, GdkEventKey *e, PathCallback *pc)
{
    gboolean evaluate = false, clear = false;
    if (e->keyval == GDK_KEY_Escape)
    {
        clear = true;
        goto finish;
    }
    else if (IS_RETURN_KEY(e))
    {
        evaluate = clear = true;
        goto finish;
    }
    else if (DWB_TAB_KEY(e))
    {
        completion_complete_path(e->state & GDK_SHIFT_MASK, pc->dir_only);
        return true;
    }
    else if (dwb_eval_override_key(e, CP_OVERRIDE_ENTRY))
        return true;
finish: 
    completion_clean_path_completion();
    if (clear)
    {
        JSValueRef argv[1];
        JSContextRef ctx = scripts_get_global_context();
        if (ctx != NULL)
        {
            if (evaluate)
            {
                const char *text = GET_TEXT();
                argv[0] = js_char_to_value(ctx, text);
            }
            else 
                argv[0] = NIL;

            scripts_call_as_function(ctx, pc->callback, pc->callback, 1, argv);
            scripts_release_global_context();
        }
        entry_snoop_end(G_CALLBACK(path_completion_callback), pc);
        path_callback_free(pc);
    }
    return false;
}

/** 
 * Initializes tab completion.
 * @name tabComplete
 * @memberOf util
 * @function
 * 
 * @param {String} label 
 *      The command line label
 * @param {Array[Object]} items[] 
 *      An array of labels, 
 * @param {String} items[].left
 *      Left completion label
 * @param {String} items[].right
 *      Right completion label
 * @param {util~onTabComplete} callback Callback function, the first argument will be the
 *                            returned string from the url bar.
 * @param {Boolean} [readonly] Whether the items are readonly, default false 
 *
 * */

static JSValueRef 
sutil_tab_complete(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    ScriptContext *s_ctx = scripts_get_context();

    if (argc < 3 || !JSValueIsInstanceOfConstructor(ctx, argv[1], s_ctx->constructors[CONSTRUCTOR_ARRAY], exc)) 
    {
        js_make_exception(ctx, exc, EXCEPTION("tabComplete: invalid argument."));
        return NULL;
    }
    s_ctx->complete = js_value_to_function(ctx, argv[2], exc);
    if (s_ctx->complete == NULL)
        return NULL;

    dwb.state.script_comp_readonly = false;
    if (argc > 3 && JSValueIsBoolean(ctx, argv[3])) 
    {
        dwb.state.script_comp_readonly = JSValueToBoolean(ctx, argv[3]);
    }

    char *left, *right, *label;
    js_array_iterator iter;
    JSValueRef val;
    JSObjectRef cur = NULL;
    Navigation *n;

    label = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);
    JSObjectRef o = JSValueToObject(ctx, argv[1], exc);
    js_array_iterator_init(ctx, &iter, o);
    while((val = js_array_iterator_next(&iter, exc))) 
    {
        cur = JSValueToObject(ctx, val, exc);
        if (cur == NULL)
            goto error_out;
        left = js_get_string_property(ctx, cur, "left");
        right = js_get_string_property(ctx, cur, "right");
        n = g_malloc(sizeof(Navigation));
        n->first = left; 
        n->second = right;
        dwb.state.script_completion = g_list_prepend(dwb.state.script_completion, n);
    }
    dwb.state.script_completion = g_list_reverse(dwb.state.script_completion);
    dwb_set_status_bar_text(dwb.gui.lstatus, label, NULL, NULL, true);

    entry_focus();
    completion_complete(COMP_SCRIPT, false);

error_out:
    js_array_iterator_finish(&iter);

    for (GList *l = dwb.state.script_completion; l; l=l->next) 
    {
        n = l->data;
        g_free(n->first); 
        g_free(n->second);
        g_free(n);
    }
    g_free(label);
    g_list_free(dwb.state.script_completion);
    dwb.state.script_completion = NULL;
    return NULL;
}
/**
 * Escapes text for usage with pango markup
 *
 * @name markupEscape 
 * @memberOf util 
 * @function
 *
 * @param {String} text The text to escape
 *
 * @returns {String} The escaped text or null
 *
 * */
static JSValueRef 
sutil_markup_escape(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *string = NULL, *escaped = NULL;
    if (argc > 0) 
    {
        string = js_value_to_char(ctx, argv[0], -1, exc);
        if (string != NULL) 
        {
            escaped = g_markup_escape_text(string, -1);
            g_free(string);
            if (escaped != NULL) 
            {
                JSValueRef ret = js_char_to_value(ctx, escaped);
                g_free(escaped);
                return ret;
            }
        }
    }
    return JSValueMakeNull(ctx);
}

/** 
 * Computes a checksum of a string
 * @name checksum 
 * @memberOf util
 * @function
 *
 * @param {String} data The data 
 * @param {ChecksumType} [type] The {@link Enums and Flags.ChecksumType|ChecksumType}, defaults to sha256
 * @returns {Boolean} Whether the shortcut was unbound
 *
 * */
static JSValueRef 
sutil_checksum(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *checksum = NULL;
    guchar *original = NULL;
    JSValueRef ret;

    if (argc < 1) 
        return JSValueMakeNull(ctx);

    original = (guchar*)js_value_to_char(ctx, argv[0], -1, exc);
    if (original == NULL)
        return NIL;

    double dtype;
    GChecksumType type = G_CHECKSUM_SHA256;
    if (argc > 1) 
    {
        dtype = JSValueToNumber(ctx, argv[1], exc);
        if (isnan(dtype)) 
        {
            ret = NIL;
            goto error_out;
        }
        type = MIN(MAX((GChecksumType)dtype, G_CHECKSUM_MD5), G_CHECKSUM_SHA256);
    }
    checksum = g_compute_checksum_for_data(type, original, -1);

    ret = js_char_to_value(ctx, checksum);

error_out:
    g_free(original);
    g_free(checksum);
    return ret;
}/*}}}*/


/** 
 * Change the mode, changeable modes are Modes.NormalMode, Modes.InsertMode and
 * Modes.CaretMode
 *
 * @name changeMode
 * @memberOf util
 * @function
 *
 * @param {Modes} mode The new mode
 * */

static JSValueRef 
sutil_change_mode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return NULL;
    double mode = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(mode))
    {
        if ((int) mode == NORMAL_MODE)
        {
            dwb_change_mode(NORMAL_MODE, true);
        }
        else if ((int)mode & (INSERT_MODE | CARET_MODE))
        {
            dwb_change_mode((int)mode);
        }
    }
    return NULL;
}

/**
 * Gets the current mode 
 *
 * @name getMode 
 * @memberOf util 
 * @function 
 *
 * @returns {Enums and Flags.Modes} The current mode
 * */
static JSValueRef 
sutil_get_mode(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return JSValueMakeNumber(ctx, BASIC_MODES(dwb.state.mode));
}

/** 
 * Gets the body of a function, useful for injecting scripts
 *
 * @name getBody
 * @memberOf util
 * @function 
 * 
 * @param {Function} function A function
 *
 * @returns {String} The body of the function
 * */
static JSValueRef 
sutil_get_body(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) 
        return JSValueMakeNull(ctx);

    JSValueRef ret;
    JSObjectRef func = js_value_to_function(ctx, argv[0], exc);
    char *body = scripts_get_body(ctx, func, exc);

    if (body == NULL)
        return NIL;

    ret = js_char_to_value(ctx, body);
    g_free(body);
    return ret;
}

/** 
 * Dispatches a keyboard or button event
 *
 * @name dispatchEvent
 * @memberOf util
 * @function 
 * 
 * @param {Object} event 
 *      Event details, see {@link signals~onButtonPress|buttonPress}, 
 *      {@link signals~onButtonRelease|buttonRelease}, 
 *      {@link signals~onKeyPress|keyPress} or 
 *      {@link signals~onKeyRelease|keyRelease} 
 *      for details.
 * @param {Number} event.type
 *      Type of the event, can be either buttonpress (4), doubleclick (5),
 *      tripleclick (6), buttonrelease (7), keypress (8) or keyrelease
 *      (9).
 *
 * @returns {Boolean} Whether the event was dispatched
 * */
static JSValueRef 
sutil_dispatch_event(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gboolean result = false;
    if (argc < 1)
        return JSValueMakeBoolean(ctx, false);
    double type = js_val_get_double_property(ctx, argv[0], "type", exc);
    if (isnan(type) || (!(IS_KEY_EVENT(type)) && !(IS_BUTTON_EVENT(type))))
        return JSValueMakeBoolean(ctx, false);

    double state = js_val_get_double_property(ctx, argv[0], "state", exc);
    if (isnan(state))
        state = 0;
    GdkEvent *event = gdk_event_new(type);
    if (IS_KEY_EVENT(type)) 
    {
        double keyval = js_val_get_double_property(ctx, argv[0], "keyVal", exc);
        if (isnan(keyval))
            goto error_out;
        event->key.window = g_object_ref(gtk_widget_get_window(dwb.gui.window));
        event->key.keyval = keyval;
        event->key.state = state;
        GdkKeymapKey *key; 
        gint n;
        if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_default(), keyval, &key, &n))
        {
            event->key.hardware_keycode = key[0].keycode;
            g_free(key);
        }
        gtk_main_do_event(event);
        result = true;
    }
    else if (IS_BUTTON_EVENT(type))
    {
        double button = js_val_get_double_property(ctx, argv[0], "button", exc);
        if (isnan(button))
            goto error_out;

        event->button.button = button;
        event->button.window = g_object_ref(gtk_widget_get_window(VIEW(dwb.state.fview)->web));
        event->button.state = state;

        double x = js_val_get_double_property(ctx, argv[0], "x", exc);
        event->button.x = isnan(x) ? 0 : x;

        double y = js_val_get_double_property(ctx, argv[0], "y", exc);
        event->button.y = isnan(y) ? 0 : y;
        
        double x_root = js_val_get_double_property(ctx, argv[0], "xRoot", exc);
        double y_root = js_val_get_double_property(ctx, argv[0], "yRoot", exc);
        if (isnan(x_root)  || isnan(y_root))
        {
            GdkDisplay *dpy = gdk_display_open(NULL);
            int cx, cy;
            gdk_display_get_pointer(dpy, NULL, &cx, &cy, NULL);
            if (isnan(x_root))
                x_root = cx;
            if (isnan(y_root))
                y_root = cy;
            gdk_display_close(dpy);
        }
        event->button.x_root = x_root;
        event->button.y_root = y_root;

        double time = js_val_get_double_property(ctx, argv[0], "time", exc);
        event->button.time = isnan(time) ? 0 : time;
        gtk_main_do_event(event);
        result = true;
    }
error_out:
    gdk_event_free(event);
    return JSValueMakeBoolean(ctx, result);
}

/** 
 * Initializes filename completion.
 * @name pathComplete
 * @memberOf util
 * @function
 * 
 * @param {util~onPathComplete} callback 
 *       Callback function called when a path was chosen or escape was pressed
 * @param {String} [label]
 *      The command line label
 * @param {String} [initialPath]
 *      The initial path, defaults to the current working directory
 * @param {Boolean} [dirOnly]
 *      Whether to complete only directories, default false.
 *
 * @since 1.3
 * */
static JSValueRef 
sutil_path_complete(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)
{
    char *status_text = NULL, 
         *initial_path = NULL;
    gboolean dir_only = false;

    if (argc == 0)
        return NULL;

    JSObjectRef callback = js_value_to_function(ctx, argv[0], exc);
    if (callback == NULL)
        return NULL;

    if (argc > 1)
    {
        status_text = js_value_to_char(ctx, argv[1], -1, exc);
        if (status_text != NULL)
        {
            dwb_set_status_bar_text(dwb.gui.lstatus, status_text, NULL, NULL, true);
        }
    }
    if (argc > 2)
        initial_path = js_value_to_char(ctx, argv[2], -1, exc);

    if (argc > 3)
    {
        dir_only = JSValueToBoolean(ctx, argv[3]);
    }
    
    PathCallback *pc = path_callback_new(ctx, callback, dir_only);

    if (initial_path == NULL)
    {
        initial_path = g_get_current_dir();
    }
    entry_snoop(G_CALLBACK(path_completion_callback), pc);
    entry_set_text(initial_path ? initial_path : "/");

    g_free(status_text);
    g_free(initial_path);
    return NULL;
}

/** 
 * For internal usage only 
 *
 * @name _base64Encode
 * @memberOf util
 * @function
 * @private
 * */
static JSValueRef 
sutil_base64_encode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSStringRef string;
    gsize length;
    const JSChar *chars;
    guchar *data; 
    char *base64; 
    JSValueRef ret;

    if (argc == 0)
        return NIL;
    string = JSValueToStringCopy(ctx, argv[0], exc);
    if (string == NULL)
        return NIL;

    length = JSStringGetLength(string);
    chars = JSStringGetCharactersPtr(string);
    data = g_malloc0(length * sizeof(guchar));

    for (guint i=0; i<length; i++)
    {
        data[i] = chars[i] & 0xff;
    }
    base64 = g_base64_encode(data, length);
    ret = js_char_to_value(ctx, base64);

    g_free(base64);
    g_free(data);
    JSStringRelease(string);

    return ret;
}

/** 
 * For internal usage only 
 *
 * @name _base64Decode
 * @memberOf util
 * @function
 * @private
 * */
static JSValueRef 
sutil_base64_decode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gsize length;
    gchar *base64; 
    guchar *data;
    gushort *js_data;
    JSStringRef string;
    JSValueRef ret;

    if (argc == 0)
        return NIL;
    base64 = js_value_to_char(ctx, argv[0], -1, exc);
    if (base64 == NULL)
        return NIL;
    data = g_base64_decode(base64, &length);
    js_data = g_malloc0(length * sizeof(gushort));
    for (guint i=0; i<length; i++)
    {
        js_data[i] = data[i];
    }
    string = JSStringCreateWithCharacters(js_data, length);
    ret = JSValueMakeString(ctx, string);

    g_free(base64);
    g_free(data);
    g_free(js_data);
    JSStringRelease(string);

    return ret;
}

JSObjectRef 
util_initialize(JSContextRef ctx) {
    /**
     * Utility functions
     *
     * @namespace 
     *      Miscellaneous utility functions
     * @name util 
     * @static 
     * @example
     * //!javascript
     *
     * var util = namespace("util");
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSStaticFunction util_functions[] = { 
        { "markupEscape",     sutil_markup_escape,         kJSDefaultAttributes },
        { "getMode",          sutil_get_mode,         kJSDefaultAttributes },
        { "getBody",          sutil_get_body,         kJSDefaultAttributes },
        { "dispatchEvent",    sutil_dispatch_event,         kJSDefaultAttributes },
        { "tabComplete",      sutil_tab_complete,         kJSDefaultAttributes },
        { "pathComplete",     sutil_path_complete,    kJSDefaultAttributes },
        { "checksum",         sutil_checksum,         kJSDefaultAttributes },
        { "changeMode",       sutil_change_mode,      kJSDefaultAttributes }, 
        { "_base64Encode",    sutil_base64_encode,    kJSDefaultAttributes },
        { "_base64Decode",    sutil_base64_decode,    kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("util", util_functions, NULL, NULL);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "util", NULL);
    JSClassRelease(klass);
    return ret;
}

