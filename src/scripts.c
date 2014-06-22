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
#include "scripts/cl_deferred.h"

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


#define BOXED_GET_CHAR(name, getter, Boxed) static JSValueRef name(JSContextRef ctx, JSObjectRef this, JSStringRef property, JSValueRef* exception)  \
{\
    Boxed *priv = JSObjectGetPrivate(this); \
    if (priv != NULL) { \
        const char *value = getter(priv); \
        if (priv != NULL) \
            return js_char_to_value(ctx, value); \
    } \
    return NIL; \
}
#define BOXED_GET_BOOLEAN(name, getter, Boxed) static JSValueRef name(JSContextRef ctx, JSObjectRef this, JSStringRef property, JSValueRef* exception)  \
{\
    Boxed *priv = JSObjectGetPrivate(this); \
    if (priv != NULL) { \
        return JSValueMakeBoolean(ctx, getter(priv)); \
    } \
    return JSValueMakeBoolean(ctx, false); \
}
#define BOXED_SET_CHAR(name, setter, Boxed)  static bool name(JSContextRef ctx, JSObjectRef this, JSStringRef propertyName, JSValueRef js_value, JSValueRef* exception) { \
    Boxed *priv = JSObjectGetPrivate(this); \
    if (priv != NULL) { \
        char *value = js_value_to_char(ctx, js_value, -1, exception); \
        if (value != NULL) \
        {\
            setter(priv, value); \
            g_free(value);\
            return true;\
        }\
    } \
    return false;\
}
#define BOXED_SET_BOOLEAN(name, setter, Boxed)  static bool name(JSContextRef ctx, JSObjectRef this, JSStringRef propertyName, JSValueRef js_value, JSValueRef* exception) { \
    Boxed *priv = JSObjectGetPrivate(this); \
    if (priv != NULL) { \
        gboolean value = JSValueToBoolean(ctx, js_value); \
        setter(priv, value); \
        return true;\
    } \
    return false;\
}

#define BOXED_DEF_VOID(Boxed, name, func) static JSValueRef name(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) { \
    Boxed *priv = JSObjectGetPrivate(this); \
    if (priv != NULL) { \
        func(priv); \
    } \
    return UNDEFINED; \
}

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
object_destroy_cb(JSObjectRef o) 
{
    EXEC_LOCK;

    JSObjectSetPrivate(o, NULL);
    JSValueUnprotect(s_ctx->global_context, o);

    EXEC_UNLOCK;
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
static void 
finalize_gtimer(JSObjectRef o) {
    GTimer *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        g_timer_destroy(ob);
    }
}


JSValueRef 
scripts_call_as_function(JSContextRef ctx, JSObjectRef func, JSObjectRef this, size_t argc, const JSValueRef argv[])
{
    char path[PATH_MAX] = {0};
    int line = -1;
    JSValueRef exc = NULL;
    JSValueRef ret = JSObjectCallAsFunction(ctx, func, this, argc, argv, &exc);
    if (exc != NULL) 
    {
        js_print_exception(ctx, exc, path, PATH_MAX, 0, &line);
    }
    return ret;
}

