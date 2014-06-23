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
 * Get a history item 
 *
 * @name getItem
 * @memberOf WebKitWebBackForwardList.prototype
 * @function 
 *
 * @param {Number} position Position of the history item
 *
 * @return {WebKitWebHistoryItem}
 *      The history item
 * */
static JSValueRef 
history_get_item(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) {
        return NIL;
    }
    double n = JSValueToNumber(ctx, argv[0], NULL);
    if (!isnan(n))
    {
        WebKitWebBackForwardList *list = JSObjectGetPrivate(this);
        g_return_val_if_fail(list != NULL, NIL);

        return scripts_make_object(ctx, G_OBJECT(webkit_web_back_forward_list_get_nth_item(list, n)));
    }
    return NIL;
}
/**
 * Number of items that precede the current item
 *
 * @name backLength
 * @memberOf WebKitWebBackForwardList.prototype
 * @type Number
 * */
static JSValueRef 
history_back_length(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebBackForwardList *list = JSObjectGetPrivate(object);
    g_return_val_if_fail(list != NULL, NIL);
    return JSValueMakeNumber(ctx, webkit_web_back_forward_list_get_back_length(list));
}
/**
 * Number of items that succeed the current item
 *
 * @name forwardLength
 * @memberOf WebKitWebBackForwardList.prototype
 * @type Number
 * */
static JSValueRef 
history_forward_length(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebBackForwardList *list = JSObjectGetPrivate(object);
    g_return_val_if_fail(list != NULL, NIL);
    return JSValueMakeNumber(ctx, webkit_web_back_forward_list_get_forward_length(list));
}

void 
history_initialize(ScriptContext *sctx) {
    /**
     * The history of a webview
     *
     * @class 
     *     The history of a webview.
     * @augments GObject
     * @name WebKitWebBackForwardList
     *
     * */
    JSStaticFunction history_functions[] = { 
        { "getItem",           history_get_item,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSStaticValue history_values[] = {
        { "backLength",     history_back_length, NULL, kJSDefaultAttributes }, 
        { "forwardLength",     history_forward_length, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "HistoryList";
    cd.staticFunctions = history_functions;
    cd.staticValues = history_values;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_HISTORY] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_HISTORY_LIST] = scripts_create_constructor(sctx->global_context, "WebKitWebBackForwardList", sctx->classes[CLASS_HISTORY], NULL, NULL);
}
