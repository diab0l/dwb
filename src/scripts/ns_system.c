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

#define SYSTEM_CHANNEL_OUT (0)
#define SYSTEM_CHANNEL_ERR (1)

typedef struct SpawnData_s {
    GIOChannel *channel;
    JSObjectRef callback;
    JSObjectRef deferred;
    GMutex mutex;
    int type;
    guint source;
} SpawnData;

#define G_FILE_TEST_VALID (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_EXECUTABLE | G_FILE_TEST_EXISTS) 

static char **
get_environment(JSContextRef ctx, JSValueRef v, JSValueRef *exc)
{
    js_property_iterator iter;
    char *name = NULL, *value = NULL;
    JSValueRef current;

    char **envp = g_get_environ();
    if (JSValueIsNull(ctx, v) || JSValueIsUndefined(ctx, v))
        return envp;
    JSObjectRef o = JSValueToObject(ctx, v, exc);
    if (o)
    {
        js_property_iterator_init(ctx, &iter, o);
        while ((current = js_property_iterator_next(&iter, NULL, &name, exc)) != NULL)
        {
            if (name) 
            {
                value = js_value_to_char(ctx, current, -1, exc);
                if (value)
                {
                    envp = g_environ_setenv(envp, name, value, true);
                    g_free(value);
                }
                g_free(name);
            }
        }
        js_property_iterator_finish(&iter);
    }
    return envp;
}
static gboolean
spawn_output(GIOChannel *channel, GIOCondition condition, SpawnData *data) 
{
    char *content = NULL; 
    gsize length;
    int status;

    if (!g_mutex_trylock(&data->mutex)) {
        return true;
    }
    status = g_io_channel_read_line(channel, &content, &length, NULL, NULL);
    if (status == G_IO_STATUS_NORMAL && content != NULL)
    {
        JSContextRef ctx = scripts_get_global_context();
        if (ctx != NULL) {
            JSValueRef argv[] = { js_char_to_value(ctx, content) };
            scripts_call_as_function(ctx, data->callback, data->callback, 1, argv);
            scripts_release_global_context();
        }
    }
    g_free(content);
    g_mutex_unlock(&data->mutex);
    return true;
}


static void 
spawn_finish_data(SpawnData *data, int success)
{
    gchar *content = NULL;
    gsize l = 0;
    g_mutex_lock(&data->mutex);

    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        if (data->callback != NULL && data->channel != NULL) {
            // read remaining data
            GIOStatus status = g_io_channel_read_to_end(data->channel, &content, &l, NULL);
            if (status == G_IO_STATUS_NORMAL && content != NULL) {
                JSValueRef argv[] = { js_char_to_value(ctx, content) };
                scripts_call_as_function(ctx, data->callback, data->callback, 1, argv);
            }
            g_free(content);
        }
        if (!deferred_fulfilled(data->deferred)) {
            if (data->type == SYSTEM_CHANNEL_OUT && success == 0) {
                JSValueRef argv[] = { JSValueMakeNumber(ctx, success) };
                deferred_resolve(ctx, data->deferred, data->deferred, 1, argv, NULL);
            }
            else if (data->type == SYSTEM_CHANNEL_ERR && success != 0) {
                JSValueRef argv[] = { JSValueMakeNumber(ctx, success) };
                deferred_reject(ctx, data->deferred, data->deferred, 1, argv, NULL);
            }
        }
        if (data->callback != NULL) {
            JSValueUnprotect(ctx, data->callback);
        }
        scripts_release_global_context();
    }

    if (data->source != 0) {
        g_source_remove(data->source);
    }
    if (data->channel != NULL) {
        g_io_channel_shutdown(data->channel, true, NULL);
        g_io_channel_unref(data->channel);
    }

    g_mutex_unlock(&data->mutex);
    g_mutex_clear(&data->mutex);
    g_free(data);
}
static void 
watch_spawn(GPid pid, gint status, SpawnData **data)
{
    int fail = 0;

    if (WIFEXITED(status) != 0) 
        fail = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        fail = WTERMSIG(status);
    else if (WIFSTOPPED(status)) 
        fail = WSTOPSIG(status);
    g_spawn_close_pid(pid);

    spawn_finish_data(data[SYSTEM_CHANNEL_OUT], fail);
    spawn_finish_data(data[SYSTEM_CHANNEL_ERR], fail);

    g_free(data);
}

