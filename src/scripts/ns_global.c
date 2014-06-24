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

static char * 
xextract(JSContextRef ctx, size_t argc, const JSValueRef argv[], char **archive, off_t *fs, JSValueRef *exc)
{
    char *content = NULL, *larchive = NULL, *path = NULL;
    if (argc < 2) 
        return NULL;
    if ((larchive = js_value_to_char(ctx, argv[0], -1, exc)) == NULL)
        goto error_out;
    if ((path = js_value_to_char(ctx, argv[1], -1, exc)) == NULL)
        goto error_out;
    if (*path == '~') {
        content = (char*)exar_search_extract(larchive, path + 1, fs);
    }
    else {
        content = (char*)exar_extract(larchive, path, fs);
    }
error_out: 
    if (archive != NULL) 
        *archive = larchive;
    else 
        g_free(larchive);
    g_free(path);
    return content;
}


/** 
 * Includes a file. 
 * Note that included files are not visible in other scripts unless they are
 * explicitly injected into the global scope. To use functions or variables from
 * an included script the script can either return an object containing the
 * public functions/variables or {@link provide} can be called in the included
 * script. 
 *
 * @name include 
 * @function
 * @param {String} path 
 *      The path to the script
 * @param {Boolean} global
 *      Whether the script should be included into the global scope.
 *
 * @return {Object}
 *      The object returned from the included script 
 *      
 * @example
 * // included script 
 * function foo()
 * {
 *     io.out("bar");
 * }
 * return {
 *      foo : foo
 * }
 *
 * // including script
 * var x = include("/path/to/script"); 
 * x.foo();
 *
 *
 * // included script
 * provide("foo", {
 *      foo : function() 
 *      {
 *          io.out("bar");
 *      }
 * });
 *
 * // including script
 * include("/path/to/script");
 * require(["foo"], function(foo) {
 *      foo.foo();
 * });
 * */

static JSValueRef 
global_include(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    gboolean global = false;
    char *path = NULL, *content = NULL, *econtent = NULL; 
    const char *script;
    JSValueRef exports[1];
    gboolean is_archive = false;

    if (argc < 1) 
        return NIL;

    if (argc > 1 && JSValueIsBoolean(ctx, argv[1])) 
        global = JSValueToBoolean(ctx, argv[1]);

    if ( (path = js_value_to_char(ctx, argv[0], PATH_MAX, exc)) == NULL) 
        goto error_out;

    if (exar_check_version(path) == 0)
    {
        econtent = (char*) exar_search_extract(path, "main.js", NULL);
        if (econtent == NULL)
        {
            js_make_exception(ctx, exc, EXCEPTION("include: main.js was not found in %s."), path);
            goto error_out;
        }
        exports[0] = scripts_get_exports(ctx, path);
        is_archive = true;
        script = econtent;
    }
    else if ( (content = util_get_file_content(path, NULL)) != NULL) 
    {
        script = content;
        exports[0] = JSValueMakeNull(ctx);
    }
    else {
        js_make_exception(ctx, exc, EXCEPTION("include: reading %s failed."), path);
        goto error_out;
    }

    if (*script == '#') 
    {
        do {
            script++;
        } while(*script && *script != '\n');
        script++;
    }

    ret = scripts_include(ctx, path, script, global, is_archive, 1, exports, exc);

error_out: 
    g_free(content);
    exar_free(econtent);
    g_free(path);
    return ret;
}/*}}}*/


/** 
 * Unbind a shortcut previously bound with <b>bind</b>
 * @name unbind 
 * @function
 *
 * @param {Number|String|bindCallback} id|command|callback 
 *      Either the handle id returned from bind or the function or the command passed to {@link bind}
 *
 * @returns {Boolean}
 *      Whether the shortcut was found and unbound
 *
 * */
