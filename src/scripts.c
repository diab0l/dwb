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

#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200112L

#include "scripts/private.h"

#define API_VERSION 1.11

//#define kJSDefaultFunction  (kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete )

#define SCRIPT_TEMPLATE_START "try{_initNewContext(this,arguments,'%s');const script=this;var exports=null;/*<dwb*/"
// FIXME: xgettext, xinclude properly defined
#define SCRIPT_TEMPLATE_XSTART "try{"\
"var exports=arguments[0];"\
"_initNewContext(this,arguments,'%s');"\
"var xinclude=_xinclude.bind(this,this.path);"\
"var xgettext=_xgettext.bind(this,this.path);"\
"const script=this;"\
"if(!exports.id)Object.defineProperty(exports,'id',{value:script.generateId()});"\
"var xprovide=function(n,m,o){provide(n+exports.id,m,o);};"\
"var xrequire=function(n){return require(n+exports.id);};/*<dwb*/"

#define SCRIPT_TEMPLATE_END "%s/*dwb>*/}catch(e){script.debug(e);} if(exports && !exports.id) return exports;"

#define SCRIPT_TEMPLATE SCRIPT_TEMPLATE_START"//!javascript\n"SCRIPT_TEMPLATE_END
#define SCRIPT_TEMPLATE_INCLUDE SCRIPT_TEMPLATE_START SCRIPT_TEMPLATE_END
#define SCRIPT_TEMPLATE_XINCLUDE SCRIPT_TEMPLATE_XSTART SCRIPT_TEMPLATE_END

#define SCRIPT_WEBVIEW(o) (WEBVIEW(((GList*)JSObjectGetPrivate(o))))

#define TRY_CONTEXT_LOCK (pthread_rwlock_tryrdlock(&s_context_lock) == 0)
#define CONTEXT_UNLOCK (pthread_rwlock_unlock(&s_context_lock))
#define EXEC_LOCK if (TRY_CONTEXT_LOCK) { if (s_ctx != NULL && s_ctx->global_context != NULL) { 
#define EXEC_UNLOCK } CONTEXT_UNLOCK; }
#define EXEC_LOCK_RETURN(result) if (!TRY_CONTEXT_LOCK) return result; { if (s_ctx != NULL && s_ctx->global_context != NULL) { 



#define UNPROTECT_0(ctx, o) ((o) = (o) == NULL ? NULL : (JSValueUnprotect((ctx), (o)), NULL))

#define SCRIPTS_SIG_PROTECTED (SCRIPTS_SIG_EXECUTE_COMMAND | \
        SCRIPTS_SIG_CHANGE_MODE | \
        SCRIPTS_SIG_KEY_PRESS | \
        SCRIPTS_SIG_KEY_RELEASE | \
        SCRIPTS_SIG_BUTTON_PRESS | \
        SCRIPTS_SIG_KEY_RELEASE)

#define PROTECT_SIGNAL(sig) (s_signal_locks |= ((sig) & SCRIPTS_SIG_PROTECTED))
#define UNPROTECT_SIGNAL(sig) (s_signal_locks &= ~((sig) & SCRIPTS_SIG_PROTECTED))
#define SIGNAL_IS_PROTECTED(sig) (s_signal_locks & ((sig) & SCRIPTS_SIG_PROTECTED))

static pthread_rwlock_t s_context_lock = PTHREAD_RWLOCK_INITIALIZER;

typedef struct SigData_s {
    gulong id; 
    GObject *instance;
} SigData;

static const char  *s_sigmap[SCRIPTS_SIG_LAST] = {
    [SCRIPTS_SIG_NAVIGATION]        = "navigation", 
    [SCRIPTS_SIG_LOAD_STATUS]       = "loadStatus", 
    [SCRIPTS_SIG_MIME_TYPE]         = "mimeType", 
    [SCRIPTS_SIG_DOWNLOAD]          = "download", 
    [SCRIPTS_SIG_DOWNLOAD_START]    = "downloadStart", 
    [SCRIPTS_SIG_DOWNLOAD_STATUS]   = "downloadStatus", 
    [SCRIPTS_SIG_RESOURCE]          = "resource", 
    [SCRIPTS_SIG_KEY_PRESS]         = "keyPress", 
    [SCRIPTS_SIG_KEY_RELEASE]       = "keyRelease", 
    [SCRIPTS_SIG_BUTTON_PRESS]      = "buttonPress", 
    [SCRIPTS_SIG_BUTTON_RELEASE]    = "buttonRelease", 
    [SCRIPTS_SIG_TAB_FOCUS]         = "tabFocus", 
    [SCRIPTS_SIG_FRAME_STATUS]      = "frameStatus", 
    [SCRIPTS_SIG_LOAD_FINISHED]     = "loadFinished", 
    [SCRIPTS_SIG_LOAD_COMMITTED]    = "loadCommitted", 
    [SCRIPTS_SIG_CLOSE_TAB]         = "closeTab", 
    [SCRIPTS_SIG_CREATE_TAB]        = "createTab", 
    [SCRIPTS_SIG_FRAME_CREATED]     = "frameCreated", 
    [SCRIPTS_SIG_CLOSE]             = "close", 
    [SCRIPTS_SIG_DOCUMENT_LOADED]   = "documentLoaded", 
    [SCRIPTS_SIG_MOUSE_MOVE]        = "mouseMove", 
    [SCRIPTS_SIG_STATUS_BAR]        = "statusBarChange", 
    [SCRIPTS_SIG_TAB_BUTTON_PRESS]  = "tabButtonPress", 
    [SCRIPTS_SIG_CHANGE_MODE]       = "changeMode", 
    [SCRIPTS_SIG_EXECUTE_COMMAND]   = "executeCommand", 
    [SCRIPTS_SIG_CONTEXT_MENU]      = "contextMenu", 
    [SCRIPTS_SIG_ERROR]             = "error", 
    [SCRIPTS_SIG_SCROLL]            = "scroll", 
    [SCRIPTS_SIG_FOLLOW]            = "followHint", 
    [SCRIPTS_SIG_ADD_COOKIE]        = "addCookie", 
    [SCRIPTS_SIG_FILE_CHOOSER]      = "chooseFile", 
    [SCRIPTS_SIG_READY]             = "ready", 
};


static JSObjectRef make_boxed(gpointer boxed, JSClassRef klass);


/* Static variables */
static ScriptContext *s_ctx;
static GSList *s_autoloaded_extensions;
static uint64_t s_signal_locks;
static JSValueRef UNDEFINED, s_nil;

/* Only defined once */

