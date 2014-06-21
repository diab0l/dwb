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

static JSValueRef 
term_print(JSContextRef ctx, FILE *stream, size_t argc, const JSValueRef argv[], JSValueRef *exc) {
    if (argc == 0)
        return NULL;
    char *out = NULL;
    double dout;
    char *json = NULL;
    int type = 0;
    for (size_t i = 0; i < argc; i++) {
        type = JSValueGetType(ctx, argv[i]);
        switch (type) 
        {
            case kJSTypeString : 
                out = js_value_to_char(ctx, argv[i], -1, exc);
                if (out != NULL) 
                { 
                    if (!isatty(fileno(stream)))
                    {
                        GRegex *regex = g_regex_new("\e\\[\\d+(?>(;\\d+)*)m", 0, 0, NULL);
                        char *tmp = out;

                        out = g_regex_replace(regex, tmp, -1, 0, "", 0, NULL);
                        g_regex_unref(regex);

                        g_free(tmp);
                        if (out == NULL)
                            return NULL;
                    }
                    fprintf(stream, "%s", out);
                    g_free(out);
                }
                break;
            case kJSTypeBoolean : 
                fprintf(stream, "%s", JSValueToBoolean(ctx, argv[i]) ? "true" : "false");
                break;
            case kJSTypeNumber : 
                dout = JSValueToNumber(ctx, argv[i], exc);
                if (!isnan(dout)) 
                    if ((int)dout == dout) 
                        fprintf(stream, "%d\n", (int)dout);
                    else 
                        fprintf(stream, "%f\n", dout);
                else 
                    fprintf(stream, "NAN");
                break;
            case kJSTypeUndefined : 
                fprintf(stream, "undefined");
                break;
            case kJSTypeNull : 
                fprintf(stream, "null");
                break;
            case kJSTypeObject : 
                json = js_value_to_json(ctx, argv[i], -1, argc == 1 ? 2 : 0,  NULL);
                if (json != NULL) 
                {
                    fprintf(stream, "%s", json);
                    g_free(json);
                }
                break;
            default : break;
        }
        fputs(i < argc - 1 ? ", " : "\n", stream);
    }
    return NULL;
}

/* io_prompt {{{*/
/**
 * Gets user input synchronously
 *
 * @name prompt 
 * @memberOf io
 * @function 
 *
 * @param {String} prompt The prompt message
 * @param {Boolean} [visible] Whether the chars should be visible, pass false
 *                            for a password prompt, default true.
 *
 * @returns {String}
 *      The user response
 * */

static JSValueRef 
io_prompt(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *prompt = NULL;
    gboolean visibility = true;
    if (argc > 0) 
        prompt = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);

    if (argc > 1 && JSValueIsBoolean(ctx, argv[1])) 
        visibility = JSValueToBoolean(ctx, argv[1]);

    char *response = dwb_prompt(visibility, prompt);
    g_free(prompt);

    if (response == NULL)
        return NIL;

    JSValueRef result = js_char_to_value(ctx, response);

    sec_memset(response, 0, strlen(response));
    g_free(response);
    return result;
}/*}}}*/

/**
 * Shows a confirmation prompt
 *
 * @name confirm 
 * @memberOf io
 * @function 
 *
 * @param {String} prompt The prompt message
 *
 * @returns {Boolean}
 *      True if <i>y</i> was pressed, false if <i>n</i> or escape was pressed
 * */
static JSValueRef 
io_confirm(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *prompt = NULL;
    gboolean result = false;
    
    if (argc > 0)
        prompt = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);

    entry_hide();

    result = dwb_confirm(dwb.state.fview, prompt ? prompt : "");
    g_free(prompt);
    return JSValueMakeBoolean(ctx, result);
}

/**
 * Read from a file 
 *
 * @name read 
 * @memberOf io
 * @function 
 *
 * @param {String} path A path to a file
 *
 * @returns {String}
 *      The file content
 * */
/* io_read {{{*/
static JSValueRef 
io_read(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char expanded[4096];
    JSValueRef ret = NULL;
    char *path = NULL, *content = NULL;
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("io.read needs an argument."));
        return NIL;
    }
    if ( (path = js_value_to_char(ctx, argv[0], PATH_MAX, exc) ) == NULL )
        goto error_out;

    if (util_expand_home(expanded, path, sizeof(expanded)) == NULL)
    {
        js_make_exception(ctx, exc, EXCEPTION("Filename too long"));
        goto error_out;
    }
    if ( (content = util_get_file_content(expanded, NULL) ) == NULL ) 
        goto error_out;

    ret = js_char_to_value(ctx, content);

error_out:
    g_free(path);
    g_free(content);
    if (ret == NULL)
        return NIL;
    return ret;

}/*}}}*/

/* io_notify {{{*/
/**
 * Show a notification in the browser window
 *
 * @name notify 
 * @memberOf io
 * @function 
 *
 * @param {String} message The message
 * */

static JSValueRef 
io_notify(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
        return NULL;

    char *message = js_value_to_char(ctx, argv[0], -1, exc);
    if (message != NULL) 
    {
        message = util_strescape_char(message, '%', '%');
        dwb_set_normal_message(dwb.state.fview, true, message);
        g_free(message);
    }
    return NULL;
}/*}}}*/

/**
 * Show an error message in the browser window
 *
 * @name error 
 * @memberOf io
 * @function 
 *
 * @param {String} message The error message
 * */