static JSValueRef 
inject_api_callback(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc)
{
    JSObjectRef api_function = NULL;
    if (argc > 0 && (api_function = js_value_to_function(ctx, argv[0], exc)))
    {
        char *body = scripts_get_body(ctx, api_function, exc);
        if (body == NULL)
            return NULL; 
        EXEC_LOCK;
        if (argc > 1)
        {
            JSValueRef args [] = { js_context_change(ctx, s_ctx->global_context, argv[1], exc) };
            scripts_include(s_ctx->global_context, "local", body, false, false, 1, args, NULL);
        }
        else 
        {
            scripts_include(s_ctx->global_context, "local", body, false, false, 0, NULL, NULL);
        }
        EXEC_UNLOCK;
        g_free(body);
        
    }
    return NULL;
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


JSObjectRef 
suri_to_object(JSContextRef ctx, SoupURI *uri, JSValueRef *exception)
{
    JSObjectRef o = JSObjectMake(ctx, NULL, NULL);
    js_set_object_property(ctx, o, "scheme", uri->scheme, exception);
    js_set_object_property(ctx, o, "user", uri->user, exception);
    js_set_object_property(ctx, o, "password", uri->password, exception);
    js_set_object_property(ctx, o, "host", uri->host, exception);
    js_set_object_number_property(ctx, o, "port", uri->port, exception);
    js_set_object_property(ctx, o, "path", uri->path, exception);
    js_set_object_property(ctx, o, "query", uri->query, exception);
    js_set_object_property(ctx, o, "fragment", uri->fragment, exception);
    return o;
}

/*}}}*/

/* SOUP_MESSAGE {{{*/
/** 
 * A SoupUri
 *
 * @class
 * @name SoupUri
 * */
 /**
  * The scheme part of the uri
  * @name scheme 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The user part of the uri
  * @name user 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The password part of the uri
  * @name password 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The host part of the uri
  * @name host 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The port of the uri
  * @name port 
  * @memberOf SoupUri.prototype 
  * @type Number
  * @readonly
  * */
 /**
  * The path part of the uri
  * @name path
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The query part of the uri
  * @name query 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The fragment part of the uri
  * @name fragment 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
static JSValueRef 
get_soup_uri(JSContextRef ctx, JSObjectRef object, SoupURI * (*func)(SoupMessage *), JSValueRef *exception)
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    if (msg == NULL)
        return NIL;

    SoupURI *uri = func(msg);
    if (uri == NULL)
        return NIL;
    return suri_to_object(ctx, uri, exception);
}

/* message_get_uri {{{*/
/**
 * The uri of a message
 *
 * @name uri
 * @memberOf SoupMessage.prototype
 * @type SoupUri
 * */
static JSValueRef 
message_get_uri(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return get_soup_uri(ctx, object, soup_message_get_uri, exception);
}/*}}}*/

/* message_get_first_party {{{*/
/**
 * The first party uri of a message
 *
 * @name firstParty
 * @memberOf SoupMessage.prototype
 * @type SoupUri
 * */
static JSValueRef 
message_get_first_party(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return get_soup_uri(ctx, object, soup_message_get_first_party, exception);
}/*}}}*/
/**
 * The request headers of a message
 *
 * @name requestHeaders
 * @memberOf SoupMessage.prototype
 * @type SoupHeaders
 *
 * @since 1.1
 * */
static JSValueRef 
message_get_request_headers(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    if (msg != NULL)
    {
        SoupMessageHeaders *headers;
        g_object_get(msg, "request-headers", &headers, NULL);
        return JSObjectMake(ctx, s_ctx->classes[CLASS_SOUP_HEADER], headers);
    }
    return NIL;
}/*}}}*/
/**
 * The response headers of a message
 *
 * @name responseHeaders
 * @memberOf SoupMessage.prototype
 * @type SoupHeaders
 *
 * @since 1.1
 * */
static JSValueRef 
message_get_response_headers(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    if (msg != NULL)
    {
        SoupMessageHeaders *headers;
        g_object_get(msg, "response-headers", &headers, NULL);
        return JSObjectMake(ctx, s_ctx->classes[CLASS_SOUP_HEADER], headers);
    }
    return NIL;
}/*}}}*/
// TODO: Documentation
static JSValueRef 
message_set_status(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return UNDEFINED;
    double status = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(status))
    {
        SoupMessage *msg = JSObjectGetPrivate(this);
        g_return_val_if_fail(msg != NULL, UNDEFINED);
        soup_message_set_status(msg, (int)status);
    }
    return UNDEFINED;
}
// TODO: Documentation
static JSValueRef 
message_set_response(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return UNDEFINED;
    char *content_type = NULL;
    char *response = js_value_to_char(ctx, argv[0], -1, exc);
    if (response != NULL)
    {
        SoupMessage *msg = JSObjectGetPrivate(this);
        g_return_val_if_fail(msg != NULL, UNDEFINED);
        if (argc > 1)
            content_type = js_value_to_char(ctx, argv[1], -1, exc);

        soup_message_set_response(msg, content_type ? content_type : "text/html", SOUP_MEMORY_TAKE, response, strlen(response));
        g_free(content_type);
    }
    return UNDEFINED;
}

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
    GTimer *timer = g_timer_new();
    return JSObjectMake(ctx, s_ctx->classes[CLASS_TIMER], timer);
}
/* FRAMES {{{*/
/* frame_get_domain {{{*/
/** 
 * The domain name of the frame which is the effective second level domain
 *
 * @name domain
 * @memberOf WebKitWebFrame.prototype
 * @type String
 *
 * */
static JSValueRef 
frame_get_domain(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(object);
    if (frame == NULL)
        return NIL;

    const char *domain = dwb_soup_get_domain(frame);
    if (domain == NULL)
        return NIL;
    return js_char_to_value(ctx, domain);
}/*}}}*/

/* frame_get_host {{{*/
/** 
 * The host name of the frame
 *
 * @name host
 * @memberOf WebKitWebFrame.prototype
 * @type String
 *
 * */
static JSValueRef 
frame_get_host(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(object);
    if (frame == NULL)
        return NIL;

    const char *host = dwb_soup_get_host(frame);
    if (host == NULL)
        return NIL;
    return js_char_to_value(ctx, host);
}/*}}}*/

/** 
 * The DOMDocument of the frame
 *
 * @name document
 * @memberOf WebKitWebFrame.prototype
 * @type DOMObject
 *
 * */
static JSValueRef 
frame_get_document(JSContextRef ctx, JSObjectRef self, JSStringRef js_name, JSValueRef* exception) {
    WebKitWebFrame *frame = JSObjectGetPrivate(self);
    if (frame == NULL)
        return NIL;
    WebKitDOMDocument *doc = webkit_web_frame_get_dom_document(frame);
    return scripts_make_object(ctx, G_OBJECT(doc));
    
}

/* frame_inject {{{*/

/**
 * Injects javascript code into a frame or webview 
 *
 * @name inject
 * @memberOf WebKitWebFrame.prototype
 * @function 
 * @example 
 * //!javascript
 * function injectable() {
 *    var text = exports.text;
 *    document.body.innerHTML = text;
 * }
 * signals.connect("documentLoaded", function(wv) {
 *    wv.inject(injectable, { text : "foo", number : 37 }, 3);
 * });
 *
 * @param {String|Function} code
 *      The script to inject, either a string or a function. If it is a function
 *      the body will be wrapped inside a new function.
 * @param {Object} arg
 *      If the script isn’t injected into the global scope the script is wrapped
 *      inside a function. arg then is accesible via arguments in the injected
 *      script, or by the variable <i>exports</i>, optional
 * @param {Number} [line]
 *      Starting line number, useful for debugging. If linenumber is greater
 *      than 0 error messages will be printed to stderr, optional.
 * @param {Boolean} [global]
 *      true to inject it into the global scope, false to encapsulate it in a
 *      function, default false
 * @returns {String}
 *      The return value of the script. If the script is injected globally
 *      inject always returns null. The return value is always converted to a
 *      string. To return objects call JSON.parse on the return value.
 * */
static JSValueRef 
frame_inject(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebFrame *frame = JSObjectGetPrivate(this);
    if (frame != NULL) 
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(frame);
        return inject(ctx, wctx, function, this, argc, argv, exc);
    }
    return NIL;
}/*}}}*/