/* MISC {{{*/
/* uncamelize {{{*/
char *
uncamelize(char *uncamel, const char *camel, char rep, size_t length) 
{
    char *ret = uncamel;
    size_t written = 0;
    if (isupper(*camel) && length > 1) 
    {
        *uncamel++ = tolower(*camel++);
        written++;
    }
    while (written++<length-1 && *camel != 0) 
    {
        if (isupper(*camel)) 
        {
            *uncamel++ = rep;
            if (++written >= length-1)
                break;
            *uncamel++ = tolower(*camel++);
        }
        else 
            *uncamel++ = *camel++;
    }
    *uncamel = 0;
    return ret;
}/*}}}*/



char * 
scripts_get_body(JSContextRef ctx, JSObjectRef func, JSValueRef *exc)
{
    char *sfunc = NULL, *result = NULL;
    const char *start, *end;
    JSStringRef js_string;
    if (func == NULL)
        return NULL;

    js_string = JSValueToStringCopy(ctx, func, exc);
    sfunc = js_string_to_char(ctx, js_string, -1);
    if (!sfunc)
        goto error_out;

    start = strchr(sfunc, '{');
    end = strrchr(sfunc, '}');
    if (!start || !end || start == end)
        goto error_out;
    if (start)
        start++;
    
    // Skip first empty line, needed for correct line numbers
    for (; *start && g_ascii_isspace(*start) && *start != '\n'; start++)
        ;
    if (*start == '\n')
        start++;

    result = g_strndup(start, end - start);

error_out:
    g_free(sfunc);
    JSStringRelease(js_string);
    return result;
}

ScriptContext * 
script_context_new() {
    ScriptContext *ctx = g_malloc0(sizeof(ScriptContext));

    ctx->gobject_signals = g_ptr_array_new();
    ctx->exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)scripts_unprotect);

    ctx->keymap_dirty = false;
    ctx->ref_quark = g_quark_from_static_string("dwb_js_ref");

    return ctx;
}

JSValueRef 
scripts_get_nil() {
    return s_nil;
}
void 
script_context_free(ScriptContext *ctx) {
    if (ctx != NULL) {
        if (ctx->gobject_signals != NULL) {
            for (guint i=0; i<ctx->gobject_signals->len; i++)
            {
                SigData *data = g_ptr_array_index(ctx->gobject_signals, i);
                g_signal_handler_disconnect(data->instance, data->id);
                g_free(data);
            }
            g_ptr_array_free(ctx->gobject_signals, false);
            ctx->gobject_signals = NULL;
        }
        if (ctx->exports != NULL) {
            g_hash_table_unref(ctx->exports);
            ctx->exports = NULL;
        }
        if (ctx->script_list != NULL) {
            g_slist_free(ctx->script_list);
            ctx->script_list = NULL;
        }
        if (ctx->timers != NULL) {
            for (GSList *timer = ctx->timers; timer; timer=timer->next)
                g_source_remove(GPOINTER_TO_INT(timer->data));
            g_slist_free(ctx->timers);
            ctx->timers = NULL;
        }
        for (GSList *l = s_ctx->created_widgets; l; l=l->next) {
            GtkWidget *w = l->data;
            JSObjectRef ref = g_object_steal_qdata(G_OBJECT(w), s_ctx->ref_quark);
            if (ref != NULL) {
                JSObjectSetPrivate(ref, NULL);
            }
            gtk_widget_destroy(w);
        }

        if (s_ctx->global_context != NULL) {
            if (ctx->constructors != NULL) {
                for (int i=0; i<CONSTRUCTOR_LAST; i++) 
                    UNPROTECT_0(ctx->global_context, ctx->constructors[i]);
            }
            if (ctx->namespaces != NULL) {
                for (int i=0; i<NAMESPACE_LAST; i++) {
                    UNPROTECT_0(ctx->global_context, ctx->namespaces[i]);
                }
            }
            if (ctx->classes != NULL) {
                for (int i=0; i<CLASS_LAST; i++) {
                    JSClassRelease(ctx->classes[i]);
                }
            }
            UNPROTECT_0(ctx->global_context, ctx->init_before);
            UNPROTECT_0(ctx->global_context, ctx->init_after);
            UNPROTECT_0(ctx->global_context, ctx->session);
            UNPROTECT_0(ctx->global_context, s_nil);
            UNPROTECT_0(ctx->global_context, UNDEFINED);
            JSGlobalContextRelease(s_ctx->global_context);
            s_ctx->global_context = NULL;
        }
        g_free(ctx);
    }
}
ScriptContext *
scripts_get_context() {
    return s_ctx;
}
JSContextRef 
scripts_get_global_context() {
    if (pthread_rwlock_tryrdlock(&s_context_lock) == 0) {
        if (s_ctx != NULL) {
            return s_ctx->global_context;
        }
        pthread_rwlock_unlock(&s_context_lock);
    }
    return NULL;
}
void
scripts_release_global_context() {
    pthread_rwlock_unlock(&s_context_lock);
}


static void 
finalize_headers(JSObjectRef o)
{
    SoupMessageHeaders *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        soup_message_headers_free(ob);
    }
}

/* inject {{{*/
JSValueRef
inject(JSContextRef ctx, JSContextRef wctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    gboolean global = false;
    JSValueRef e = NULL;
    JSObjectRef f;
    JSStringRef script;
    char *name = NULL;
    char *body = NULL;
    double debug = -1;
    char *arg = NULL;
    char *tmp = NULL;
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("inject: missing argument"));
        return NIL;
    }
    if (argc > 1 && !JSValueIsNull(ctx, argv[1])) 
    {
        arg = js_value_to_json(ctx, argv[1], -1, 0, exc);
    }
    if (argc > 2)
        debug = JSValueToNumber(ctx, argv[2], exc);
    if (argc > 3 && JSValueIsBoolean(ctx, argv[3])) 
        global = JSValueToBoolean(ctx, argv[3]);

    if (JSValueIsObject(ctx, argv[0]) && (f = js_value_to_function(ctx, argv[0], exc)) != NULL)
    {
        body = scripts_get_body(ctx, f, exc);
        name = js_get_string_property(ctx, f, "name");
    }
    else 
    {
        body = js_value_to_char(ctx, argv[0], -1, exc);
    }
    if (body == NULL) {
        return NIL;
    }

    if (!global) {
        tmp = body;
        if (arg == NULL) {
            body = g_strdup_printf("(function() {\n%s\n}())", body);
        }
        else {
            body = g_strdup_printf("(function(exports) {\n%s\n}(%s))", body, arg);
        }
        g_free(tmp);
    }
    script = JSStringCreateWithUTF8CString(body);

    JSValueRef wret = JSEvaluateScript(wctx, script, NULL, NULL, 0, &e);
    if (!global && e == NULL) 
    {
        char *retx = js_value_to_json(wctx, wret, -1, 0, NULL);
        // This could be replaced with js_context_change
        if (retx) 
        {
            ret = js_char_to_value(ctx, retx);
            g_free(retx);
        }
    }
    if (e != NULL && !isnan(debug) && debug > 0)
    {
        int line = 0;
        fprintf(stderr, "DWB SCRIPT EXCEPTION: An error occured injecting %s.\n", name == NULL || *name == '\0' ? "[anonymous]" :  name);
        js_print_exception(wctx, e, NULL, 0, (int)(debug-2), &line);
        line--;

        fputs("==> DEBUG [SOURCE]\n", stderr);
        if (body == NULL)
            body = js_string_to_char(ctx, script, -1);
        char **lines = g_strsplit(body, "\n", -1);
        char **reallines = global ? lines : lines+1;

        fprintf(stderr, "    %s\n", line < 3 ? "BOF" : "...");
        for (int i=MAX(line-2, 0); reallines[i] != NULL && i < line + 1; i++)
            fprintf(stderr, "%s %d > %s\n", i == line-1 ? "-->" : "   ", i+ ((int) debug), reallines[i]);
        fprintf(stderr, "    %s\n", line + 2 >= (int)g_strv_length(reallines) ? "EOF" : "...");

        g_strfreev(lines);
    }
    g_free(body);
    g_free(name);
    JSStringRelease(script);
    return ret;
}/*}}}*/
/*}}}*/

