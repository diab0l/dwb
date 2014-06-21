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
/* tabs_current {{{*/
/**
 * The currently focused webview
 *
 * @name current 
 * @memberOf tabs
 * @type WebKitWebView
 * */

static JSValueRef 
tabs_current(JSContextRef ctx, JSObjectRef this, JSStringRef name, JSValueRef* exc) 
{
    if (dwb.state.fview && CURRENT_VIEW()->script_wv) 
        return CURRENT_VIEW()->script_wv;
    else 
        return NIL;
}/*}}}*/

/* tabs_number {{{*/
/**
 * The number of the currently focused webview
 *
 * @name number 
 * @memberOf tabs
 * @type Number
 * */
static JSValueRef 
tabs_number(JSContextRef ctx, JSObjectRef this, JSStringRef name, JSValueRef* exc) 
{
    return JSValueMakeNumber(ctx, g_list_position(dwb.state.views, dwb.state.fview));
}/*}}}*/

/* tabs_length {{{*/
/**
 * Total number of tabs
 *
 * @name length 
 * @memberOf tabs
 * @type Number
 * */
static JSValueRef 
tabs_length(JSContextRef ctx, JSObjectRef this, JSStringRef name, JSValueRef* exc) 
{
    return JSValueMakeNumber(ctx, g_list_length(dwb.state.views));
}/*}}}*/

/* tabs_get{{{*/
static JSValueRef 
tabs_get(JSContextRef ctx, JSObjectRef this, JSStringRef name, JSValueRef* exc) {
    JSValueRef v = JSValueMakeString(ctx, name);
    double n = JSValueToNumber(ctx, v, exc);
    if (!isnan(n)) {
        GList *nth = g_list_nth(dwb.state.views, (int)n);
        if (nth != NULL) {
            return VIEW(nth)->script_wv;
        }
        else {
            return NIL;
        }
    }
    return NULL;
}
/*}}}*/

JSObjectRef
tabs_initialize(JSContextRef ctx) {
    /**
     * tabs is an array like object that can be used to get webviews. tabs also
     * implements all ECMAScript 5 array functions like forEach, map, filter, ...
     *
     * @namespace 
     *      Static object that can be used to get webviews
     * @name tabs 
     * @static 
     * @example
     * //!javascript
     *
     * var tabs = namespace("tabs");
     * // get the second tab
     * var secondTab = tabs[1];
     *
     * // iterate over all tabs
     * tabs.forEach(function(tab) {
     *      ...
     * });
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSStaticValue tab_values[] = { 
        { "current",      tabs_current, NULL,   kJSDefaultAttributes },
        { "number",       tabs_number,  NULL,   kJSDefaultAttributes },
        { "length",       tabs_length,  NULL,   kJSDefaultAttributes },
        { 0, 0, 0, 0 }, 
    };

    JSClassRef klass = scripts_create_class("tabs", NULL, tab_values, tabs_get);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "tabs", NULL);
    JSClassRelease(klass);
    return ret;
}