static void
load_string_resource(WebKitWebView *wv, WebKitWebFrame *frame, WebKitWebResource *resource, WebKitNetworkRequest *request, WebKitNetworkResponse *response, gpointer *data) 
{
    const char *uri = webkit_network_request_get_uri(request);
    if (!(g_str_has_prefix(uri, "dwb-chrome:") 
            || g_str_has_prefix(uri, "data:image/gif;base64,") 
            || g_str_has_prefix(uri, "data:image/png;base64,") 
            || g_str_has_prefix(uri, "data:image/jpeg;base64,") 
            || g_str_has_prefix(uri, "data:image/svg;base64,") )
            || webkit_web_view_get_main_frame(wv) != frame)
        webkit_network_request_set_uri(request, "about:blank");
}

static gboolean 
load_string_navigation(WebKitWebView *wv, WebKitWebFrame *frame, WebKitNetworkRequest *request, WebKitWebNavigationAction *action,
    WebKitWebPolicyDecision *policy, GList *gl) 
{
    if (frame == webkit_web_view_get_main_frame(wv))
    {
        const char *uri = webkit_network_request_get_uri(request);
        if (!g_str_has_prefix(uri, "dwb-chrome:"))
        {
            g_signal_handlers_disconnect_by_func(wv, load_string_navigation, NULL);
            g_signal_handlers_disconnect_by_func(wv, load_string_resource, NULL);
        }
    }
    return false;
}

static void
load_string_status(WebKitWebFrame *frame, GParamSpec *param, JSObjectRef deferred)
{
    EXEC_LOCK;
    WebKitLoadStatus status = webkit_web_frame_get_load_status(frame);
    gboolean is_main_frame = webkit_web_view_get_main_frame(webkit_web_frame_get_web_view(frame)) == frame;
    if (status == WEBKIT_LOAD_FINISHED)
        deferred_resolve(s_ctx->global_context, deferred, deferred, 0, NULL, NULL);
    else if (status == WEBKIT_LOAD_COMMITTED
            && is_main_frame
            && GPOINTER_TO_INT(g_object_get_data(G_OBJECT(frame), "dwb_load_string_api")))
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(frame);
        JSStringRef api_name = JSStringCreateWithUTF8CString("dwb");
        JSObjectRef api_function = JSObjectMakeFunctionWithCallback(wctx, api_name, inject_api_callback);
        JSObjectSetProperty(wctx, JSContextGetGlobalObject(wctx), api_name, api_function, kJSDefaultAttributes, NULL);
        JSStringRelease(api_name);
    }
    else if (status == WEBKIT_LOAD_FAILED)
        deferred_reject(s_ctx->global_context, deferred, deferred, 0, NULL, NULL);
    if (status == WEBKIT_LOAD_FINISHED || status == WEBKIT_LOAD_FAILED)
    {
        g_signal_handlers_disconnect_by_func(frame, load_string_status, deferred);
        g_object_steal_data(G_OBJECT(frame), "dwb_load_string_api");
    }
    JSValueUnprotect(s_ctx->global_context, deferred);
    EXEC_UNLOCK;

}
/**
 * Loads a string in a frame or a webview
 *
 * @name loadString
 * @memberOf WebKitWebFrame.prototype
 * @function 
 *
 * @param {String} content
 *      The string to load
 * @param {String} [mimeType]
 *      The MIME-type, if omitted or null <i>text/html</i> is assumed.
 * @param {String} [encoding]
 *      The character encoding, if omitted or null <i>UTF-8</i> is assumed.
 * @param {String} [baseUri]
 *      The base uri, if present it must either use the uri-scheme <i>dwb-chrome:</i>
 *      or <i>file:</i>, otherwise the request will be ignored. 
 * @param {boolean} [externalSources]
 *      Whether external sources, e.g. scripts, are allowed, defaults to false. If
 *      external resources are forbidden the function <b>dwb</b> can be called
 *      in the webcontext to execute functions in the scripting context 
 *
 * @returns {Deferred}
 *      A deferred that will be resolved if the webview has finished loading the
 *      string and rejected if an error occured
 *
 * @since 1.3
 * */

static JSValueRef 
frame_load_string(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *content = NULL, 
         *mime_type = NULL, 
         *encoding = NULL, 
         *base_uri = NULL;

    if (argc == 0)
        return NIL;
    WebKitWebFrame *frame = JSObjectGetPrivate(this);
    g_return_val_if_fail(frame != NULL, NIL);

    JSObjectRef deferred = NULL;
    gboolean forbid_resources = true;
    gboolean create_api_function = false;
    WebKitWebView *wv = webkit_web_frame_get_web_view(frame);

    deferred = deferred_new(ctx);
    JSValueProtect(ctx, deferred);
    content = js_value_to_char(ctx, argv[0], -1, exc);
    if (content == NULL)
        return UNDEFINED;
    if (argc > 1)
    {
        mime_type = js_value_to_char(ctx, argv[1], -1, exc);
        if (argc > 2)
        {
            encoding = js_value_to_char(ctx, argv[2], -1, exc);
            if (argc > 3)
                base_uri = js_value_to_char(ctx, argv[3], -1, exc);
            if (argc > 4)
                forbid_resources = ! JSValueToBoolean(ctx, argv[4]);
        }
    }
    create_api_function = base_uri != NULL && g_str_has_prefix(base_uri, "dwb-chrome:") && forbid_resources;
    if (forbid_resources && create_api_function)
    {
        g_signal_connect(wv, "navigation-policy-decision-requested", G_CALLBACK(load_string_navigation), NULL);
        g_signal_connect(wv, "resource-request-starting", G_CALLBACK(load_string_resource), NULL);
    }
    g_object_set_data(G_OBJECT(frame), "dwb_load_string_api", GINT_TO_POINTER(create_api_function));

    webkit_web_frame_load_string(frame, content, mime_type, encoding, base_uri ? base_uri : "");
    g_signal_connect(frame, "notify::load-status", G_CALLBACK(load_string_status), deferred);

    g_free(content);
    g_free(mime_type);
    g_free(encoding);
    g_free(base_uri);
    return deferred ? deferred : NIL;
}
/*}}}*/

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
/* global_checksum {{{*/




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