/* Deferred {{{*/

void
sigdata_append(gulong sigid, GObject *instance)
{
    SigData *data = g_malloc(sizeof(SigData));
    data->id = sigid;
    data->instance = instance;
    g_ptr_array_add(s_ctx->gobject_signals, data);

}
void
sigdata_remove(gulong sigid, GObject *instance)
{
    for (guint i=0; i<s_ctx->gobject_signals->len; i++)
    {
        SigData *data = g_ptr_array_index(s_ctx->gobject_signals, i);
        if (data->id == sigid && data->instance == instance)
        {
            g_ptr_array_remove_index_fast(s_ctx->gobject_signals, i);
            g_free(data);
            return;
        }
    }
}



/* FRAMES {{{*/


static void 
unbind_free_keymap(JSContextRef ctx, GList *l)
{
    g_return_if_fail(l != NULL);
    KeyMap *m = l->data;
    JSValueUnprotect(ctx, m->map->arg.js);
    g_free(m->map->n.first);
    g_free(m->map->n.second);
    g_free(m->map);
    g_free(m);
    dwb.keymap = g_list_delete_link(dwb.keymap, l);
}


/* GLOBAL {{{*/
/** 
 * Callback that will be called when a shortcut or command was invoked that was
 * bound with {@link bind}
 *
 * @callback bindCallback
 * 
 * @param {Object} arguments 
 * @param {String} arguments.shortcut
 *      The shortcut that was pressed
 * @param {Number} arguments.modifier
 *      The modifier
 * @param {Number} arguments.nummod 
 *      Numerical modifier that was used or -1 if no modifier was used.
 * @param {Number} [arguments.arg]
 *      Argument if the callback was invoked from commandline and an argument
 *      was used on commanline
 * */
/* scripts_eval_key {{{*/
DwbStatus
scripts_eval_key(KeyMap *m, Arg *arg) 
{
    char *json = NULL;
    int nummod = dwb.state.nummod;

    if (! (m->map->prop & CP_OVERRIDE)) {
        CLEAR_COMMAND_TEXT();
        dwb_change_mode(NORMAL_MODE, true);
    }

    if (s_ctx != NULL && s_ctx->global_context != NULL)
    {
        if (arg->p == NULL) 
            json = util_create_json(3, CHAR, "key", m->key, 
                    INTEGER, "modifier", m->mod,  
                    INTEGER, "nummod", nummod);
        else 
            json = util_create_json(4, CHAR, "key", m->key, 
                    INTEGER, "modifier", m->mod,  
                    INTEGER, "nummod", nummod,
                    CHAR, "arg", arg->p);

        JSValueRef argv[] = { js_json_to_value(s_ctx->global_context, json) };
        scripts_call_as_function(s_ctx->global_context, arg->js, arg->js, 1, argv);
    }

    g_free(json);

    return STATUS_OK;
}/*}}}*/

void 
scripts_clear_keymap() {
    if (s_ctx != NULL && s_ctx->keymap_dirty) {
        EXEC_LOCK;
        GList *l, *next = dwb.keymap;
        KeyMap *km;
        while (next) {
            l = next;
            next = next->next;
            km = l->data;
            if (km->map->prop & CP_SCRIPT && km->map->arg.i == 0) {
                unbind_free_keymap(s_ctx->global_context, l);
            }
        }
        EXEC_UNLOCK;
        s_ctx->keymap_dirty = false;
    }
}


/*{{{*/
static JSValueRef
settings_get(JSContextRef ctx, JSObjectRef jsobj, JSStringRef js_name, JSValueRef *exc) 
{
    char buffer[PROP_LENGTH];
    char *name = js_string_to_char(ctx, js_name, PROP_LENGTH);
    if (name != NULL)
    {
        uncamelize(buffer, name, '-', PROP_LENGTH);
        WebSettings *s = g_hash_table_lookup(dwb.settings, buffer);
        g_free(name);
        if (s == NULL) 
            return NIL;
        switch (s->type) 
        {
            case INTEGER : 
                return JSValueMakeNumber(ctx, s->arg_local.i);
            case DOUBLE : 
                return JSValueMakeNumber(ctx, s->arg_local.d);
            case BOOLEAN : 
                return JSValueMakeBoolean(ctx, s->arg_local.b);
            case CHAR : 
            case COLOR_CHAR : 
                return js_char_to_value(ctx, s->arg_local.p);
            default : return NIL;
        }
    }
    return NIL;
}
/*}}}*/

JSValueRef 
scripts_include(JSContextRef ctx, const char *path, const char *script, gboolean global, gboolean is_archive, size_t argc, const JSValueRef *argv, JSValueRef *exc)
{
    JSStringRef js_script;
    JSValueRef ret = NIL;

    if (global)
    {
        js_script = JSStringCreateWithUTF8CString(script);
        ret = JSEvaluateScript(ctx, js_script, NULL, NULL, 0, exc);
    }
    else 
    {
        char *debug = g_strdup_printf(is_archive ? SCRIPT_TEMPLATE_XINCLUDE : SCRIPT_TEMPLATE_INCLUDE, path, script);
        js_script = JSStringCreateWithUTF8CString(debug);
        JSObjectRef function = JSObjectMakeFunction(ctx, NULL, 0, NULL, js_script, NULL, 1, exc);
        if (function != NULL) 
        {
            if (argc > 0)
            {
                js_set_property(ctx, function, "exports", argv[0], kJSDefaultAttributes, NULL);
            }
            ret = JSObjectCallAsFunction(ctx, function, function, argc, argv, exc);
        }
        g_free(debug);
    }
    JSStringRelease(js_script);
    return ret;
}
/* global_include {{{*/




