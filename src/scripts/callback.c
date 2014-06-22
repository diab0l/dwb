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


CallbackData * 
callback_data_new(GObject *gobject, JSObjectRef object, JSObjectRef callback, StopCallbackNotify notify)  
{
    CallbackData *c = NULL;
    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) 
        return NULL;

    c = g_malloc(sizeof(CallbackData));
    c->gobject = gobject != NULL ? g_object_ref(gobject) : NULL;
    if (object != NULL) 
    {
        JSValueProtect(ctx, object);
        c->object = object;
    }
    if (object != NULL) 
    {
        JSValueProtect(ctx, callback);
        c->callback = callback;
    }
    c->notify = notify;
    scripts_release_global_context();
    return c;
}/*}}}*/

/* callback_data_free {{{*/
void
callback_data_free(CallbackData *c) 
{
    if (c != NULL) 
    {
        if (c->gobject != NULL) 
            g_object_unref(c->gobject);

        JSContextRef ctx = scripts_get_global_context();
        if (ctx != NULL) {
            if (c->object != NULL) 
                JSValueUnprotect(ctx, c->object);
            if (c->callback != NULL) 
                JSValueUnprotect(ctx, c->callback);
            scripts_release_global_context();
        }

        g_free(c);
    }
}
void 
make_callback(JSContextRef ctx, JSObjectRef self, GObject *gobject, const char *signalname, JSValueRef value, StopCallbackNotify notify, JSValueRef *exception) 
{
    JSObjectRef func = js_value_to_function(ctx, value, exception);
    if (func != NULL) 
    {
        CallbackData *c = callback_data_new(gobject, self, func, notify);
        if (c != NULL)
            g_signal_connect_swapped(gobject, signalname, G_CALLBACK(callback), c);
    }
}

/** 
 * @callback WebKitDownload~statusCallback
 * @param {WebKitDownload} download 
 *      The download
 * */
void 
callback(CallbackData *c) 
{
    gboolean ret = false;
    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL)
        return;

    JSValueRef val[] = { c->object != NULL ? c->object : NIL };
    JSValueRef jsret = scripts_call_as_function(ctx, c->callback, c->callback, 1, val);
    if (JSValueIsBoolean(ctx, jsret))
        ret = JSValueToBoolean(ctx, jsret);
    if (ret || (c != NULL && c->gobject != NULL && c->notify != NULL && c->notify(c))) 
    {
        g_signal_handlers_disconnect_by_func(c->gobject, callback, c);
        callback_data_free(c);
    }

    scripts_release_global_context();
}