/* DATA {{{*/
/* data_get_profile {{{*/
/*}}}*/

/* SYSTEM {{{*/

/* SYSTEM {{{*/

/* spawn_output {{{*/

/* system_file_test {{{*/



/*}}}*/

/* IO {{{*/

static JSObjectRef 
hwv_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    GObject *wv = G_OBJECT(webkit_web_view_new());
    return make_object_for_class(ctx, s_ctx->classes[CLASS_HIDDEN_WEBVIEW], wv, false);
}

/** 
 * Moves a widget in a GtkBox or a GtkMenu to a new Position
 *
 * @name reorderChild
 * @memberOf GtkWidget.prototype
 * @function 
 *
 * @param {GtkWidget} child
 *      The child widget
 * @param {Number} position
 *      Whether to expand the widget
 * */
static JSValueRef 
widget_reorder_child(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2)
        return UNDEFINED;
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, UNDEFINED);

    if (! (GTK_IS_BOX(widget) || GTK_IS_MENU(widget)))
    {
        js_make_exception(ctx, exc, EXCEPTION("Widget.reorderChild: Not a GtkBox or GtkMenu"));
        return UNDEFINED;
    }

    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return UNDEFINED;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return UNDEFINED;
    double position = JSValueToNumber(ctx, argv[1], exc);
    if (isnan(position))
        return UNDEFINED;
    if (GTK_IS_BOX(widget)) {
        gtk_box_reorder_child(GTK_BOX(widget), child, (int)position);
    }
    else {
        gtk_menu_reorder_child(GTK_MENU(widget), child, (int)position);
    }
    return UNDEFINED;
}

/** 
 * Adds a widget to a GtkBox
 *
 * @name packEnd
 * @memberOf GtkWidget.prototype
 * @function 
 *
 * @param {GtkWidget} child
 *      The child widget
 * @param {Boolean} expand
 *      Whether to expand the widget
 * @param {Boolean} fill
 *      Whether to fill the remaining space
 * @param {Number} padding
 *      Padding in the box
 *
 * */
/** 
 * Adds a widget to a GtkBox
 *
 * @name packStart
 * @memberOf GtkWidget.prototype
 * @function 
 *
 * @param {GtkWidget} child
 *      The child widget
 * @param {Boolean} expand
 *      Whether to expand the widget
 * @param {Boolean} fill
 *      Whether to fill the remaining space
 * @param {Number} padding
 *      Padding in the box
 *
 * */
static JSValueRef 
widget_pack(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc <= 2)
        return UNDEFINED;

    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, UNDEFINED);

    if (! GTK_IS_BOX(widget))
    {
        js_make_exception(ctx, exc, EXCEPTION("Widget.packStart: Not a GtkBox"));
        return UNDEFINED;
    }

    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return UNDEFINED;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return UNDEFINED;


    gboolean expand = JSValueToBoolean(ctx, argv[1]);
    gboolean fill = JSValueToBoolean(ctx, argv[2]);

    gdouble padding = 0;
    if (argc > 3) 
    {
        padding = JSValueToNumber(ctx, argv[3], exc);
        if (isnan(padding))
            padding = 0;
    }
    char *name = js_get_string_property(ctx, function, "name");
    if (!g_strcmp0(name, "packStart"))
        gtk_box_pack_start(GTK_BOX(widget), child, expand, fill, (int)padding);
    else 
        gtk_box_pack_end(GTK_BOX(widget), child, expand, fill, (int)padding);
    g_free(name);
    return UNDEFINED;
}

JSValueRef
container_child_func(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc, void (*func)(GtkContainer *, GtkWidget *)) {
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, UNDEFINED);

    if (! GTK_IS_CONTAINER(widget))
    {
        js_make_exception(ctx, exc, EXCEPTION("Not a GtkContainer"));
        return UNDEFINED;
    }
    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return UNDEFINED;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return UNDEFINED;
    func(GTK_CONTAINER(widget), child);
    return UNDEFINED;
}
/**  
 * Adds a child to a GtkWidget, note that the function can only be called on
 * widgets derived from GtkContainer
 *
 * @name add
 * @memberOf GtkWidget.prototype
 * @function
 *
 * @param {GtkWidget} widget
 *      The widget to add to the container
 *
 * */
static JSValueRef 
widget_container_add(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return container_child_func(ctx, function, this, argc, argv, exc, gtk_container_add);
}
/**  
 * Removes a child from a GtkWidget, note that the function can only be called on
 * widgets derived from GtkContainer
 *
 * @name remove
 * @memberOf GtkWidget.prototype
 * @function
 * @since 1.9
 *
 * @param {GtkWidget} widget
 *      The widget to remove from the container
 *
 * */
static JSValueRef 
widget_container_remove(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return container_child_func(ctx, function, this, argc, argv, exc, gtk_container_remove);
}
/** 
 * Gets all children of a widget. Note that this function can only be called on
 * widgets derived from GtkContainer.
 *
 * @name getChildren
 * @memberOf GtkWidget.prototype
 * @function
 * @since 1.9
 *
 * @return {Array[GtkWidget]} 
 *      An array of children or an empty array if the widget has no children
 *
 * */
