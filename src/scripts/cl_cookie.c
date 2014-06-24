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

static JSObjectRef 
cookie_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    ScriptContext *sctx = scripts_get_context();
    JSObjectRef result = NULL;
    if (sctx != NULL) {
        SoupCookie *cookie = soup_cookie_new("", "", "", "", 0);
        result = JSObjectMake(ctx, sctx->classes[CLASS_COOKIE], cookie);
        scripts_release_context();
    }
    return result;
}

/**
 * Sets the maximum age of a cookie
 *
 * @name setMaxAge
 * @memberOf Cookie.prototype
 * @function
 * @since 1.5
 *
 * @param {Number} seconds 
 *      The number of seconds until the cookie expires, if set to -1 it is a
 *      session cookie
 * */
static JSValueRef 
cookie_set_max_age(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return NULL;
    SoupCookie *cookie = JSObjectGetPrivate(this);
    if (cookie != NULL )
    {
        double value = JSValueToNumber(ctx, argv[0], exc);
        if (!isnan(value))
        {
            soup_cookie_set_max_age(cookie, (int) value);
        }
    }
    return NULL;
}
/**
 * Deletes the cookie from the jar
 *
 * @name delete
 * @memberOf Cookie.prototype
 * @function
 * @since 1.5
 *
 * */
static JSValueRef 
cookie_delete(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupCookie *cookie = JSObjectGetPrivate(this);
    if (cookie != NULL)
        dwb_soup_cookie_delete(cookie);
    return NULL;
}
/**
 * Saves the cookie to the jar
 *
 * @name save
 * @memberOf Cookie.prototype
 * @function
 * @since 1.5
 *
 * */
static JSValueRef 
cookie_save(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupCookie *cookie = JSObjectGetPrivate(this);
    if (cookie != NULL)
        dwb_soup_cookie_save(cookie);
    return NULL;
}
/** 
 * The cookie name
 * @name name
 * @memberOf Cookie.prototype
 * @type String
 * @since 1.5
 * */
BOXED_GET_CHAR(cookie_get_name, soup_cookie_get_name, SoupCookie);
BOXED_SET_CHAR(cookie_set_name, soup_cookie_set_name, SoupCookie);
/** 
 * The cookie value
 * @name value
 * @memberOf Cookie.prototype
 * @type String
 * @since 1.5
 * */
BOXED_GET_CHAR(cookie_get_value, soup_cookie_get_value, SoupCookie);
BOXED_SET_CHAR(cookie_set_value, soup_cookie_set_value, SoupCookie);
/** 
 * The cookie domain
 * @name domain
 * @memberOf Cookie.prototype
 * @type String
 * @since 1.5
 * */
BOXED_GET_CHAR(cookie_get_domain, soup_cookie_get_domain, SoupCookie);
BOXED_SET_CHAR(cookie_set_domain, soup_cookie_set_domain, SoupCookie);
/** 
 * The cookie path
 * @name path
 * @memberOf Cookie.prototype
 * @type String
 * @since 1.5
 * */
BOXED_GET_CHAR(cookie_get_path, soup_cookie_get_path, SoupCookie);
BOXED_SET_CHAR(cookie_set_path, soup_cookie_set_path, SoupCookie);
/** 
 * If the cookie should only be transferred over ssl
 * @name secure
 * @memberOf Cookie.prototype
 * @type boolean
 * @since 1.5
 * */
BOXED_GET_BOOLEAN(cookie_get_secure, soup_cookie_get_secure, SoupCookie);
BOXED_SET_BOOLEAN(cookie_set_secure, soup_cookie_set_secure, SoupCookie);
/** 
 * If the cookie should not be exposed to scripts
 * @name httpOnly
 * @memberOf Cookie.prototype
 * @type String
 * @since 1.5
 * */
BOXED_GET_BOOLEAN(cookie_get_http_only, soup_cookie_get_http_only, SoupCookie);
BOXED_SET_BOOLEAN(cookie_set_http_only, soup_cookie_set_http_only, SoupCookie);

/** 
 * The cookie expiration time
 * @name expires
 * @memberOf Cookie.prototype
 * @type Date
 * @readonly
 * @since 1.5
 * */
static JSValueRef 
cookie_get_expires(JSContextRef ctx, JSObjectRef this, JSStringRef property, JSValueRef* exception)  
{
    JSValueRef ret = NIL;
    SoupCookie *cookie = JSObjectGetPrivate(this);
    if (cookie != NULL)
    {
        SoupDate *date = soup_cookie_get_expires(cookie);
        if (date == NULL)
            return NIL;
        char *date_str = soup_date_to_string(date, SOUP_DATE_HTTP);
        JSValueRef argv[] = { js_char_to_value(ctx, date_str) };
        g_free(date_str);
        ret  = JSObjectMakeDate(ctx, 1, argv, exception);
    }
    return ret;
}

static void 
cookie_finalize(JSObjectRef o)
{
    SoupCookie *cookie = JSObjectGetPrivate(o);
    if (cookie != NULL)
    {
        soup_cookie_free(cookie);
    }
}

void 
cookie_initialize(ScriptContext *sctx) {
    /** 
     * Constructs a new cookie
     *
     * @class 
     *    A cookie
     * @name Cookie
     * @since 1.5
     *
     * @constructs Cookie 
     *
     * @returns Cookie
     * */
    /* download */
    JSStaticValue cookie_values[] = {
        { "name",           cookie_get_name, cookie_set_name, kJSPropertyAttributeDontDelete }, 
        { "value",          cookie_get_value, cookie_set_value, kJSPropertyAttributeDontDelete }, 
        { "domain",         cookie_get_domain, cookie_set_domain, kJSPropertyAttributeDontDelete }, 
        { "path",           cookie_get_path, cookie_set_path, kJSPropertyAttributeDontDelete }, 
        { "expires",        cookie_get_expires, NULL, kJSPropertyAttributeReadOnly }, 
        { "secure",         cookie_get_secure, cookie_set_secure, kJSPropertyAttributeDontDelete }, 
        { "httpOnly",       cookie_get_http_only, cookie_set_http_only, kJSPropertyAttributeDontDelete }, 
        { 0, 0, 0, 0 }, 
    };
    JSStaticFunction cookie_functions[] = {
        { "save",               cookie_save, kJSDefaultAttributes }, 
        { "delete",             cookie_delete, kJSDefaultAttributes },
        { "setMaxAge",          cookie_set_max_age, kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.staticValues = cookie_values;
    cd.staticFunctions = cookie_functions;
    cd.finalize = cookie_finalize;
    sctx->classes[CLASS_COOKIE] = JSClassCreate(&cd);
    sctx->constructors[CONSTRUCTOR_COOKIE] = scripts_create_constructor(sctx->global_context, "Cookie", sctx->classes[CLASS_COOKIE], cookie_constructor_cb, NULL);
}
