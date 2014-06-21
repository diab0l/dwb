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

enum {
    SELECTION_PRIMARY = 1,
    SELECTION_CLIPBOARD = 2
};


static GdkAtom 
atom_from_jsvalue(JSContextRef ctx, JSValueRef val, JSValueRef *exc)
{
    double type = JSValueToNumber(ctx, val, exc);
    if (isnan(type))
        return NULL;
    if ((int)type == SELECTION_PRIMARY)
        return GDK_SELECTION_PRIMARY;
    else if ((int)type == SELECTION_CLIPBOARD)
        return GDK_NONE;
    else
        return NULL;
}

/**
 * Sets content of the system clipboard
 * @name set
 * @memberOf clipboard
 * @function 
 *
 * @param {Selection} selection The {@link Enums and Flags.Selection|Selection} to set
 * @param {String} text The text to set
 *
 * */
static JSValueRef 
clipboard_set(JSContextRef ctx, JSObjectRef f, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2)
        return NULL;
    GdkAtom atom = atom_from_jsvalue(ctx, argv[0], exc);
    if (atom == NULL)
        atom = GDK_NONE;
    char *text = js_value_to_char(ctx, argv[1], -1, exc);
    if (text != NULL)
    {
        GtkClipboard *cb = gtk_clipboard_get(atom);
        gtk_clipboard_set_text(cb, text, -1);
        g_free(text);
    }
    return NULL;
}
/** 
 * Callback called when the clipboard was received
 * @callback clipboard~onGotClipboard
 *
 * @param {String} text 
 *      The text content of the clipboard
 * */
static void
got_clipboard(GtkClipboard *cb, const char *text, JSObjectRef callback)
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSValueRef args[] = { text == NULL ? NIL : js_char_to_value(ctx, text) };
        scripts_call_as_function(ctx, callback, callback, 1, args);
        JSValueUnprotect(ctx, callback);
        scripts_release_global_context();
    }
}
/**
 * Gets content of the system clipboard
 * @name get
 * @memberOf clipboard
 * @function 
 *
 * @param {Selection} selection 
 *      The {@Link Enums and Flags.Selection|Selection} to get
 * @param {clipboard~onGotClipboard} [callback] 
 *      A callback function that is called when the clipboard content was
 *      retrieved, if a callback function is used the clipboard will be fetched
 *      asynchronously, otherwise it will be fetched synchronously
 *
 * @returns {String|undefined}
 *      The content of the clipboard or undefined if a callback function is used
 * */
static JSValueRef 
clipboard_get(JSContextRef ctx, JSObjectRef f, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1)
        return NIL;
    GdkAtom atom = atom_from_jsvalue(ctx, argv[0], exc);
    if (atom == NULL)
        atom = GDK_NONE;
    GtkClipboard *clipboard = gtk_clipboard_get(atom);
    if (argc > 1) 
    {
        JSObjectRef callback = js_value_to_function(ctx, argv[1], exc);
        if (callback != NULL)
        {
            JSValueProtect(ctx, callback);
            gtk_clipboard_request_text(clipboard, (GtkClipboardTextReceivedFunc)got_clipboard, callback);
        }
    }
    else 
    {
        JSValueRef ret = NIL;
        char *text = gtk_clipboard_wait_for_text(clipboard);
        if (text != NULL)
        {
            ret = js_char_to_value(ctx, text);
            g_free(text);
        }
        return ret;
    }
    return NIL;
}

JSObjectRef 
clipboard_initialize(JSContextRef ctx) {
    /**
     * Access to the system clipboard
     * @namespace 
     *      Accessing the system clipboard
     * @name clipboard 
     * @static 
     * @example
     * //!javascript
     *
     * var clipboard = namespace("clipboard");
     *
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSStaticFunction clipboard_functions[] = { 
        { "get",     clipboard_get,         kJSDefaultAttributes },
        { "set",     clipboard_set,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("clipboard", clipboard_functions, NULL, NULL);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "clipboard", NULL);
    JSClassRelease(klass);
    return ret;
}