/* io_error {{{*/
static JSValueRef 
io_error(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
        return NULL;

    char *message = js_value_to_char(ctx, argv[0], -1, exc);
    if (message != NULL) 
    {
        message = util_strescape_char(message, '%', '%');
        dwb_set_error_message(dwb.state.fview, message);
        g_free(message);
    }
    return NULL;
}/*}}}*/

/**
 * Get directory entries
 *
 * @name dirnames 
 * @memberOf io
 * @function 
 *
 * @param {String} path A path to a directory
 *
 * @returns {Array[String]}
 *      An array of file names
 * */
static JSValueRef 
io_dir_names(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
        return NIL;

    char expanded[4096];
    JSValueRef ret = NIL;
    GDir *dir;
    char *dir_name = js_value_to_char(ctx, argv[0], PATH_MAX, exc);
    const char *name;

    if (dir_name == NULL)
        return NIL;
    if (util_expand_home(expanded, dir_name, sizeof(expanded)) == NULL)
    {
        js_make_exception(ctx, exc, EXCEPTION("Filename too long"));
        goto error_out;
    }

    if ((dir = g_dir_open(expanded, 0, NULL)) != NULL) 
    {
        GSList *list = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) 
        {
            list = g_slist_prepend(list, (gpointer)js_char_to_value(ctx, name));
        }
        g_dir_close(dir);

        JSValueRef args[g_slist_length(list)];

        int i=0;
        for (GSList *l = list; l; l=l->next, i++) 
            args[i] = l->data;

        ret = JSObjectMakeArray(ctx, i, args, exc);
        g_slist_free(list);
    }
    else 
        ret = NIL;

error_out:
    g_free(dir_name);
    return ret;
}
/* io_write {{{*/
/** 
 * Write to a file 
 *
 * @name write
 * @memberOf io
 * @function
 *
 * @param {String} path Path to a file to write to
 * @param {String} mode Either <i>"a"</i> to append or <i>"w"</i> to strip the file
 * @param {String} text The text that should be written to the file
 *
 * @returns {Boolean}
 *      true if writing was successful
 * */
static JSValueRef 
io_write(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char expanded[4096];
    gboolean ret = false;
    FILE *f;
    char *path = NULL, *content = NULL, *mode = NULL;
    if (argc < 3) 
    {
        js_make_exception(ctx, exc, EXCEPTION("io.write needs 3 arguments."));
        return JSValueMakeBoolean(ctx, false);
    }

    if ( (path = js_value_to_char(ctx, argv[0], PATH_MAX, exc)) == NULL )
        goto error_out;

    if ( (mode = js_value_to_char(ctx, argv[1], -1, exc)) == NULL )
        goto error_out;

    if (g_strcmp0(mode, "w") && g_strcmp0(mode, "a")) 
    {
        js_make_exception(ctx, exc, EXCEPTION("io.write: invalid mode."));
        goto error_out;
    }
    if ( (content = js_value_to_char(ctx, argv[2], -1, exc)) == NULL ) 
        goto error_out;

    if (util_expand_home(expanded, path, sizeof(expanded)) == NULL)
    {
        js_make_exception(ctx, exc, EXCEPTION("Filename too long"));
        goto error_out;
    }
    if ( (f = fopen(expanded, mode)) != NULL) 
    {
        fputs(content, f);
        fclose(f);
        ret = true;
    }
    else 
        js_make_exception(ctx, exc, EXCEPTION("io.write: cannot open %s for writing."), path);

error_out:
    g_free(path);
    g_free(mode);
    g_free(content);
    return JSValueMakeBoolean(ctx, ret);
}/*}}}*/


/* io_print {{{*/
/** 
 * Print messages to stdout
 *
 * @name out
 * @memberOf io
 * @function
 *
 * @param {String}  text  
 *      The text to print
 * @param {String}  ...   
 * 
 * */
static JSValueRef 
io_out(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return term_print(ctx, stdout, argc, argv, exc);
}/*}}}*/
/*}}}*/
/** 
 * Print messages to stderr
 *
 * @name err
 * @memberOf io
 * @function
 *
 * @param {String}  text  
 *      The text to print
 * @param {String}  ...   
 * 
 * */
static JSValueRef 
io_err(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return term_print(ctx, stderr, argc, argv, exc);
}/*}}}*/

JSObjectRef 
io_initialize(JSContextRef ctx) {
    /**
     * Static object for input and output
     *
     * @namespace 
     *      Static object for input and output and file operations
     * @name io
     * @static
     * @example
     * //!javascript
     *
     * var io = namespace("io");
     * */

    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSStaticFunction io_functions[] = { 
        { "out",       io_out,              kJSDefaultAttributes },
        { "err",       io_err,              kJSDefaultAttributes },
        { "prompt",    io_prompt,           kJSDefaultAttributes },
        { "confirm",   io_confirm,          kJSDefaultAttributes },
        { "read",      io_read,             kJSDefaultAttributes },
        { "write",     io_write,            kJSDefaultAttributes },
        { "dirNames",  io_dir_names,        kJSDefaultAttributes },
        { "notify",    io_notify,           kJSDefaultAttributes },
        { "error",     io_error,            kJSDefaultAttributes },
        { 0,           0,           0 },
    };
    JSClassRef klass = scripts_create_class("io", io_functions, NULL, NULL);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSPropertyAttributeDontDelete, "io", NULL);
    JSClassRelease(klass);
    return ret;
}