/* global_send_request {{{*/

/**
 * Callback that will be called when <i>Return</i> was pressed after {@link util.tabComplete} was invoked.
 *
 * @callback util~onTabComplete 
 *
 * @param {String} text Text from the textentry. 
 * */
void 
scripts_completion_activate(void) 
{
    EXEC_LOCK;
    const char *text = GET_TEXT();
    JSValueRef val[] = { js_char_to_value(s_ctx->global_context, text) };
    completion_clean_completion(false);
    dwb_change_mode(NORMAL_MODE, true);
    scripts_call_as_function(s_ctx->global_context, s_ctx->complete, s_ctx->complete, 1, val);
    EXEC_UNLOCK;
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
    return UNDEFINED;
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
    return UNDEFINED;
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
    return UNDEFINED;
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
    return UNDEFINED;
}

/* DOWNLOAD {{{*/
#if WEBKIT_CHECK_VERSION(1, 10, 0)
/*}}}*/
static JSValueRef 
file_chooser_select(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    if (argc == 0) {
        return UNDEFINED;
    }
    WebKitFileChooserRequest *request = JSObjectGetPrivate(this);
    if (request == NULL) {
        return UNDEFINED;
    }
    char **files = g_malloc0_n(argc, sizeof(char *));

    for (size_t i=0; i<argc; i++) {
        files[i] = js_value_to_char(ctx, argv[i], -1, exc);
        if (files[i] == NULL) {
            goto error_out;
        }
    }
    webkit_file_chooser_request_select_files(request, (const gchar * const *)files);

error_out:
    if (request != NULL) {
        g_object_unref(request);
    }
    g_strfreev(files);

    return UNDEFINED;
}
#endif


JSObjectRef 
scripts_make_cookie(SoupCookie *cookie)
{
    g_return_val_if_fail(cookie != NULL || s_ctx != NULL, NULL);
    return make_boxed(cookie, s_ctx->classes[CLASS_COOKIE]);
}
static JSObjectRef 
cookie_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    g_return_val_if_fail(s_ctx != NULL, NULL);
    SoupCookie *cookie = soup_cookie_new("", "", "", "", 0);
    return JSObjectMake(ctx, s_ctx->classes[CLASS_COOKIE], cookie);
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
        return UNDEFINED;
    SoupCookie *cookie = JSObjectGetPrivate(this);
    if (cookie != NULL )
    {
        double value = JSValueToNumber(ctx, argv[0], exc);
        if (!isnan(value))
        {
            soup_cookie_set_max_age(cookie, (int) value);
        }
    }
    return UNDEFINED;
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
    return UNDEFINED;
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
    return UNDEFINED;
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
/* gui {{{*/

/* SIGNALS {{{*/
/* signal_set {{{*/
static bool
signal_set(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef value, JSValueRef* exception) 
{
    char *name = js_string_to_char(ctx, js_name, -1);
    JSObjectRef o;

    if (name == NULL)
        return false;
    if (g_str_has_prefix(name, "on") && strlen(name) > 3 && g_ascii_isupper(*(name+2)))
    {
        JSObjectRef func = js_value_to_function(ctx, value, exception);
        if (func) 
        {
            name[2] = g_ascii_tolower(name[2]);
            const char *tmp = name+2;
            JSObjectRef scripts = js_get_object_property(s_ctx->global_context, JSContextGetGlobalObject(s_ctx->global_context), "Signal");
            if (scripts)
            {
                JSObjectRef connect = js_get_object_property(s_ctx->global_context, scripts, "connect");
                if (connect)
                {
                    JSValueRef js_value = js_char_to_value(ctx, tmp);
                    JSValueRef argv[] = { js_value, func };
                    JSObjectCallAsFunction(s_ctx->global_context, connect, func, 2, argv, exception);
                }
            }

            g_free(name);
            return true;
        }
    }

    for (int i = SCRIPTS_SIG_FIRST; i<SCRIPTS_SIG_LAST; i++) 
    {
        if (strcmp(name, s_sigmap[i])) 
            continue;

        if (JSValueIsNull(ctx, value)) 
        {
            s_ctx->sig_objects[i] = NULL;
            dwb.misc.script_signals &= ~(1ULL<<i);
        }
        else if ( (o = js_value_to_function(ctx, value, exception)) != NULL) 
        {
            s_ctx->sig_objects[i] = o;
            dwb.misc.script_signals |= (1ULL<<i);
        }
        break;
    }

    g_free(name);
    return false;
}/*}}}*/

/* scripts_emit {{{*/
gboolean
scripts_emit(ScriptSignal *sig) 
{
    g_return_val_if_fail(s_ctx != NULL, false);

    int numargs, i, additional = 0;
    gboolean ret = false;
    JSObjectRef function = s_ctx->sig_objects[sig->signal];
    if (function == NULL)
        return false;

    if (SIGNAL_IS_PROTECTED(sig->signal)) 
        return false;

    EXEC_LOCK_RETURN(false);

    PROTECT_SIGNAL(sig->signal);

    if (sig->jsobj != NULL)
        additional++;
    if (sig->json != NULL)
        additional++;
    if (sig->arg != NULL)
        additional++;

    numargs = MIN(sig->numobj, SCRIPT_MAX_SIG_OBJECTS)+additional;
    JSValueRef val[numargs];
    i = 0;

    if (sig->jsobj != NULL) 
        val[i++] = sig->jsobj;

    for (int j=0; j<sig->numobj; j++) 
    {
        if (sig->objects[j] != NULL) 
            val[i++] = scripts_make_object(s_ctx->global_context, G_OBJECT(sig->objects[j]));
        else 
            val[i++] = NIL;
    }

    if (sig->json != NULL)
    {
        JSValueRef vson = js_json_to_value(s_ctx->global_context, sig->json);
        val[i++] = vson == NULL ? NIL : vson;
    }
    if (sig->arg != NULL)
    {
        switch (sig->arg->n)
        {
            case BOOLEAN : val[i++] = JSValueMakeBoolean(s_ctx->global_context, sig->arg->b); break;
            case INTEGER : val[i++] = JSValueMakeNumber(s_ctx->global_context, sig->arg->i); break;
            case DOUBLE  : val[i++] = JSValueMakeNumber(s_ctx->global_context, sig->arg->d); break;
            case CHAR    : val[i++] = js_char_to_value(s_ctx->global_context, sig->arg->p); break;
        }
    }

    JSValueRef js_ret = scripts_call_as_function(s_ctx->global_context, function, function, numargs, val);

    if (JSValueIsBoolean(s_ctx->global_context, js_ret)) 
        ret = JSValueToBoolean(s_ctx->global_context, js_ret);

    UNPROTECT_SIGNAL(sig->signal);

    EXEC_UNLOCK;

    return ret;
}/*}}}*/
/*}}}*/

