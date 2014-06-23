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
 * The elapsed time since the timer was started, or if the timer is stopped the
 * elapsed time between last start and last stop.
 *
 * @name elapsed
 * @memberOf Timer.prototype
 * @type Object 
 * @property {Number} seconds   The elapsed seconds with fractional part
 * @property {Number} micro     The the fractional part in microseconds
 * @since 1.10
 * */
static JSValueRef 
gtimer_elapsed(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    gdouble elapsed = 0;
    gulong micro = 0;
    GTimer *timer = JSObjectGetPrivate(object);
    if (timer != NULL) {
        elapsed = g_timer_elapsed(timer, &micro);
        JSObjectRef value = JSObjectMake(ctx, NULL, NULL);
        js_set_object_number_property(ctx, value, "seconds", elapsed, exception);
        js_set_object_number_property(ctx, value, "micro", micro, exception);
        return value;
    }
    return NIL;
}/*}}}*/

/** 
 * Starts the timer
 *
 * @name start
 * @memberOf Timer.prototype
 * @function 
 * @since 1.10
 * */
BOXED_DEF_VOID(GTimer, gtimer_start, g_timer_start);
/** 
 * Stops the timer
 *
 * @name stop
 * @memberOf Timer.prototype
 * @function 
 * @since 1.10
 * */
BOXED_DEF_VOID(GTimer, gtimer_stop, g_timer_stop);
/** 
 * Continues the timer if the timer has been stopped
 *
 * @name continue
 * @memberOf Timer.prototype
 * @function 
 * @since 1.10
 * */
BOXED_DEF_VOID(GTimer, gtimer_continue, g_timer_continue);
/** 
 * Resets the timer
 *
 * @name reset
 * @memberOf Timer.prototype
 * @function
 * @since 1.10
 * */
BOXED_DEF_VOID(GTimer, gtimer_reset, g_timer_reset);

static JSObjectRef 
gtimer_construtor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    ScriptContext *sctx = scripts_get_context();
    GTimer *timer = g_timer_new();
    return JSObjectMake(ctx, sctx->classes[CLASS_TIMER], timer);
}
static void 
finalize(JSObjectRef o) {
    GTimer *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        g_timer_destroy(ob);
    }
}


void
cltimer_initilize(ScriptContext *sctx) {
    /** 
     * Constructs a new Timer.
     *
     * @name Timer
     * @class 
     *      A timer object for measuring time, e.g. for debugging purposes
     * @since 1.10
     * */

    JSStaticValue gtimer_values[] = {
        { "elapsed",                 gtimer_elapsed,        NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSStaticFunction gtimer_functions[] = {
        { "start",              gtimer_start,       kJSDefaultAttributes }, 
        { "stop",               gtimer_stop,        kJSDefaultAttributes }, 
        { "continue",           gtimer_continue,    kJSDefaultAttributes }, 
        { "reset",              gtimer_reset,       kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };

    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "Timer";
    cd.staticFunctions = gtimer_functions;
    cd.staticValues = gtimer_values;
    cd.finalize = finalize;
    sctx->classes[CLASS_TIMER] = JSClassCreate(&cd);
    sctx->constructors[CONSTRUCTOR_TIMER] = scripts_create_constructor(sctx->global_context, "Timer", sctx->classes[CLASS_TIMER], gtimer_construtor_cb, NULL);
}