static SpawnData *
initialize_channel(JSContextRef ctx, JSObjectRef callback, JSObjectRef deferred, int fd, int type) {
    SpawnData *data = g_malloc(sizeof(SpawnData));

    data->deferred  = deferred;
    data->type      = type;
    g_mutex_init(&data->mutex);

    if (callback != NULL) {
        data->callback = callback;
        JSValueProtect(ctx, callback);

        data->channel = g_io_channel_unix_new(fd);
        data->source = g_io_add_watch(data->channel, G_IO_IN, (GIOFunc)spawn_output, data);
        g_io_channel_set_flags(data->channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_close_on_unref(data->channel, true);
    }
    else {
        data->callback = NULL; 
        data->channel = NULL;
        data->source = 0;
    }
    return data;
}


/** 
 * Gets an environment variable
 * 
 * @name getEnv 
 * @memberOf system
 * @function 
 *
 * @param {String} name The name of the environment variable
 *
 * @returns {String}
 *      The environment variable or <i>null</i> if the variable wasn't found
 * */
static JSValueRef 
system_get_env(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
        return NIL;

    char *name = js_value_to_char(ctx, argv[0], -1, exc);
    if (name == NULL) 
        return NIL;

    const char *env = g_getenv(name);
    g_free(name);

    if (env == NULL)
        return NIL;

    return js_char_to_value(ctx, env);
}
/** 
 * Sets an environment variable
 * 
 * @name setEnv 
 * @memberOf system
 * @function 
 *
 * @param {String} name         The name of the environment variable
 * @param {String} value        The value of the environment variable
 * @param {String} [overwrite]  Whether to overwrite the variable if it exists, default true
 *
 * */
static JSValueRef 
system_set_env(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2) 
        return NULL;

    char *name = js_value_to_char(ctx, argv[0], -1, exc);
    char *value = js_value_to_char(ctx, argv[1], -1, exc);
    gboolean overwrite = true;
    if (name == NULL || value == NULL) 
        goto error_out;

    if (argc > 2) {
        overwrite = JSValueToBoolean(ctx, argv[2]);
    }
    g_setenv(name, value, overwrite);

error_out:
    g_free(name);
    g_free(value);

    return NULL;
}

/** 
 * Gets the process if of the dwb instance
 * 
 * @name getPid 
 * @memberOf system
 * @function 
 *
 * @returns {Number}
 *      The process id of the dwb instance 
 * */
static JSValueRef 
system_get_pid(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return JSValueMakeNumber(ctx, getpid());
}

/** 
 * Spawn a process synchronously. This function should be used with care. The
 * execution is single threaded so longer running processes will block the whole
 * execution
 * 
 * @name spawnSync 
 * @memberOf system
 * @function 
 *
 * @param {String} command The command to execute
 * @param {Object}   [environ] Object that can be used to add environment
 *                             variables to the childs environment
 *
 * @returns {Object}
 *      An object that contains <i>stdout</i>, <i>stderr</i> and the <i>status</i> code.
 * */
static JSValueRef 
system_spawn_sync(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc<1) 
        return NIL;

    JSObjectRef ret = NULL;
    int srgc, status;
    char **srgv = NULL, *command = NULL, *out, *err;
    char **envp = NULL;

    command = js_value_to_char(ctx, argv[0], -1, exc);
    if (command == NULL) 
        return NIL;
    if (argc > 1)
        envp = get_environment(ctx, argv[1], exc);

    if (g_shell_parse_argv(command, &srgc, &srgv, NULL) && 
            g_spawn_sync(NULL, srgv, envp, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &err, &status, NULL)) 
    {
        ret = JSObjectMake(ctx, NULL, NULL);
        js_set_object_property(ctx, ret, "stdout", out, exc);
        js_set_object_property(ctx, ret, "stderr", err, exc);
        js_set_object_number_property(ctx, ret, "status", status, exc);
    }
    g_free(command);
    g_strfreev(srgv);
    g_strfreev(envp);

    if (ret == NULL)
        return NIL;

    return ret;
}/*}}}*/