/* OBJECTS {{{*/
/* scripts_make_object {{{*/

static JSObjectRef 
make_boxed(gpointer boxed, JSClassRef klass)
{
    JSObjectRef ret = NULL;

    EXEC_LOCK_RETURN(NULL);
    ret = JSObjectMake(s_ctx->global_context, klass, boxed);
    EXEC_UNLOCK;

    return ret;
}

JSObjectRef 
scripts_make_object(JSContextRef ctx, GObject *o) 
{
    if (o == NULL) 
    {
        JSValueRef v = NIL;
        return JSValueToObject(ctx, v, NULL);
    }
    JSObjectRef result;
    int iclass;
    if (WEBKIT_IS_WEB_VIEW(o)) 
        iclass = CLASS_WEBVIEW;
    else if (WEBKIT_IS_WEB_FRAME(o))
        iclass = CLASS_FRAME;
    else if (WEBKIT_IS_DOWNLOAD(o)) 
        iclass = CLASS_DOWNLOAD;
    else if (SOUP_IS_MESSAGE(o)) 
        iclass = CLASS_MESSAGE;
    else if (WEBKIT_IS_WEB_BACK_FORWARD_LIST(o))
        iclass = CLASS_HISTORY;
    else if (GTK_IS_MENU(o))
        iclass = CLASS_MENU;
    else if (GTK_IS_WIDGET(o))
        iclass = CLASS_SECURE_WIDGET;
#if WEBKIT_CHECK_VERSION(1, 10, 0)
    else if (WEBKIT_IS_FILE_CHOOSER_REQUEST(o)) {
        iclass = CLASS_FILE_CHOOSER;
        o = g_object_ref(o);
    }
#endif
    else if (WEBKIT_IS_DOM_OBJECT(o)) {
        return make_dom_object(ctx, o);
    }
    else 
        iclass = CLASS_GOBJECT;

    result =  make_object_for_class(ctx, iclass, o, true);

    return result;
}/*}}}*/


JSClassRef 
scripts_create_class(const char *name, JSStaticFunction staticFunctions[], JSStaticValue staticValues[], JSObjectGetPropertyCallback get_property_cb) 
{
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = name;
    cd.staticFunctions = staticFunctions;
    cd.staticValues = staticValues;
    cd.setProperty = set_property_cb;
    cd.getProperty = get_property_cb;
    return JSClassCreate(&cd);
}

/* scripts_create_object {{{*/
JSObjectRef 
scripts_create_object(JSContextRef ctx, JSClassRef class, JSObjectRef obj, JSClassAttributes attr, const char *name, void *private) 
{
    JSObjectRef ret = JSObjectMake(ctx, class, private);
    js_set_property(ctx, obj, name, ret, attr, NULL);
    JSValueProtect(ctx, ret);
    return ret;
}/*}}}*/

/* set_property {{{*/

/* get_property {{{*/

/* get_property {{{*/
JSObjectRef
scripts_create_constructor(JSContextRef ctx, char *name, JSClassRef class, JSObjectCallAsConstructorCallback cb, JSValueRef *exc)
{
    JSObjectRef constructor = JSObjectMakeConstructor(ctx, class, cb);
    JSStringRef js_name = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(ctx, JSContextGetGlobalObject(ctx), js_name, constructor, kJSPropertyAttributeDontDelete, exc);
    JSStringRelease(js_name);
    JSValueProtect(ctx, constructor);
    return constructor;

}
JSObjectRef
scripts_get_exports(JSContextRef ctx, const char *path)
{
    ScriptContext *sctx = scripts_get_context();
    JSObjectRef ret = g_hash_table_lookup(sctx->exports, path);
    if (ret == NULL)
    {
        ret = JSObjectMake(ctx, NULL, NULL);
        JSValueProtect(ctx, ret);
        g_hash_table_insert(sctx->exports, g_strdup(path), ret);
    }
    return ret;
}
/* create_global_object {{{*/
static void  
create_global_object() 
{
    s_ctx = script_context_new();

    pthread_rwlock_wrlock(&s_context_lock);
    JSClassDefinition cd; 
    s_ctx->ref_quark = g_quark_from_static_string("dwb_js_ref");

    JSClassRef class;

    JSGlobalContextRef ctx = global_initialize();
    s_ctx->global_context = ctx;
    JSObjectRef global_object = JSContextGetGlobalObject(s_ctx->global_context);

    /**
     * The api-version
     * @name version 
     * @readonly
     * @type Number
     * */
    js_set_object_number_property(ctx, global_object, "version", API_VERSION, NULL);

    s_ctx->namespaces[NAMESPACE_DATA]       = data_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_TIMER]      = timer_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_SYSTEM]     = system_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_IO]         = io_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_GUI]        = gui_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_TABS]       = tabs_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_UTIL]       = util_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_CLIPBOARD]  = clipboard_initialize(s_ctx->global_context);
    s_ctx->namespaces[NAMESPACE_NET]        = net_initialize(s_ctx->global_context);
#ifdef WITH_LIBSECRET
    s_ctx->namespaces[NAMESPACE_KEYRING]    = keyring_initialize(s_ctx->global_context);