static JSValueRef 
global_unbind(JSContextRef ctx, JSObjectRef function, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
#define KEYMAP_MAP(l) (((KeyMap*)((l)->data))->map)
    if (argc == 0) 
        return JSValueMakeBoolean(ctx, false);
    gboolean result = false;

    ScriptContext *sctx = scripts_get_context();
    if (sctx == NULL) 
        return JSValueMakeBoolean(ctx, false);
    GList *l = NULL;
    if (JSValueIsNumber(ctx, argv[0])) {
        int id = (int)JSValueToNumber(ctx, argv[0], exc);
        for (l = dwb.keymap; l; l=l->next)
            if (KEYMAP_MAP(l)->prop & CP_SCRIPT && KEYMAP_MAP(l)->arg.i == id) {
                break;
            }
    }
    else if (JSValueIsString(ctx, argv[0])) 
    {
        char *name = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);
        for (l = dwb.keymap; l; l=l->next)
            if (KEYMAP_MAP(l)->prop & CP_SCRIPT && g_strcmp0(KEYMAP_MAP(l)->n.first, name)) {
                break;
            }
        g_free(name);
    }
    else if (JSValueIsObject(ctx, argv[0])) 
    {
        for (l = dwb.keymap; l; l=l->next)
            if ( KEYMAP_MAP(l)->arg.js && 
                    JSValueIsEqual(ctx, argv[0], KEYMAP_MAP(l)->arg.js, exc) )
                break;
    }
    if (l != NULL) 
    {
        // don't free it yet, if unbind is called from inside a bind
        // callback parse_command_line and eval_key would use an invalid keymap
        KEYMAP_MAP(l)->arg.i = 0;
        sctx->keymap_dirty = true;

        for (GList *gl = dwb.override_keys; gl; gl=gl->next)
        {
            KeyMap *m = gl->data;
            if (m->map->prop & CP_SCRIPT) 
                dwb.override_keys = g_list_delete_link(dwb.override_keys, l);
        }
        result = true;
    }
    scripts_release_context();
    return JSValueMakeBoolean(ctx, result);
#undef KEYMAP_MAP
}/*}}}*/
/** 
 * For internal usage only
 *
 * @name _bind
 * @function
 * @private
 *
 * */
/* global_bind {{{*/
static JSValueRef 
global_bind(JSContextRef ctx, JSObjectRef function, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    static int id;
    int ret = -1;

    gchar *keystr, *callback_name;
    char *name = NULL, *callback = NULL;
    guint option = CP_DONT_SAVE | CP_SCRIPT | CP_DONT_CLEAN;
    gboolean override = false;

    if (argc < 2) 
        return JSValueMakeBoolean(ctx, false);

    keystr = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);

    JSObjectRef func = js_value_to_function(ctx, argv[1], exc);
    if (func == NULL)
        goto error_out;

    if (argc > 2) 
    {
        if (JSValueIsNumber(ctx, argv[2]) )
        {
            double additional_option = JSValueToNumber(ctx, argv[2], exc);
            if (!isnan(additional_option) && (int) additional_option & CP_OVERRIDE)
            {
                option |= (int)additional_option & CP_OVERRIDE;
                override = true;
            }
        }
        else 
        {
            name = js_value_to_char(ctx, argv[2], JS_STRING_MAX, exc);
            if (name != NULL) 
            { 
                option |= CP_COMMANDLINE;
                callback_name = js_get_string_property(ctx, func, "name");
                callback = g_strdup_printf("JavaScript: %s", callback_name == NULL || *callback_name == 0 ? "[anonymous]" : callback_name);
                g_free(callback_name);
            }
        }
    }
    if (keystr == NULL && name == NULL) 
        goto error_out;

    JSValueProtect(ctx, func);

    ret = ++id;
    Arg a = { .js = func, .i = ret };
    KeyMap *map = dwb_add_key(keystr, name, callback, (Func)scripts_eval_key, option, &a);
    if (override)
        dwb.override_keys = g_list_prepend(dwb.override_keys, map);

error_out:
    g_free(keystr);
    return JSValueMakeNumber(ctx, ret);
}/*}}}*/