/* system_spawn {{{*/
/**  
 * For internal usage only
 *
 * @name _spawn
 * @memberOf system
 * @function
 * @private
 * */
/** 
 * Spawn a process asynchronously
 * 
 * @name spawn 
 * @memberOf system
 * @function 
 * @example 
 * // Simple spawning without using stdout/stderr
 * system.spawn("foo");
 * // Using stdout/stderr
 * system.spawn("foo", {
 *      cacheStdout : true, 
 *      cacheStderr : true, 
 *      onFinished : function(result) {
 *          io.out("Process terminated with status " + result.status);
 *          io.out("Stdout is :" + result.stdout);
 *          io.out("Stderr is :" + result.stderr);
 *      }
 * });
 * // Equivalently using the deferred
 * system.spawn("foo", { cacheStdout : true, cacheStderr : true }).always(function(result) {
 *      io.out("Process terminated with status " + result.status);
 *      io.out("Stdout is :" + result.stdout);
 *      io.out("Stderr is :" + result.stderr);
 * });
 * // Only use stdout if the process terminates successfully
 * system.spawn("foo", { cacheStdout : true }).done(function(result) {
 *     io.out(result.stdout);
 * });
 * // Only use stderr if the process terminates with an error
 * system.spawn("foo", { cacheStderr : true }).fail(function(result) {
 *     io.out(result.stderr);
 * });
 * // Using environment variables with automatic caching
 * system.spawn('sh -c "echo $foo"', {
 *      onStdout : function(stdout) {
 *          io.out("stdout: " + stdout);
 *      },
 *      environment : { foo : "bar" }
 * }).then(function(result) { 
 *      io.out(result.stdout); 
 * });
 *
 *
 * @param {String} command The command to execute
 * @param {Object} [options] 
 * @param {Function} [options.onStdout] 
 *     A callback function that is called when a line from
 *     stdout was read. The function takes one parameter, the line that has been read.
 *     To get the complete stdout use either <b>onFinished</b> or the
 *     Deferred returned from <i>system.spawn</i>.
 * @param {Boolean} [options.cacheStdout] 
 *     Whether stdout should be cached. If <b>onStdout</b> is defined
 *     stdout will be cached automatically. If it isn't defined and stdout is
 *     required in <b>onFinished</b> or in the Deferred's resolve
 *     function cacheStdout must be set to true, default: false.
 * @param {Function} [options.onStderr] 
 *     A callback function that is called when a line from
 *     stderr was read. The function takes one parameter, the line that has been read. 
 *     To get the complete stderr use either <b>onFinished</b> or the
 *     Deferred returned from <i>system.spawn</i>.
 * @param {Boolean} [options.cacheStderr] 
 *     Whether stderr should be cached. If <b>onStderr</b> is defined
 *     stderr will be cached automatically. If it isn't defined and stderr is
 *     required in <b>onFinished</b> or in the Deferred's reject
 *     function cacheStderr must be set to true, default: false.
 * @param {Function} [options.onFinished] 
 *     A callback that will be called when the child process has terminated. The
 *     callback takes one argument, an object that contains stdout if caching of
 *     stdout is enabled (see <b>cacheStdout</b>), stderr if caching of stderr
 *     is enabled (see <b>cacheStderr</b>) and status, i.e. the return code of
 *     the child process.
 * @param {String} [options.stdin] 
 *     String that will be piped to stdin of the child process. 
 * @param {Object} [options.environment] 
 *     Hash of environment variables that will be set in the childs environment
 *
 * @returns {Deferred}
 *      A deferred, it will be resolved if the child exits normally, it will be
 *      rejected if the child process exits abnormally. The argument passed to
 *      resolve and reject is an Object containing stdout if caching of stdout
 *      is enabled (see <b>cacheStdout</b>), stderr if caching of stderr is enabled (see 
        <b>cacheStderr</b>) and status, i.e. the return code of the child process.
 * */
