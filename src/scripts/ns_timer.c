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

/**
 * @callback timer~startCallback
 * @returns {Boolean}
 *      Return true to stop the timer
 * */
static gboolean
timeout_callback(JSObjectRef obj) 
{
    gboolean ret = false;
    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) 
        return false;

    JSValueRef val = scripts_call_as_function(ctx, obj, obj, 0, NULL);
    if (val == NULL)
        ret = false;
    else 
        ret = !JSValueIsBoolean(ctx, val) || JSValueToBoolean(ctx, val);

    scripts_release_global_context();

    return ret;
}


/** 
 * For internal usage only
 *
 * @name _stop
 * @memberOf timer
 * @function
 * @private
 * */
/**
 * Stops a timer started by {@link timerStart}
 * @name stop 
 * @memberOf timer
 * @function
 * 
 * @param {Number} id 
 *      A timer handle retrieved from {@link timer.start|start}
 *
 * @returns {Boolean}
 *      true if the timer was stopped
 * */
static JSValueRef 
timer_stop(JSContextRef ctx, JSObjectRef f, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("timerStop: missing argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    gdouble sigid = JSValueToNumber(ctx, argv[0], exc);

    if (!isnan(sigid)) 
    {
        ScriptContext *sctx = scripts_get_context();
        if (sctx == NULL) 
            return JSValueMakeBoolean(ctx, false);

        gboolean ret = g_source_remove((int)sigid);
        GSList *source = g_slist_find(sctx->timers, GINT_TO_POINTER(sigid));
        if (source)
            sctx->timers = g_slist_delete_link(sctx->timers, source);

        scripts_release_context();
        return JSValueMakeBoolean(ctx, ret);
    }
    return JSValueMakeBoolean(ctx, false);
}

/** 
 * For internal usage only
 *
 * @name _start
 * @memberOf timer
 * @function
 * @private
 * */
/* global_timer_start {{{*/
static JSValueRef 
timer_start(JSContextRef ctx, JSObjectRef f, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2) 
    {
        js_make_exception(ctx, exc, EXCEPTION("timerStart: missing argument."));
        return JSValueMakeNumber(ctx, -1);
    }
    double msec = JSValueToNumber(ctx, argv[0], exc);
    int ret = -1;

    if (isnan(msec))
        return JSValueMakeNumber(ctx, -1);

    JSObjectRef func = js_value_to_function(ctx, argv[1], exc);
    if (func == NULL)
        return JSValueMakeNumber(ctx, -1);

    JSValueProtect(ctx, func);

    ScriptContext *sctx = scripts_get_context();
    if (sctx != NULL) {
        ret = g_timeout_add_full(G_PRIORITY_DEFAULT, (int)msec, (GSourceFunc)timeout_callback, func, (GDestroyNotify)scripts_unprotect);
        sctx->timers = g_slist_prepend(sctx->timers, GINT_TO_POINTER(ret));
        scripts_release_context();
    }
    return JSValueMakeNumber(ctx, ret);
}

JSObjectRef 
timer_initialize(JSContextRef ctx) {
    /**
     * Static object for timed execution 
     * @namespace 
     *      Static object for timed execution 
     * @name timer
     * @static
     * @example
     * //!javascript
     *
     * var timer = namespace("timer");
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSObjectRef ret;

    JSStaticFunction timer_functions[] = { 
        { "_start",         timer_start,         kJSDefaultAttributes },
        // FIXME: wrapper isn't really necessary
        { "_stop",        timer_stop,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("timer", timer_functions, NULL, NULL);

    ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "timer", NULL);
    JSClassRelease(klass);
    return ret;
}