static JSValueRef 
widget_get_children(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    GtkWidget *widget = JSObjectGetPrivate(this);

    g_return_val_if_fail(widget != NULL, UNDEFINED);

    JSValueRef result;
    GList *children = NULL;

    int i=0;

    if (! GTK_IS_CONTAINER(widget))
    {
        js_make_exception(ctx, exc, EXCEPTION("Widget.getChildren: Not a GtkContainer"));
        return NIL;
    }
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    if (children == NULL) {
        return JSObjectMakeArray(ctx, 0, NULL, exc);
    }
    int n_children = g_list_length(children);
    if (n_children == 0) {
        result = JSObjectMakeArray(ctx, 0, NULL, exc);
    }
    else {
        JSValueRef js_children[n_children];
        for (GList *l=children; l; l = l->next, i++) {
            js_children[i] = scripts_make_object(ctx, G_OBJECT(l->data));
        }
        result = JSObjectMakeArray(ctx, n_children, js_children, exc);
    }
    g_list_free(children);
    return result;
}

/**
 * Destroys a widget
 *
 * @name destroy
 * @memberOf GtkWidget.prototype
 * @function
 * */
static JSValueRef 
widget_destroy(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, UNDEFINED);

    s_ctx->created_widgets = g_slist_remove_all(s_ctx->created_widgets, widget);
    gtk_widget_destroy(widget);
    return UNDEFINED;
}

static JSObjectRef 
widget_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    GType type = 0;
    if (argc > 0)
    {
        char *stype = js_value_to_char(ctx, argv[0], 128, exception);
        if (stype == NULL)
            return JSValueToObject(ctx, NIL, NULL);
        type = g_type_from_name(stype);
        if (type == 0)
        {
            js_make_exception(ctx, exception, EXCEPTION("Widget constructor: unknown widget type"));
            return JSValueToObject(ctx, NIL, NULL);
        }
        GtkWidget *widget = gtk_widget_new(type, NULL);
        s_ctx->created_widgets = g_slist_prepend(s_ctx->created_widgets, widget);
        return scripts_make_object(ctx, G_OBJECT(widget));
    }
    return JSValueToObject(ctx, NIL, NULL);
}
/**
 * Called when a menu item was activated that was added to the popup menu,
 * <span class="ilkw">this</span> will refer to the GtkMenuItem.
 *
 * @callback GtkMenu~onMenuActivate
 * */
static void 
menu_callback(GtkMenuItem *item, JSObjectRef callback)
{
    EXEC_LOCK;
    JSObjectRef this =  make_object_for_class(s_ctx->global_context, s_ctx->classes[CLASS_WIDGET], G_OBJECT(item), true);
    scripts_call_as_function(s_ctx->global_context, callback, this, 0, NULL);
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
/**
 * Add menu items to the menu
 *
 * @name addItems
 * @memberOf GtkMenu.prototype
 * @function 
 *
 * @param {Array} items[] 
 *      Array of menu items and callbacks, if an item is <i>null</i> or label or
 *      callback is omitted it will be a separator
 * @param {GtkMenu~onMenuActivate} [items[].callback] 
 *      Callback called when the item is clicked, must be defined if it isn't a
 *      submenu 
 * @param {GtkMenu} [items[].menu]
 *      A gtk menu that will get a submenu of the menu, must be defined if
 *      callback is omitted
 * @param {String} [items[].label] 
 *      Label of the item, if omitted it will be a separator, if an character is
 *      preceded by an underscore it will be used as an accelerator key, to use
 *      a literal underscore in a label use a double underscore
 * @param {Number} [items[].position] 
 *      Position of the item or separator starting at 0, if omitted it will be appended
 *
 * @example 
 *
 * Signal.connect("contextMenu", function(wv, menu) {
 *     var submenu = new GtkWidget("GtkMenu");
 * 
 *     var submenuItems = [ 
 *         { 
 *             label : "Copy url to clipboard", 
 *             callback : function() 
 *             {
 *                 clipboard.set(Selection.clipboard, wv.uri);
 *             } 
 *         }, 
 *         { 
 *             label : "Copy url to primary", 
 *             callback : function() 
 *             {
 *                 clipboard.set(Selection.primary, wv.uri);
 *             } 
 *         }
 *     ];
 * 
 *     var menuItems = [
 *         // append separator
 *         null, 
 *         // append a menu item
 *         { 
 *             label : "Copy current url", 
 *             menu : submenu
 *         }
 *     ];
 * 
 *    submenu.addItems(submenuItems);
 *    menu.addItems(menuItems);
 * });
 *
 * */
static JSValueRef 
menu_add_items(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSObjectRef arg;
    if (argc == 0 || (arg = JSValueToObject(ctx, argv[0], exc)) == NULL )
        return UNDEFINED;

    double p;
    int position = -1;
    char *label = NULL;
    GtkWidget *item;

    JSObjectRef callback, o;
    JSValueRef current, label_value, callback_value;
    JSObjectRef js_submenu;
    GObject *submenu;

    GtkMenu *menu = JSObjectGetPrivate(this);
    if (menu)
    {
        JSStringRef str_position = JSStringCreateWithUTF8CString("position");
        JSStringRef str_label = JSStringCreateWithUTF8CString("label");
        JSStringRef str_callback = JSStringCreateWithUTF8CString("callback");

        js_array_iterator iter;
        js_array_iterator_init(ctx, &iter, arg);
        while ((current = js_array_iterator_next(&iter, exc)) != NULL)
        {
            item = NULL;
            label = NULL;
            if (JSValueIsNull(ctx, current))
            {
                item = gtk_separator_menu_item_new();
                goto create;
            }

            o = JSValueToObject(ctx, current, exc);
            if (o == NULL) {
                continue;
            }
            if (JSObjectHasProperty(ctx, o, str_position))
            {
                current = JSObjectGetProperty(ctx, o, str_position, exc);
                p = JSValueToNumber(ctx, current, exc);
                if (isnan(p))
                    position = -1;
                else 
                    position = (int) p;
            }
            if (JSObjectHasProperty(ctx, o, str_label))
            {
                label_value = JSObjectGetProperty(ctx, o, str_label, exc);
                label = js_value_to_char(ctx, label_value, -1, exc);
                if (label == NULL)
                    goto error;
                item = gtk_menu_item_new_with_mnemonic(label);

                if ((js_submenu = js_get_object_property(ctx, o, "menu")) != NULL) {
                    submenu = JSObjectGetPrivate(js_submenu);
                    if (submenu == NULL || !GTK_IS_MENU(submenu)) {
                        gtk_widget_destroy(item);
                        goto error;
                    }

                    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(submenu));
                }
                if (JSObjectHasProperty(ctx, o, str_callback)) {
                    callback_value = JSObjectGetProperty(ctx, o, str_callback, exc);
                    callback = js_value_to_function(ctx, callback_value, exc);
                    if (callback == NULL) {
                        gtk_widget_destroy(item);
                        goto error;
                    }
                    g_signal_connect(item, "activate", G_CALLBACK(menu_callback), callback);
                }
            }
            else 
            {
                item = gtk_separator_menu_item_new();
            }
create: 
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            if (position != -1)
                gtk_menu_reorder_child(menu, item, position);
            gtk_widget_show_all(item);
error:
            g_free(label);
        }
        js_array_iterator_finish(&iter);

        JSStringRelease(str_position);
        JSStringRelease(str_label);
        JSStringRelease(str_callback);
    }
    return UNDEFINED;

}