static JSValueRef 
system_spawn(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    int outfd, errfd, infd = -1;
    char **srgv = NULL, *cmdline = NULL;
    char **envp = NULL;
    int srgc;
    JSObjectRef oc = NULL, ec = NULL;
    SpawnData **data;
    GPid pid;
    char *pipe_stdin = NULL;
    gint spawn_options = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;


    if (argc == 0) 
        return NIL;

    cmdline = js_value_to_char(ctx, argv[0], -1, exc);
    if (cmdline == NULL) 
        goto error_out;

    if (argc > 1 && !JSValueIsNull(ctx, argv[1])) 
        oc = js_value_to_function(ctx, argv[1], NULL);
    
    if (argc > 2 && !JSValueIsNull(ctx, argv[2])) 
        ec = js_value_to_function(ctx, argv[2], NULL);

    if (argc > 3)
        pipe_stdin = js_value_to_char(ctx, argv[3], -1, exc);

    if (argc > 4)
        envp = get_environment(ctx, argv[4], exc);

    if (oc == NULL) {
        spawn_options |= G_SPAWN_STDOUT_TO_DEV_NULL;
    }
    if (ec == NULL) {
        spawn_options |= G_SPAWN_STDERR_TO_DEV_NULL;
    }

    if (!g_shell_parse_argv(cmdline, &srgc, &srgv, NULL) || 
            !g_spawn_async_with_pipes(NULL, srgv, envp, spawn_options,
                NULL, NULL, &pid,  
                //NULL,
                pipe_stdin != NULL ? &infd : NULL,
                oc != NULL ? &outfd : NULL, 
                ec != NULL ? &errfd : NULL, NULL)) 
    {
        js_make_exception(ctx, exc, EXCEPTION("spawning %s failed."), cmdline);
        goto error_out;
    }

    JSObjectRef deferred = deferred_new(ctx);

    data = g_malloc_n(2, sizeof(SpawnData*));

    data[SYSTEM_CHANNEL_OUT] = initialize_channel(ctx, oc, deferred, outfd, SYSTEM_CHANNEL_OUT);
    data[SYSTEM_CHANNEL_ERR] = initialize_channel(ctx, ec, deferred, errfd, SYSTEM_CHANNEL_ERR);

    if (pipe_stdin != NULL && infd != -1)
    {
        if (write(infd, pipe_stdin, strlen(pipe_stdin)) == -1 || write(infd, "\n", 1) == -1)
            perror("system.spawn");
    }
    if (infd != -1)
        close(infd);

    g_child_watch_add(pid, (GChildWatchFunc)watch_spawn, data);
    ret = deferred;

error_out:
    g_free(pipe_stdin);
    g_strfreev(envp);
    g_free(cmdline);
    g_strfreev(srgv);

    return ret;
}/*}}}*/

/**
 * Quotes a string for usage with a shell
 *
 * @name shellQuote
 * @memberOf system
 * @function
 * @since 1.11
 *
 * @param {String} unquoted An unquoted String.
 *
 * @returns {String} 
 *      The quoted string
 * */
static JSValueRef 
system_shell_quote(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    char *unquoted = NULL, *quoted = NULL;
    if (argc > 0) {
        unquoted = js_value_to_char(ctx, argv[0], -1, exc);
        if (unquoted != NULL) {
            quoted = g_shell_quote(unquoted);
            ret = js_char_to_value(ctx, quoted);

            g_free(quoted);
            g_free(unquoted);
        }
    }
    return ret;
}
/**
 * Unquotes a quoted string as /bin/sh would unquote it.
 *
 * @name shellUnquote
 * @memberOf system
 * @function
 * @since 1.11
 *
 * @param {String} quoted A quoted String.
 *
 * @returns {String} 
 *      The unquoted string
 * */