#endif

    /**
     * Object that can be used to get dwb's settings, to set dwb's settings use
     * {@link execute}
     * @name settings 
     * @type Object
     * @example 
     * if (settings.enableScripts == true)
     *      execute("local_set enable-scripts false");
     * */
    cd = kJSClassDefinitionEmpty;
    cd.getProperty = settings_get;
    cd.setProperty = set_property_cb;
    class = JSClassCreate(&cd);
    scripts_create_object(ctx, class, global_object, kJSDefaultAttributes, "settings", NULL);
    JSClassRelease(class);


    /**
     * Execute code on certain events. dwb emits some signals, e.g. before a new
     * site is loaded, the signals object can be used to handle these signals.
     * All events are emitted on the signals object itself, for example 
     * "signals.keyPress = function() { ...  };" would connect to the keyPress
     * signal but it is <b>strongly discouraged</b> to use this pattern since it will
     * only allow to connect one callback to a certain signal. To handle signals
     * {@link Signal}-objects can be used, it manages signals, allows to connect
     * to more than one signal and also allows to easily disconnect/reconnect to
     * signals. 
     *
     * There is just one convenient pattern that allows setting
     * callbacks directly on signals: if the signal name starts with "on"
     * dwb will internally create a new Signal and connect to it with the given
     * callback function, i.e. using 
     * <b>signals.onResource = function () {...}</b> allows to connect more than
     * one callback to the "resource"-event, however it doesn't give you as much
     * control as creating a {@link Signal}. When connected with this pattern it
     * is <b>not</b> possible to disconnect from the signal with 
     * <b>signals.onResource  = null;</b>, instead {@link Signal.disconnect}
     * must be used.
     *
     * To connect a single webview to an event the callback can directly be set
     * on the webview, see {@link WebKitWebView} for a list of signals that can
     * be set on WebKitWebViews.  More than one callback function can be set on
     * the same webview. 
     * Note that is is only possible to disconnect from signals directly set on
     * a webview by calling <b>this.disconnect();</b> in the callback function
     * or calling <b>Signal.disconnect(callback);</b>.
     *
     * @namespace 
     * @name signals 
     * @example 
     * function onNavigation(wv, frame, request) 
     * {
     *      ... 
     *      if (request.uri == "http://www.example.com")
     *          this.disconnect();
     * }
     * // Connect to the navigation event
     * // this is the preferred way.
     * var s = new Signal("navigation", onNavigation).connect();
     * // or equivalently 
     * var signals = namespace("signals");
     * signals.onNavigation = onNavigation;
     *
     * // Connect the current tab to an event
     * tabs.current.onButtonPress = function(webview, result, event) {
     *      io.out(event);
     * }
     * // Connect the current tab once to an event
     * tabs.current.onceDocumentLoaded = function(wv) {
     *      io.out("document load finished");
     * }
     *
     *
     * */
    cd = kJSClassDefinitionEmpty;
    cd.className = "signals";
    cd.setProperty = signal_set;
    class = JSClassCreate(&cd);

    s_ctx->namespaces[NAMESPACE_SIGNALS] = JSObjectMake(ctx, class, NULL);
    JSValueProtect(ctx, s_ctx->namespaces[NAMESPACE_SIGNALS]);
    JSClassRelease(class);

    class = scripts_create_class("extensions", NULL, NULL, NULL);
    s_ctx->namespaces[NAMESPACE_EXTENSIONS] = scripts_create_object(ctx, class, global_object, kJSDefaultAttributes, "extensions", NULL);
    JSClassRelease(class);

    class = scripts_create_class("Signal", NULL, NULL, NULL);
    scripts_create_object(ctx, class, global_object, kJSPropertyAttributeDontDelete, "Signal", NULL);
    JSClassRelease(class);


    /**
     * Wrapped namespace for the console in the webcontext, will print
     * messages to the webinspector console. 
     *
     * @namespace 
     *      Wraps the webinspector console functions
     *
     * @name console 
     * @static 
     * @example
     * //!javascript
     *
     * var console = namespace("console");
     * */
    s_ctx->namespaces[NAMESPACE_CONSOLE] = scripts_create_object(ctx, NULL, global_object, kJSDefaultAttributes, "console", NULL);

    gobject_initialize(s_ctx);
    webview_initialize(s_ctx);
    frame_initialize(s_ctx);
    deferred_initialize(s_ctx);
    message_initialize(s_ctx);
    cltimer_initilize(s_ctx);
    history_initialize(s_ctx);
    widget_initialize(s_ctx);
    menu_initialize(s_ctx);
    dom_initialize(s_ctx);


#if WEBKIT_CHECK_VERSION(1, 10, 0)
    JSStaticFunction file_chooser_functions[] = { 
        { "selectFiles",          file_chooser_select,        kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitFileChooserRequest";
    cd.staticFunctions = file_chooser_functions;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_FILE_CHOOSER] = JSClassCreate(&cd);
#endif

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
    cd = kJSClassDefinitionEmpty;
    cd.staticValues = cookie_values;
    cd.staticFunctions = cookie_functions;
    cd.finalize = cookie_finalize;
    s_ctx->classes[CLASS_COOKIE] = JSClassCreate(&cd);
    s_ctx->constructors[CONSTRUCTOR_COOKIE] = scripts_create_constructor(ctx, "Cookie", s_ctx->classes[CLASS_COOKIE], cookie_constructor_cb, NULL);



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
    cd = kJSClassDefinitionEmpty;
    cd.className = "SoupHeaders";
    cd.staticFunctions = header_functions;
    cd.finalize = finalize_headers;
    s_ctx->classes[CLASS_SOUP_HEADER] = JSClassCreate(&cd);
    
    s_ctx->session = make_object_for_class(ctx, CLASS_GOBJECT, G_OBJECT(webkit_get_default_session()), false);
    JSValueProtect(ctx, s_ctx->session);

    s_ctx->global_context = ctx;

    pthread_rwlock_unlock(&s_context_lock);
}/*}}}*/
/*}}}*/

/* INIT AND END {{{*/
/* apply_scripts {{{*/
    static void 
apply_scripts() 
{
    int length = g_slist_length(s_ctx->script_list); 
    int i=0;
    JSValueRef exports[1];
    size_t argc = 0;
    char *path;

    // XXX Not needed?
    JSObjectRef *objects = g_malloc(length * sizeof(JSObjectRef));
    for (GSList *l=s_ctx->script_list; l; l=l->next, i++) 
    {
        objects[i] = JSObjectMake(s_ctx->global_context, NULL, NULL);
        js_set_property(s_ctx->global_context, objects[i], "func", l->data, 0, NULL);
    }
    if (s_ctx->init_before != NULL) 
    {
        JSValueRef argv[] = {  JSObjectMakeArray(s_ctx->global_context, length, (JSValueRef*)objects, NULL) };
        JSObjectCallAsFunction(s_ctx->global_context, s_ctx->init_before, NULL, 1, argv, NULL);
        UNPROTECT_0(s_ctx->global_context, s_ctx->init_before);
    }

    for (GSList *l = s_ctx->script_list; l; l=l->next) 
    {
        argc = 0;
        path = js_get_string_property(s_ctx->global_context, l->data, "path");
        if (path != NULL)
        {
            exports[0] = scripts_get_exports(s_ctx->global_context, path);
            argc = 1;
        }

        JSObjectCallAsFunction(s_ctx->global_context, l->data, l->data, argc, argc > 0 ? exports : NULL, NULL);
    }
    g_slist_free(s_ctx->script_list);
    s_ctx->script_list = NULL;

    if (s_ctx->init_after != NULL) 
    {
        JSObjectCallAsFunction(s_ctx->global_context, s_ctx->init_after, NULL, 0, NULL, NULL);
        UNPROTECT_0(s_ctx->global_context, s_ctx->init_after);
    }
    g_free(objects);
}/*}}}*/