/** 
 * Executes a command, note that execute cannot be called in the global scope of
 * a script or in a function that is called from the global scope of the
 * script, i.e. it can only be called from functions that aren't executed when
 * the script is first executed like signal callbacks or bind callbacks. If it
 * is required call execute directly after dwb has been initialized the
 * <b>ready</b> signal can be used.
 * @name execute 
 * @function
 * @example 
 * bind("Control x", function() {
 *      execute("tabopen www.example.com");
 * });
 *
 * // Calling execute on startup
 * Signal.connect("ready", function() {
 *      execute("tabopen example.com");
 * });
 *
 * @param {String} name 
 *      A command, the command syntax is the same as the syntax on dwb's
 *      commandline
 *
 * @returns {Boolean}
 *      true if execution was successful
 *
 *
 * */
static JSValueRef 
global_execute(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    DwbStatus status = STATUS_ERROR;
    if (argc < 1) 
        return JSValueMakeBoolean(ctx, false);
    if (!dwb.state.views || !dwb.state.fview) {
        js_make_exception(ctx, exc, EXCEPTION("execute can only be called after dwb has been initialized"));
        return JSValueMakeBoolean(ctx, false);
    }

    char *command = js_value_to_char(ctx, argv[0], -1, exc);
    if (command != NULL) 
    {
        status = dwb_parse_command_line(command);
        g_free(command);
    }
    if (status == STATUS_END)
        exit(0);
    return JSValueMakeBoolean(ctx, status == STATUS_OK);
}/*}}}*/
static JSValueRef 
global_namespace(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    ScriptContext *sctx = scripts_get_context();
    if (sctx == NULL) 
        return NULL;
    static const char *mapping[] = {
        [NAMESPACE_CLIPBOARD]   = "clipboard",
        [NAMESPACE_CONSOLE]     = "console",
        [NAMESPACE_DATA]        = "data",
        [NAMESPACE_EXTENSIONS]  = "extensions",
        [NAMESPACE_GUI]         = "gui",
        [NAMESPACE_IO]          = "io",
        [NAMESPACE_NET]         = "net",
        [NAMESPACE_SIGNALS]     = "signals",
        [NAMESPACE_SYSTEM]      = "system",
        [NAMESPACE_TABS]        = "tabs",
        [NAMESPACE_TIMER]       = "timer",
        [NAMESPACE_UTIL]        = "util",
#ifdef WITH_LIBSECRET
        [NAMESPACE_KEYRING]     = "keyring"
#endif
    };
    JSValueRef ret = NULL;
    if (argc > 0) {
        JSStringRef name = JSValueToStringCopy(ctx, argv[0], exc);
        if (name != NULL) {
            for (int i=0; i<NAMESPACE_LAST; i++) {
                if (JSStringIsEqualToUTF8CString(name, mapping[i])) {
                    ret = sctx->namespaces[i];
                    break;
                }
            } 
        }
        JSStringRelease(name);
    }
    scripts_release_context();
    return ret;
}

/** 
 * Exit dwb
 * @name exit 
 * @function
 *
 * */
/* global_exit {{{*/
static JSValueRef 
global_exit(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    dwb_end(0);
    exit(EXIT_SUCCESS);
    return 0;
}/*}}}*/

/** 
 * Refers to the global object
 * @name global 
 * @type Object
 * @readonly
 *
 * */
static JSValueRef 
global_get(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return JSContextGetGlobalObject(ctx);
}
/** 
 * Same as {@link provide}, but can only be called from an archive. The module
 * can only be required by the same archive in which the module was provided.
 *
 * @name xprovide
 * @function
 * @param {String} name The name of the module
 * @param {Object} module The module
 * @param {Boolean} [overwrite]
 *      Whether to overwrite existing module with
 *      the same name, default false
 * */
/** 
 * Same as {@link require}, but can only be called from an archive and xrequire
 * is always synchronous. Only modules provided in the same archive can be
 * loaded with xrequire.
 *
 * @name xrequire
 * @function
 * @param {String} name The name of the module
 *
 * @returns {Object} 
 *      The module
 * */
