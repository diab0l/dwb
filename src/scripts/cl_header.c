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

static void 
finalize_headers(JSObjectRef o)
{
    SoupMessageHeaders *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        soup_message_headers_free(ob);
    }
}

JSValueRef
soup_header_get_function(JSContextRef ctx, const char * (*func)(SoupMessageHeaders *, const char *name), 
        JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    JSValueRef result = NIL;
    if (argc > 0)
    {
        SoupMessageHeaders *hdrs = JSObjectGetPrivate(this);
        char *name = js_value_to_char(ctx, argv[0], -1, exc);
        if (name != NULL)
        {
            const char *value = func(hdrs, name);
            if (value)
            {
                result = js_char_to_value(ctx, value);
            }
            g_free(name);
        }
    }
    return result;
}
void
soup_header_set_function(JSContextRef ctx, void (*func)(SoupMessageHeaders *, const char *name, const char *value), 
        JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef *exc)
{
    if (argc > 1)
    {
        SoupMessageHeaders *hdrs = JSObjectGetPrivate(this);
        char *name = js_value_to_char(ctx, argv[0], -1, exc);
        if (name != NULL)
        {
            char *value = js_value_to_char(ctx, argv[1], -1, exc);
            if (value)
            {
                func(hdrs, name, value);
                g_free(value);
            }
            g_free(name);
        }
    }
}
/** 
 * Gets a header 
 *
 * @name getOne
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @param {String} name Name of the header
 *
 * @returns {String}
 *      The header or <i>null</i>.
 *
 * @since 1.1
 * */
static JSValueRef 
soup_headers_get_one(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return soup_header_get_function(ctx, soup_message_headers_get_one, this, argc, argv, exc);
}
/** 
 * Gets a comma seperated header list
 *
 * @name getList
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @param {String} name Name of the header list
 *
 * @returns {String}
 *      A comma seperated header list or <i>null</i>.
 *
 * @since 1.1
 * */
static JSValueRef 
soup_headers_get_list(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return soup_header_get_function(ctx, soup_message_headers_get_list, this, argc, argv, exc);
}
/** 
 * Appends a value to a header
 *
 * @name append
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @param {String} name 
 *      Name of the header
 * @param {String} value 
 *      Value of the header
 *
 * @since 1.1
 * */
static JSValueRef 
soup_headers_append(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    soup_header_set_function(ctx, soup_message_headers_append, this, argc, argv, exc);
    return NULL;
}
/** 
 * Replaces a header
 *
 * @name replace
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @param {String} name 
 *      Name of the header
 * @param {String} value
 *      New value of the header
 * @since 1.1
 * */
static JSValueRef 
soup_headers_replace(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    soup_header_set_function(ctx, soup_message_headers_replace, this, argc, argv, exc);
    return NULL;
}
/** 
 * Removes a header
 *
 * @name remove
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @param {String} name 
 *      Name of the header
 * @since 1.1
 * */
static JSValueRef 
soup_headers_remove(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc > 0)
    {
        SoupMessageHeaders *hdrs = JSObjectGetPrivate(this);
        char *name = js_value_to_char(ctx, argv[0], -1, exc);
        if (name != NULL)
        {
            soup_message_headers_remove(hdrs, name);
            g_free(name);
        }
    }
    return NULL;
}
/** 
 * Removes all headers
 *
 * @name clear
 * @memberOf SoupHeaders.prototype
 * @function
 *
 * @since 1.1
 * */
static JSValueRef 
soup_headers_clear(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupMessageHeaders *hdrs = JSObjectGetPrivate(this);
    soup_message_headers_clear(hdrs);
    return NULL;
}


void 
header_initialize(ScriptContext *sctx) {
    /** 
     * Represents a SoupMessage header
     *
     * @class 
     *    Represents a {@link SoupMessage} header.
     * @name SoupHeaders
     * @augments Object
     *
     * @since 1.1
     * */
    JSStaticFunction header_functions[] = { 
        { "getOne",                soup_headers_get_one,       kJSDefaultAttributes },
        { "getList",                soup_headers_get_list,       kJSDefaultAttributes },
        { "append",                soup_headers_append,       kJSDefaultAttributes },
        { "replace",                soup_headers_replace,       kJSDefaultAttributes },
        { "remove",                soup_headers_remove,       kJSDefaultAttributes },
        { "clear",                soup_headers_clear,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "SoupHeaders";
    cd.staticFunctions = header_functions;
    cd.finalize = finalize_headers;
    sctx->classes[CLASS_SOUP_HEADER] = JSClassCreate(&cd);
}