/* scripts_create_tab {{{*/
void 
scripts_create_tab(GList *gl) 
{
    static gboolean applied = false;
    if (s_ctx == NULL )  
    {
        VIEW(gl)->script_wv = NULL;
        return;
    }
    if (!applied) 
    {
        apply_scripts();
        applied = true;
    }
    JSObjectRef o = scripts_make_object(s_ctx->global_context, G_OBJECT(VIEW(gl)->web));

    JSValueProtect(s_ctx->global_context, o);
    VIEW(gl)->script_wv = o;
}/*}}}*/

/* scripts_remove_tab {{{*/
void 
scripts_remove_tab(JSObjectRef obj) 
{
    if (obj == NULL) {
        return;
    }
    EXEC_LOCK;

    if (EMIT_SCRIPT(CLOSE_TAB)) 
    {
        /**
         * Emitted when a tab is closed
         * @event  closeTab
         * @memberOf signals
         * @param {signals~onCloseTab} callback 
         *      Callback function that will be called when the signal is emitted
         *
         * */
        /**
         * Callback called when a tab is closed
         * @callback signals~onCloseTab
         *
         * @param {WebKitWebView} webview The corresponding WebKitWebView
         * */
        ScriptSignal signal = { obj, SCRIPTS_SIG_META(NULL, CLOSE_TAB, 0) };
        scripts_emit(&signal);
    }
    JSValueUnprotect(s_ctx->global_context, obj);

    EXEC_UNLOCK;
}/*}}}*/

JSValueRef  
exec_namespace_function(JSContextRef ctx, const char *namespace_name, const char *function_name, size_t argc, const JSValueRef *argv)
{
    JSObjectRef global = JSContextGetGlobalObject(ctx);
    JSObjectRef namespace = js_get_object_property(ctx, global, namespace_name);

    g_return_val_if_fail(namespace != NULL, NULL);

    JSObjectRef function = js_get_object_property(ctx, namespace, function_name);

    g_return_val_if_fail(function != NULL, NULL);

    return scripts_call_as_function(ctx, function, function, argc, argv);
}

gboolean
scripts_load_chrome(JSObjectRef wv, const char *uri)
{
    g_return_val_if_fail(uri != NULL, false);
    gboolean ret = false;

    EXEC_LOCK;
    JSValueRef argv[] = { wv, js_char_to_value(s_ctx->global_context, uri) };
    JSValueRef result = exec_namespace_function(s_ctx->global_context, "extensions", "loadChrome", 2, argv);
    if (result)
    {
        ret = JSValueToBoolean(s_ctx->global_context, result);
    }
    EXEC_UNLOCK;
    return ret;
}
void
scripts_load_extension(const char *extension)
{
    g_return_if_fail(extension != NULL);

    EXEC_LOCK;
    JSValueRef argv[] = { js_char_to_value(s_ctx->global_context, extension) };
    exec_namespace_function(s_ctx->global_context, "extensions", "load", 1, argv);
    s_autoloaded_extensions = g_slist_prepend(s_autoloaded_extensions, g_strdup(extension));
    dwb_set_normal_message(dwb.state.fview, true, "Extension %s autoloaded", extension);
    EXEC_UNLOCK;
}

static char * 
init_macro(const char *content, const char *name, const char *fmt, ...) {
    va_list args;
    char *pattern = NULL, *prepared = NULL, *macro = NULL; 
    GRegex *r = NULL;

    pattern = g_strdup_printf("[^\\S\\n\\r]*__%s__[^\\S\\n\\r]*\\((.*)\\);[^\\S\\n\\r]*", name);
    r = g_regex_new(pattern, 0, 0, NULL);

    g_return_val_if_fail(r != NULL, NULL);

    va_start(args, fmt);
    prepared = g_strdup_vprintf(fmt, args);
    va_end(args);

    macro = g_regex_replace(r, content, -1, 0, prepared, 0, NULL);

    g_regex_unref(r);
    g_free(pattern);
    g_free(prepared);
    return macro;
}


void 
init_script(const char *path, const char *script, gboolean is_archive, const char *template, int offset)
{
    char *debug = NULL, *prepared = NULL, *tmp = NULL;
    if (s_ctx == NULL) 
        create_global_object();


    if (js_check_syntax(s_ctx->global_context, script, path, 2)) 
    {
        /** 
         * Prints an assertion message and removes all handles owned by the
         * script. If called in the global context of a script it stops the
         * execution of the script. Note that \__assert\__ is not actually a
         * function but a macro, a ; is mandatory at the end of an \__assert\__
         * statement.
         *
         * @name __assert__
         * @function
         *
         * @param {Expression} expression 
         *      An expression  that evaluates to true or false.
         *
         * @since 1.8
         *
         * @example 
         * //!javascript
         *
         * var a = [];
         *
         * // returns immediately
         * __assert__(a.length != 0);
         *
         * */
        prepared = init_macro(script, "assert", 
                "if(!(\\1)){try{script.removeHandles();throw new Error();}catch(e){io.debug('Assertion in %s:'+(e.line)+' failed: \\1');};return;}", 
                path);

        g_return_if_fail(prepared != NULL);

        /** 
         * Use this script only for the specified profiles. If the current profile doesn't match
         * the specified profiles the script will stop immediately. This macro
         * must be defined at the beginning of the script.  Note that
         * \__profile\__ is a macro, a ; is mandatory at the end of a
         * \__profile\__ statement.
         *
         * @name __profile__
         * @function
         *
         * @param {String...} profiles
         *      Profiles that use this script 
         *
         * @since 1.8
         *
         * @example 
         * //!javascript
         *
         * __profile__("default", "private");
         *
         * // not reached if the profile isn't default or private
         *
         * */
        tmp = prepared;
        prepared = init_macro(tmp, "profile", 
                "if(!([\\1].some(function(n) { return data.profile == n; }))){return;}");

        g_return_if_fail(prepared != NULL);

        debug = g_strdup_printf(template, path, prepared);

        JSObjectRef function = js_make_function(s_ctx->global_context, debug, path, offset);
        if (is_archive)
            js_set_object_property(s_ctx->global_context, function, "path", path, NULL);

        if (function != NULL) 
            s_ctx->script_list = g_slist_prepend(s_ctx->script_list, function);

        g_free(prepared);
        g_free(tmp);
        g_free(debug);
    }
}