/** 
 * Load a textfile from an archive. This function can only be called from scripts
 * inside an archive.
 *
 * @name xgettext
 * @function
 * @param {String} path
 *      Path of the file in the archive, if the path starts with a ~ dwb
 *      searches for the file in the archive, i.e. it doesn't have to be an
 *      absolute archive path but the filename has to be unique
 * @returns {String} 
 *      The content of the file
 *
 * @example
 * // Archive structure
 * //
 * // content/main.js
 * // content/bar.html
 * // content/foo.html
 * var bar = xgettext("content/bar.html"); 
 * var foo = xgettext("~foo.html"); 
 * 
 * */
static JSValueRef
global_xget_text(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *content = NULL;
    JSValueRef ret = NIL;
    off_t fs;

    content = xextract(ctx, argc, argv, NULL, &fs, exc);
    if (content != NULL) {
        ret = js_char_to_value(ctx, content);
    }
    exar_free(content);
    return ret;
}
/** 
 * Include scripts from an archive.
 *
 * Same as {@link include} but this function can only be called from scripts
 * inside an archive, so this is mostly useful in extensions. However it is
 * possible to include scripts from an archive calling the internal function
 * _xinclude which takes two parameters, the path of the archive and the path of
 * the included file in the archive.
 * All scripts in an archive share an object <b>exports</b> which can be used
 * to share data between scripts in an archive, all exports objects have a
 * readonly property <b>id</b> which is unique to all archives, it can be used
 * together with require/provide to define unique module names.
 *
 * Unlike {@link include} included archive-scripts cannot be included into the
 * global scope. 
 *
 * @name xinclude
 * @function
 * @param {String} path
 *      Path of the file in the archive, if the path starts with a ~ dwb
 *      searches for the file in the archive, i.e. it doesn't have to be an
 *      absolute archive path but the filename has to be unique
 *
 * @returns {Object}
 *      The object returned from the included file.
 *
 * @example
 * // Archive structure
 * //
 * // main.js
 * // content/foo.js
 * // content/bar.js
 *
 * // main.js
 * xinclude("content/foo.js");
 * // searches for bar.js in the archive
 * xinclude("~bar.js");
 *
 * // content/foo.js
 * function getFoo() {
 *      return 37;
 * }
 * exports.getFoo = getFoo;
 *
 * // content/bar.js
 * var x = exports.getFoo();
 *
 * // using require/provide
 * // main.js
 * require(["foo" + exports.id], function(foo) {
 *      io.out(foo.bar);
 * });
 * xinclude("content/foo.js");
 *
 * // content/foo.js
 * provide("foo" + exports.id, {
 *     bar : 37
 * }); 
 *
 * */
static JSValueRef
global_xinclude(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *content = NULL, *archive = NULL;
    JSValueRef ret = NIL;
    off_t fs;

    content = xextract(ctx, argc, argv, &archive, &fs, exc);
    if (content != NULL)
    {
        JSValueRef exports[] = { scripts_get_exports(ctx, archive) };
        ret = scripts_include(ctx, archive, content, false, true, 1, exports, exc);
    }
    g_free(archive);
    exar_free(content);
    return ret;
}



JSGlobalContextRef 
global_initialize() {
    JSGlobalContextRef ctx;

    JSStaticValue global_values[] = {
        { "global",       global_get, NULL,   kJSDefaultAttributes },
        { 0, 0, 0, 0 }, 
    };

    JSStaticFunction global_functions[] = { 
        { "namespace",          global_namespace,         kJSDefaultAttributes },
        { "execute",            global_execute,         kJSDefaultAttributes },
        { "exit",               global_exit,            kJSDefaultAttributes },
        { "_bind",              global_bind,            kJSDefaultAttributes },
        { "unbind",             global_unbind,          kJSDefaultAttributes },
        { "include",            global_include,         kJSDefaultAttributes },
        { "_xinclude",          global_xinclude,       kJSDefaultAttributes },
        { "_xgettext",          global_xget_text,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };

    JSClassRef klass = scripts_create_class("dwb", global_functions, global_values, NULL);
    ctx = JSGlobalContextCreate(klass);
    JSClassRelease(klass);
    return ctx;
}