/* DOWNLOAD {{{*/
/* download_constructor_cb {{{*/
/**
 * Constructs a new download
 *
 * @memberOf WebKitDownload.prototype
 * @constructor
 * @param {String} uri The uri of the download
 * */
static JSObjectRef 
download_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    if (argc<1) 
        return JSValueToObject(ctx, NIL, NULL);

    char *uri = js_value_to_char(ctx, argv[0], -1, exception);
    if (uri == NULL) 
    {
        js_make_exception(ctx, exception, EXCEPTION("Download constructor: invalid argument"));
        return JSValueToObject(ctx, NIL, NULL);
    }

    WebKitNetworkRequest *request = webkit_network_request_new(uri);
    g_free(uri);
    if (request == NULL) 
    {
        js_make_exception(ctx, exception, EXCEPTION("Download constructor: invalid uri"));
        return JSValueToObject(ctx, NIL, NULL);
    }

    WebKitDownload *download = webkit_download_new(request);
    return JSObjectMake(ctx, s_ctx->classes[CLASS_DOWNLOAD], download);
}/*}}}*/
/**
 * Constructs a new Deferred.
 * @memberOf Deferred.prototype
 * @constructor 
 */
static JSObjectRef 
deferred_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    return deferred_new(ctx);
}

/* stop_download_notify {{{*/
static gboolean 
stop_download_notify(CallbackData *c) 
{
    WebKitDownloadStatus status = webkit_download_get_status(WEBKIT_DOWNLOAD(c->gobject));
    if (status == WEBKIT_DOWNLOAD_STATUS_ERROR || status == WEBKIT_DOWNLOAD_STATUS_CANCELLED || status == WEBKIT_DOWNLOAD_STATUS_FINISHED) 
        return true;

    return false;
}/*}}}*/

/* download_start {{{*/
/** 
 * Starts a download
 *
 * @name start
 * @memberOf WebKitDownload.prototype
 * @function 
 *
 * @param {WebKitDownload~statusCallback} callback
 *      Callback function that will be executed whenever the 
 *      {@link Enums and Flags.DownloadStatus|DownloadStatus} changes
 *
 * @returns {Boolean}
 *      true if the download was started
 * */
static JSValueRef 
download_start(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitDownload *download = JSObjectGetPrivate(this);
    if (download == NULL)
        return JSValueMakeBoolean(ctx, false);

    if (webkit_download_get_destination_uri(download) == NULL) 
    {
        js_make_exception(ctx, exc, EXCEPTION("Download.start: destination == null"));
        return JSValueMakeBoolean(ctx, false);
    }

    if (argc > 0) 
        make_callback(ctx, this, G_OBJECT(download), "notify::status", argv[0], stop_download_notify, exc);

    webkit_download_start(download);
    return JSValueMakeBoolean(ctx, true);

}/*}}}*/

/** 
 * Cancels a download
 *
 * @name cancel
 * @memberOf WebKitDownload.prototype
 * @function 
 * */
/* download_cancel {{{*/
static JSValueRef 
download_cancel(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitDownload *download = JSObjectGetPrivate(this);
    g_return_val_if_fail(download != NULL, UNDEFINED);

    webkit_download_cancel(download);
    return UNDEFINED;
}/*}}}*/
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

