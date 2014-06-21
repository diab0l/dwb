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

#include "script_private.h"

/** 
 * Profile which will be <i>default</i> unless another profile is specified on command line
 *
 * @name profile 
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_profile(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return js_char_to_value(ctx, dwb.misc.profile);
}

/** 
 * The current session name, if 'save-session' is disabled and no session name
 * is given on commandline it will always be "default"
 *
 * @name sessionName
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_session_name(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return js_char_to_value(ctx, session_get_name());
}

/** 
 * The cache directory used by dwb
 *
 * @name cacheDir 
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_cache_dir(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return js_char_to_value(ctx, dwb.files[FILES_CACHEDIR]);
}

/** 
 * The configuration diretory
 *
 * @name configDir 
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_config_dir(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    char *dir = util_build_path();
    if (dir == NULL) 
        return NIL;

    JSValueRef ret = js_char_to_value(ctx, dir);
    g_free(dir);
    return ret;
}

/** 
 * The system data dir, for a default installation it is /usr/share/dwb
 *
 * @name systemDataDir 
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_system_data_dir(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    char *dir = util_get_system_data_dir(NULL);
    if (dir == NULL) 
        return NIL;

    JSValueRef ret = js_char_to_value(ctx, dir);
    g_free(dir);
    return ret;
}

/** 
 * The user data dir, in most cases it will be ~/.local/share/dwb
 *
 * @name userDataDir 
 * @memberOf data
 * @readonly
 * @type String
 * */
static JSValueRef 
data_get_user_data_dir(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    char *dir = util_get_user_data_dir(NULL);
    if (dir == NULL) 
        return NIL;

    JSValueRef ret = js_char_to_value(ctx, dir);
    g_free(dir);
    return ret;
}

JSObjectRef
data_initialize(JSContextRef ctx) {
    /** 
     * Get internally used data like configuration files
     * 
     * @namespace
     *      Get internally used data like configuration files
     * @name data
     * @static 
     *
     * @example 
     * //!javascript
     *
     * var data = namespace("data");
     * */

    JSObjectRef data;

    JSObjectRef global_object = JSContextGetGlobalObject(ctx);

    JSStaticValue data_values[] = {
        { "profile",        data_get_profile, NULL, kJSDefaultAttributes },
        { "sessionName",    data_get_session_name, NULL, kJSDefaultAttributes },
        { "cacheDir",       data_get_cache_dir, NULL, kJSDefaultAttributes },
        { "configDir",      data_get_config_dir, NULL, kJSDefaultAttributes },
        { "systemDataDir",  data_get_system_data_dir, NULL, kJSDefaultAttributes },
        { "userDataDir",    data_get_user_data_dir, NULL, kJSDefaultAttributes },
        { 0, 0, 0,  0 }, 
    };
    JSClassRef klass = scripts_create_class("data", NULL, data_values, NULL);
    data = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "data", NULL);
    JSClassRelease(klass);
    return data;
}