/* scripts_init_script {{{*/
void
scripts_init_script(const char *path, const char *script) 
{
    init_script(path, script, false, SCRIPT_TEMPLATE, 1);
}/*}}}*/

void
scripts_init_archive(const char *path, const char *script) 
{
    init_script(path, script, true, SCRIPT_TEMPLATE_XINCLUDE, 0);
}

void
evaluate(const char *script) 
{
    EXEC_LOCK;
    JSStringRef js_script = JSStringCreateWithUTF8CString(script);
    JSEvaluateScript(s_ctx->global_context, js_script, NULL, NULL, 0, NULL);
    JSStringRelease(js_script);
    EXEC_UNLOCK;
}

JSObjectRef 
get_private(JSContextRef ctx, char *name) 
{
    JSStringRef js_name = JSStringCreateWithUTF8CString(name);
    JSObjectRef global_object = JSContextGetGlobalObject(s_ctx->global_context);

    JSObjectRef ret = js_get_object_property(s_ctx->global_context, global_object, name);
    JSValueProtect(s_ctx->global_context, ret);
    JSObjectDeleteProperty(s_ctx->global_context, global_object, js_name, NULL);

    JSStringRelease(js_name);
    return ret;
}
void
scripts_reapply()
{
    if (scripts_init(false)) 
    {
        for (GList *gl = dwb.state.views; gl; gl=gl->next)
        {
            JSObjectRef o = scripts_make_object(s_ctx->global_context, G_OBJECT(VIEW(gl)->web));
            JSValueProtect(s_ctx->global_context, o);
            VIEW(gl)->script_wv = o;
        }
        apply_scripts();
        for (GSList *l = s_autoloaded_extensions; l; l=l->next) 
        {
            scripts_load_extension(l->data);
        }
    }
}

/* scripts_init {{{*/
gboolean 
scripts_init(gboolean force) {
    dwb.misc.script_signals = 0;
    if (s_ctx == NULL) 
    {
        if (force || dwb.misc.js_api == JS_API_ENABLED) 
            create_global_object();
        else 
            return false;
    }

    dwb.state.script_completion = NULL;

    char *dir = util_get_data_dir(LIBJS_DIR);
    if (dir != NULL) 
    {
        JSValueRef e = NULL;
        GString *content = g_string_new(NULL);
        util_get_directory_content(content, dir, "js", "dwb.js");
        if (content != NULL)  
        {
            JSStringRef js_script = JSStringCreateWithUTF8CString(content->str);
            JSEvaluateScript(s_ctx->global_context, js_script, NULL, NULL, 0, &e);
            JSStringRelease(js_script);
            if (e != NULL) {
                js_print_exception(s_ctx->global_context, e, NULL, 0, 0, NULL);
            }
        }
        g_string_free(content, true);
        g_free(dir);
    }


    UNDEFINED = JSValueMakeUndefined(s_ctx->global_context);
    JSValueProtect(s_ctx->global_context, UNDEFINED);
    s_nil = JSValueMakeNull(s_ctx->global_context);
    JSValueProtect(s_ctx->global_context, s_nil);

    s_ctx->init_before = get_private(s_ctx->global_context, "_initBefore");
    s_ctx->init_after = get_private(s_ctx->global_context, "_initAfter");

    JSObjectRef o = JSObjectMakeArray(s_ctx->global_context, 0, NULL, NULL);
    s_ctx->constructors[CONSTRUCTOR_ARRAY] = js_get_object_property(s_ctx->global_context, o, "constructor");
    JSValueProtect(s_ctx->global_context, s_ctx->constructors[CONSTRUCTOR_ARRAY]); 
    return true;
}/*}}}*/

gboolean 
scripts_execute_one(const char *script) 
{
    gboolean ret = false;
    if (s_ctx != NULL && s_ctx->global_context != NULL)
    {
        char *debug = g_strdup_printf(SCRIPT_TEMPLATE_INCLUDE, "dwb:scripts", script);
        ret = js_execute(s_ctx->global_context, debug, NULL) != NULL;
        g_free(debug);
    }

    return ret;
}
void
scripts_unprotect(JSObjectRef obj) 
{
    g_return_if_fail(obj != NULL);
    EXEC_LOCK;
    JSValueUnprotect(s_ctx->global_context, obj);
    EXEC_UNLOCK;
}
void
scripts_check_syntax(char **scripts)
{
    scripts_init(true);
    for (int i=0; scripts[i]; i++)
    {
        char *content = util_get_file_content(scripts[i], NULL);
        if (content != NULL)
        {
            const char *tmp = content;
            if (g_str_has_prefix(tmp, "#!javascript"))
                tmp += 12;
            if (js_check_syntax(s_ctx->global_context, tmp, scripts[i], 0)) 
            {
                fprintf(stderr, "Syntax of %s is correct.\n", scripts[i]);
            }
            g_free(content);
        }
    }
    scripts_end(true);
}

/* scripts_end {{{*/
void
scripts_end(gboolean clean_all) 
{
    pthread_rwlock_wrlock(&s_context_lock);
    if (s_ctx != NULL) 
    {
        GList *next, *l;
        next = dwb.override_keys;
        while (next != NULL)
        {
            l = next;
            next = next->next;
            KeyMap *m = l->data;
            if (m->map->prop & CP_SCRIPT) 
                dwb.override_keys = g_list_delete_link(dwb.override_keys, l);

        }
        next = dwb.keymap;
        while(next != NULL) 
        {
            l = next;
            next = next->next;
            KeyMap *m = l->data;
            if (m->map->prop & CP_SCRIPT) 
                unbind_free_keymap(s_ctx->global_context, l);
        }
        dwb.misc.script_signals = 0;

        for (GList *gl = dwb.state.views; gl; gl=gl->next) {
            if (VIEW(gl)->script_wv != NULL) {
                JSValueUnprotect(s_ctx->global_context, VIEW(gl)->script_wv);
                VIEW(gl)->script_wv = NULL;
            }
        }

        if (s_ctx->global_context != NULL) {
            JSValueUnprotect(s_ctx->global_context, UNDEFINED);
            JSValueUnprotect(s_ctx->global_context, NIL);
        }

        script_context_free(s_ctx);
        s_ctx = NULL;
    }
    pthread_rwlock_unlock(&s_context_lock);
    if (clean_all) {
        g_slist_free_full(s_autoloaded_extensions, g_free);
    }
}/*}}}*//*}}}*/