JSObjectRef 
make_object_for_class(JSContextRef ctx, JSClassRef class, GObject *o, gboolean protect)
{
    JSObjectRef retobj = g_object_get_qdata(o, s_ctx->ref_quark);
    if (retobj != NULL) {
        return retobj;
    }

    retobj = JSObjectMake(ctx, class, o);
    if (protect) 
    {
        g_object_set_qdata_full(o, s_ctx->ref_quark, retobj, (GDestroyNotify)object_destroy_cb);
        JSValueProtect(ctx, retobj);
    }
    else 
        g_object_set_qdata_full(o, s_ctx->ref_quark, retobj, NULL);

    return retobj;
}

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
    JSClassRef class;
    if (WEBKIT_IS_WEB_VIEW(o)) 
        class = s_ctx->classes[CLASS_WEBVIEW];
    else if (WEBKIT_IS_WEB_FRAME(o))
        class = s_ctx->classes[CLASS_FRAME];
    else if (WEBKIT_IS_DOWNLOAD(o)) 
        class = s_ctx->classes[CLASS_DOWNLOAD];
    else if (SOUP_IS_MESSAGE(o)) 
        class = s_ctx->classes[CLASS_MESSAGE];
    else if (WEBKIT_IS_WEB_BACK_FORWARD_LIST(o))
        class = s_ctx->classes[CLASS_HISTORY];
    else if (GTK_IS_MENU(o))
        class = s_ctx->classes[CLASS_MENU];
    else if (GTK_IS_WIDGET(o))
        class = s_ctx->classes[CLASS_SECURE_WIDGET];
#if WEBKIT_CHECK_VERSION(1, 10, 0)
    else if (WEBKIT_IS_FILE_CHOOSER_REQUEST(o)) {
        class = s_ctx->classes[CLASS_FILE_CHOOSER];
        o = g_object_ref(o);
    }
#endif
    else if (WEBKIT_IS_DOM_OBJECT(o)) {
        return make_dom_object(ctx, o);
    }
    else 
        class = s_ctx->classes[CLASS_GOBJECT];

    result =  make_object_for_class(ctx, class, o, true);

    return result;
}/*}}}*/