static JSValueRef 
system_shell_unquote(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    char *unquoted = NULL, *quoted = NULL;
    if (argc > 0) {
        quoted = js_value_to_char(ctx, argv[0], -1, exc);
        if (quoted != NULL) {
            unquoted = g_shell_unquote(quoted, NULL);
            if (unquoted != NULL) {
                ret = js_char_to_value(ctx, unquoted);
                g_free(unquoted);
            }
            g_free(quoted);
        }
    }
    return ret;
}

/** 
 * Creates a directory and all parent directories
 * 
 * @name mkdir 
 * @memberOf system
 * @function 
 *
 * @param {Path} path Path to create
 * @param {Number} mode The permissions the directory will get
 *
 * @returns {Boolean}
 *      <i>true</i> if creation was successful or if the diretory already
 *      existed
 * */
static JSValueRef 
system_mkdir(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char expanded[4096];
    gboolean ret = false;
    if (argc < 2) 
    {
        js_make_exception(ctx, exc, EXCEPTION("system.mkdir needs an argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    char *path = js_value_to_char(ctx, argv[0], PATH_MAX, exc);
    double mode = JSValueToNumber(ctx, argv[1], exc);
    if (path != NULL && !isnan(mode)) 
    {
        if (util_expand_home(expanded, path, sizeof(expanded)) == NULL)
        {
            js_make_exception(ctx, exc, EXCEPTION("Filename too long"));
            goto error_out;
        }

        ret = g_mkdir_with_parents(expanded, (gint)mode) == 0;
    }
error_out:
    g_free(path);
    return JSValueMakeBoolean(ctx, ret);

}

/** 
 * Checks for existence of a file or directory
 * 
 * @name fileTest 
 * @memberOf system
 * @function 
 *
 * @param {String} path Path to a file to check
 * @param {FileTest} flags 
 *      Bitmask of {@link Enums and Flags.FileTest|FileTest} flags
 *
 * @returns {Boolean}
 *      <i>true</i> if any of the flags is set
j * */
static JSValueRef 
system_file_test(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2) 
    {
        js_make_exception(ctx, exc, EXCEPTION("system.fileTest needs an argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    char *path = js_value_to_char(ctx, argv[0], PATH_MAX, exc);
    if (path == NULL) 
        return JSValueMakeBoolean(ctx, false);

    double test = JSValueToNumber(ctx, argv[1], exc);
    if (isnan(test) || ! ( (((guint)test) & G_FILE_TEST_VALID) == (guint)test) ) 
        return JSValueMakeBoolean(ctx, false);

    gboolean ret = g_file_test(path, (GFileTest) test);
    g_free(path);
    return JSValueMakeBoolean(ctx, ret);
}/*}}}*/

JSObjectRef 
system_initialize(JSContextRef ctx) {
    /**
     * Static object for system functions
     * 
     * @namespace 
     *      Static object for system functions such as spawning processes,
     *      getting environment variables
     * @name system
     * @static
     * @example 
     * //!javascript
     *
     * var system = namespace("system");
     * */

    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    JSStaticFunction system_functions[] = { 
        { "_spawn",          system_spawn,           kJSDefaultAttributes },
        { "spawnSync",       system_spawn_sync,        kJSDefaultAttributes },
        { "getEnv",          system_get_env,           kJSDefaultAttributes },
        { "setEnv",          system_set_env,           kJSDefaultAttributes },
        { "getPid",          system_get_pid,           kJSDefaultAttributes },
        { "fileTest",        system_file_test,        kJSDefaultAttributes },
        { "mkdir",           system_mkdir,            kJSDefaultAttributes },
        { "shellQuote",      system_shell_quote,      kJSDefaultAttributes },
        { "shellUnquote",    system_shell_unquote,      kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("system", system_functions, NULL, NULL);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "system", NULL);
    JSClassRelease(klass);
    return ret;
}