/* set_property_cb {{{*/
static bool
set_property_cb(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception) {
    return true;
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

    /* Frame */
    /** 
     * Represents a frame or an iframe
     *
     * @name WebKitWebFrame
     * @augments GObject
     * @class 
     *      Represents a frame or iframe. Due to same origin policy it
     *      is not possible to inject scripts from a WebKitWebView into iframes with a
     *      different domain. For this purpose the frame object can be used.
     * */
    JSStaticFunction frame_functions[] = { 
        { "inject",          frame_inject,             kJSDefaultAttributes },
        { "loadString",      frame_load_string,        kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };
    JSStaticValue frame_values[] = {
        { "host", frame_get_host, NULL, kJSDefaultAttributes }, 
        { "document", frame_get_document, NULL, kJSDefaultAttributes }, 
        { "domain", frame_get_domain, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };

    cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitWebFrame";
    cd.staticFunctions = frame_functions;
    cd.staticValues = frame_values;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_FRAME] = JSClassCreate(&cd);

    s_ctx->constructors[CONSTRUCTOR_FRAME] = scripts_create_constructor(ctx, "WebKitWebFrame", s_ctx->classes[CLASS_FRAME], NULL, NULL);

    /* SoupMessage */ 
    /**
     * Represents a SoupMessage 
     *
     * @name SoupMessage
     * @augments GObject 
     * @class 
     *      Represents a SoupMessage. Can be used to inspect information about a
     *      request
     *
     * @returns undefined
     * */
    JSStaticValue message_values[] = {
        { "uri",                message_get_uri, NULL, kJSDefaultAttributes }, 
        { "firstParty",         message_get_first_party, NULL, kJSDefaultAttributes }, 
        { "requestHeaders",     message_get_request_headers, NULL, kJSDefaultAttributes }, 
        { "responseHeaders",    message_get_response_headers, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSStaticFunction message_functions[] = {
        { "setStatus",       message_set_status, kJSDefaultAttributes }, 
        { "setResponse",     message_set_response, kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };

    cd = kJSClassDefinitionEmpty;
    cd.className = "SoupMessage";
    cd.staticFunctions = message_functions;
    cd.staticValues = message_values;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_MESSAGE] = JSClassCreate(&cd);

    s_ctx->constructors[CONSTRUCTOR_SOUP_MESSAGE] = scripts_create_constructor(ctx, "SoupMessage", s_ctx->classes[CLASS_MESSAGE], NULL, NULL);

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

    cd = kJSClassDefinitionEmpty;
    cd.className = "Timer";
    cd.staticFunctions = gtimer_functions;
    cd.staticValues = gtimer_values;
    cd.finalize = finalize_gtimer;
    s_ctx->classes[CLASS_TIMER] = JSClassCreate(&cd);
    s_ctx->constructors[CONSTRUCTOR_TIMER] = scripts_create_constructor(ctx, "Timer", s_ctx->classes[CLASS_TIMER], gtimer_construtor_cb, NULL);

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
    cd.className = "HistoryList";
    cd.staticFunctions = history_functions;
    cd.staticValues = history_values;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_HISTORY] = JSClassCreate(&cd);

    s_ctx->constructors[CONSTRUCTOR_HISTORY_LIST] = scripts_create_constructor(ctx, "WebKitWebBackForwardList", s_ctx->classes[CLASS_HISTORY], NULL, NULL);

    /**
     * Constructs a new Deferred
     *
     * @class 
     *      Deferred objects can be used to manage asynchronous operations. It
     *      can trigger a callback function when an asynchrounous operation has
     *      finished, and allows chaining of callbacks. Deferred basically has 2
     *      callback chains, a done-chain and a fail-chain. If a asynchronous
     *      operation is successful the deferred should be resolved and the done
     *      callback chain of the deferred is called. If a asynchronous
     *      operation fails the deferred should be rejected and the fail
     *      callback chain of the deferred is called.
     * @name Deferred
     * @constructs Deferred
     * @example
     * system.spawn("command").then(
     *      function() {
     *          // called when execution was  successful 
     *      },
     *      function(errorcode) {
     *          // called when execution wasn't successful 
     *      }
     * );
     *
     * function foo() {
     *     var d = new Deferred();
     *     timerStart(2000, function() {
     *         d.reject("rejected");
     *     });
     *     return d;
     * }
     * function onResponse(response) {
     *     io.out(response);
     * }
     *
     * // Will print "rejected" after 2 and 4 seconds
     * foo().fail(onResponse).fail(onResponse);
     *
     * @returns A Deferred
     *
     * */
    JSStaticFunction deferred_functions[] = { 
        { "then",             deferred_then,         kJSDefaultAttributes },
        { "resolve",          deferred_resolve,         kJSDefaultAttributes },
        { "reject",           deferred_reject,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSStaticValue deferred_values[] = {
        { "isFulfilled",     deferred_is_fulfilled, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "Deferred"; 
    cd.staticFunctions = deferred_functions;
    cd.staticValues = deferred_values;
    s_ctx->classes[CLASS_DEFERRED] = JSClassCreate(&cd);
    s_ctx->constructors[CONSTRUCTOR_DEFERRED] = scripts_create_constructor(ctx, "Deferred", s_ctx->classes[CLASS_DEFERRED], deferred_constructor_cb, NULL);

    /** 
     * Constructs a new GtkWidget
     *
     * @class 
     *      It is possible to create new widgets but only widgets that are currently
     *      used by dwb can be created. The widgets used by dwb can be found under
     *      {@see http://portix.bitbucket.org/dwb/resources/layout.html}
     * @augments GObject
     * @name GtkWidget
     *
     * @constructs GtkWidget
     * @param {String} name Name of the Widget, e.g. "GtkLabel"
     *
     * @returns A GtkWidget
     * @example
     * var myLabel = new GtkWidget("GtkLabel");
     * */
    JSStaticFunction secure_widget_functions[] = { 
        { "packStart",              widget_pack,          kJSDefaultAttributes },
        { "packEnd",                widget_pack,          kJSDefaultAttributes },
        { "reorderChild",           widget_reorder_child, kJSDefaultAttributes },
        { "add",                    widget_container_add, kJSDefaultAttributes },
        { "remove",                 widget_container_remove, kJSDefaultAttributes },
        { "getChildren",            widget_get_children, kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "SecureWidget";
    cd.staticFunctions = secure_widget_functions;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_SECURE_WIDGET] = JSClassCreate(&cd);

    JSStaticFunction widget_functions[] = { 
        { "destroy",                widget_destroy,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "GtkWidget";
    cd.staticFunctions = widget_functions;
    cd.parentClass = s_ctx->classes[CLASS_SECURE_WIDGET];
    s_ctx->classes[CLASS_WIDGET] = JSClassCreate(&cd);
    s_ctx->constructors[CONSTRUCTOR_WIDGET] = scripts_create_constructor(ctx, "GtkWidget", s_ctx->classes[CLASS_WIDGET], widget_constructor_cb, NULL);

    cd = kJSClassDefinitionEmpty;
    cd.className = "HiddenWebView";
    cd.staticFunctions = widget_functions;
    cd.parentClass = s_ctx->classes[CLASS_WEBVIEW];
    s_ctx->classes[CLASS_HIDDEN_WEBVIEW] = JSClassCreate(&cd);

    s_ctx->constructors[CONSTRUCTOR_HIDDEN_WEB_VIEW] = scripts_create_constructor(ctx, "HiddenWebView", s_ctx->classes[CLASS_HIDDEN_WEBVIEW], hwv_constructor_cb, NULL);

    /**
     * @class 
     *      Widget that will be created when a context menu is shown, can be
     *      used to add custom items to the menu
     *
     * @augments GtkWidget 
     * @name GtkMenu
     * */
    JSStaticFunction menu_functions[] = { 
        { "addItems",                menu_add_items,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "GtkMenu";
    cd.staticFunctions = menu_functions;
    cd.parentClass = s_ctx->classes[CLASS_WIDGET];
    s_ctx->classes[CLASS_MENU] = JSClassCreate(&cd);

    /** 
     * Constructs a new download
     *
     * @class 
     *    Object that can be used for downloads
     * @name WebKitDownload
     * @augments GObject
     *
     * @constructs WebKitDownload 
     *
     * @param {String} url The url of the download
     *
     * @returns WebKitDownLoad
     * */
    /* download */
    JSStaticFunction download_functions[] = { 
        { "start",          download_start,        kJSDefaultAttributes },
        { "cancel",         download_cancel,        kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };

    cd.className = "WebKitDownload";
    cd.staticFunctions = download_functions;
    cd.staticValues = NULL;
    cd.parentClass = s_ctx->classes[CLASS_GOBJECT];
    s_ctx->classes[CLASS_DOWNLOAD] = JSClassCreate(&cd);

    s_ctx->constructors[CONSTRUCTOR_DOWNLOAD] = scripts_create_constructor(ctx, "WebKitDownload", s_ctx->classes[CLASS_DOWNLOAD], download_constructor_cb, NULL);

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


    dom_initialize(s_ctx);

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
    
    s_ctx->session = make_object_for_class(ctx, s_ctx->classes[CLASS_GOBJECT], G_OBJECT(webkit_get_default_session()), false);
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

        // Only destroy webviews references if the a new context will be
        // created, if dwb is closed the references will be freed by view_clean.
        if (!clean_all) {
            for (GList *gl = dwb.state.views; gl; gl=gl->next) {
                if (VIEW(gl)->script_wv != NULL) {
                    JSValueUnprotect(s_ctx->global_context, VIEW(gl)->script_wv);
                    VIEW(gl)->script_wv = NULL;
                }
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
