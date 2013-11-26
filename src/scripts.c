/*
 * Copyright (c) 2010-2013 Stefan Bolte <portix@gmx.net>
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <JavaScriptCore/JavaScript.h>
#include <glib.h>
#include <cairo.h>
#include <exar.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "dwb.h"
#include "scripts.h" 
#include "session.h" 
#include "util.h" 
#include "js.h" 
#include "soup.h" 
#include "domain.h" 
#include "application.h" 
#include "completion.h" 
#include "entry.h" 

#define API_VERSION 1.5

//#define kJSDefaultFunction  (kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete )
#define kJSDefaultProperty  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )
#define kJSDefaultAttributes  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )

#define SCRIPT_TEMPLATE_START "try{_initNewContext(this,arguments,'%s');const script=this;/*<dwb*/"
#define SCRIPT_TEMPLATE_XSTART "try{"\
"var exports=arguments[0];"\
"_initNewContext(this,arguments,'%s');"\
"var xinclude=_xinclude.bind(this,this.path);"\
"const script=this;"\
"if(!exports.id)Object.defineProperty(exports,'id',{value:script.generateId()});/*<dwb*/"

#define SCRIPT_TEMPLATE_END "%s/*dwb>*/}catch(e){script.debug(e);};"

#define SCRIPT_TEMPLATE SCRIPT_TEMPLATE_START"//!javascript\n"SCRIPT_TEMPLATE_END
#define SCRIPT_TEMPLATE_INCLUDE SCRIPT_TEMPLATE_START SCRIPT_TEMPLATE_END
#define SCRIPT_TEMPLATE_XINCLUDE SCRIPT_TEMPLATE_XSTART SCRIPT_TEMPLATE_END

#define SCRIPT_WEBVIEW(o) (WEBVIEW(((GList*)JSObjectGetPrivate(o))))
#define EXCEPTION(X)   "DWB EXCEPTION : "X
#define PROP_LENGTH 128
#define G_FILE_TEST_VALID (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_EXECUTABLE | G_FILE_TEST_EXISTS) 
#define TRY_CONTEXT_LOCK (pthread_rwlock_tryrdlock(&s_context_lock) == 0)
#define CONTEXT_UNLOCK (pthread_rwlock_unlock(&s_context_lock))
#define IS_KEY_EVENT(X) (((int)(X)) == GDK_KEY_PRESS || ((int)(X)) == GDK_KEY_RELEASE)
#define IS_BUTTON_EVENT(X) (((int)(X)) == GDK_BUTTON_PRESS || ((int)(X)) == GDK_BUTTON_RELEASE \
                            || ((int)(X)) == GDK_2BUTTON_PRESS || ((int)(X)) == GDK_3BUTTON_PRESS)

#define SERVER_DO_SHUTDOWN(server) do { soup_server_disconnect(server); g_object_unref(server); } while(0)

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

static pthread_rwlock_t s_context_lock = PTHREAD_RWLOCK_INITIALIZER;

typedef struct SigData_s {
    gulong id; 
    GObject *instance;
} SigData;

typedef struct _CallbackData CallbackData;
typedef struct _SSignal SSignal;
typedef gboolean (*StopCallbackNotify)(CallbackData *);

struct _CallbackData {
    GObject *gobject;
    JSObjectRef object;
    JSObjectRef callback;
    StopCallbackNotify notify;
};
struct _SSignal {
    int id;
    GSignalQuery *query;
    JSObjectRef object;
    JSObjectRef func;
};
typedef struct DeferredPriv_s 
{
    JSObjectRef reject;
    JSObjectRef resolve;
    JSObjectRef next;
    gboolean is_fulfilled;
} DeferredPriv;
typedef struct PathCallback_s 
{
    JSObjectRef callback; 
    gboolean dir_only;
} PathCallback;

typedef struct SpawnData_s {
    GIOChannel *channel;
    JSObjectRef callback;
    JSObjectRef deferred;
    int status; 
    gboolean is_stdout;
    int finished;
} SpawnData;
#define SPAWN_FINISHED 0x1
#define SPAWN_CLOSED 0x2

#define SPAWN_DATA_INIT(_data, _channel, _callback, _deferred, _is_stdout) do { \
    _data->channel = _channel; \
    _data->callback = _callback; \
    _data->deferred = _deferred; \
    _data->is_stdout = _is_stdout; \
    _data->status = 0; \
    _data->finished = 0; } while (0)


//static GSList *s_signals;
#define S_SIGNAL(X) ((SSignal*)X->data)

static const struct {
    int sig;
    const char *name;
} s_sigmap[] = {
    { SCRIPTS_SIG_NAVIGATION, "navigation" },
    { SCRIPTS_SIG_LOAD_STATUS, "loadStatus" },
    { SCRIPTS_SIG_MIME_TYPE, "mimeType" },
    { SCRIPTS_SIG_DOWNLOAD, "download" }, 
    { SCRIPTS_SIG_DOWNLOAD_START, "downloadStart" }, 
    { SCRIPTS_SIG_DOWNLOAD_STATUS, "downloadStatus" }, 
    { SCRIPTS_SIG_RESOURCE, "resource" },
    { SCRIPTS_SIG_KEY_PRESS, "keyPress" },
    { SCRIPTS_SIG_KEY_RELEASE, "keyRelease" },
    { SCRIPTS_SIG_BUTTON_PRESS, "buttonPress" },
    { SCRIPTS_SIG_BUTTON_RELEASE, "buttonRelease" },
    { SCRIPTS_SIG_TAB_FOCUS, "tabFocus" },
    { SCRIPTS_SIG_FRAME_STATUS, "frameStatus" },
    { SCRIPTS_SIG_LOAD_FINISHED, "loadFinished" },
    { SCRIPTS_SIG_LOAD_COMMITTED, "loadCommitted" },
    { SCRIPTS_SIG_CLOSE_TAB, "closeTab" },
    { SCRIPTS_SIG_CREATE_TAB, "createTab" },
    { SCRIPTS_SIG_FRAME_CREATED, "frameCreated" },
    { SCRIPTS_SIG_CLOSE, "close" },
    { SCRIPTS_SIG_DOCUMENT_LOADED, "documentLoaded" },
    { SCRIPTS_SIG_MOUSE_MOVE, "mouseMove" },
    { SCRIPTS_SIG_STATUS_BAR, "statusBarChange" },
    { SCRIPTS_SIG_TAB_BUTTON_PRESS, "tabButtonPress" },
    { SCRIPTS_SIG_CHANGE_MODE, "changeMode" },
    { SCRIPTS_SIG_EXECUTE_COMMAND, "executeCommand" },
    { SCRIPTS_SIG_CONTEXT_MENU, "contextMenu" },
    { SCRIPTS_SIG_ERROR,    "error" },
    { SCRIPTS_SIG_SCROLL,    "scroll" },
    { SCRIPTS_SIG_FOLLOW,    "followHint" },
    { SCRIPTS_SIG_ADD_COOKIE,    "addCookie" },
    { 0, NULL },
};

enum {
    CONSTRUCTOR_DEFAULT = 0,
    CONSTRUCTOR_WEBVIEW,
    CONSTRUCTOR_DOWNLOAD,
    CONSTRUCTOR_WIDGET,
    CONSTRUCTOR_FRAME,
    CONSTRUCTOR_SOUP_MESSAGE,
    CONSTRUCTOR_HISTORY_LIST,
    CONSTRUCTOR_DEFERRED,
    CONSTRUCTOR_HIDDEN_WEB_VIEW,
    CONSTRUCTOR_SOUP_HEADERS,
    CONSTRUCTOR_SERVER,
    CONSTRUCTOR_COOKIE,
    CONSTRUCTOR_LAST,
};
enum {
    SELECTION_PRIMARY = 1,
    SELECTION_CLIPBOARD = 2
};

static void callback(CallbackData *c);
static void make_callback(JSContextRef ctx, JSObjectRef this, GObject *gobject, const char *signalname, JSValueRef value, StopCallbackNotify notify, JSValueRef *exception);
static JSObjectRef make_object(JSContextRef ctx, GObject *o);
static JSObjectRef make_object_for_class(JSContextRef ctx, JSClassRef class, GObject *o, gboolean);
static void object_destroy_cb(JSObjectRef o);
static JSObjectRef make_boxed(gpointer boxed, JSClassRef klass);

/* Static variables */
static JSObjectRef s_sig_objects[SCRIPTS_SIG_LAST];
static JSGlobalContextRef s_global_context;
static GSList *s_script_list;
static JSClassRef s_gobject_class, 
                  s_webview_class, 
                  s_frame_class, 
                  s_download_class, 
                  s_download_class, 
                  s_widget_class, 
                  s_menu_class, 
                  s_secure_widget_class, 
                  s_message_class, 
                  s_deferred_class, 
                  s_history_class,
                  s_soup_header_class, 
                  s_soup_server_class,
                  s_cookie_class;
static JSObjectRef s_array_contructor;
static JSObjectRef s_completion_callback;
static GQuark s_ref_quark;
static JSObjectRef s_init_before, s_init_after; // s_private;
static JSObjectRef s_constructors[CONSTRUCTOR_LAST];
static JSObjectRef s_soup_session;
static GSList *s_timers = NULL;
static GPtrArray *s_gobject_signals = NULL;
static gboolean s_debugging = false;
static GHashTable *s_exports = NULL;
static GSList *s_servers = NULL;

/* Only defined once */
static JSValueRef UNDEFINED, NIL;

/* MISC {{{*/
/* uncamelize {{{*/
static char *
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

static char * 
get_body(JSContextRef ctx, JSObjectRef func, JSValueRef *exc)
{
    char *sfunc = NULL, *result = NULL;
    if (func == NULL)
        return NULL;
    JSStringRef js_string = JSValueToStringCopy(ctx, func, exc);
    sfunc = js_string_to_char(ctx, js_string, -1);
    if (!sfunc)
        goto error_out;
    const char *start = strchr(sfunc, '{');
    const char *end = strrchr(sfunc, '}');
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

static JSValueRef 
call_as_function_debug(JSContextRef ctx, JSObjectRef func, JSObjectRef this, size_t argc, const JSValueRef argv[])
{
    char path[PATH_MAX] = {0};
    int line = -1;
    JSValueRef exc = NULL;
    JSValueRef ret = JSObjectCallAsFunction(ctx, func, this, argc, argv, &exc);
    if (exc != NULL) 
    {
        if (!s_debugging)
            js_print_exception(ctx, exc, path, PATH_MAX, 0, &line);
        else 
        {
            //JSObjectRef p = JSObjectMake(ctx, NULL, NULL);
            //if (*path)
            //    js_set_object_property(ctx, p, "path", path, NULL);
            //if (line>=0)
            //    js_set_object_number_property(ctx, p, "line", line, NULL);
            //JSValueRef argv[] = { p };
            //JSObjectCallAsFunction(ctx, s_private, NULL, 1, argv, NULL);
        }
    }
    return ret;
}

/* inject {{{*/
static JSValueRef
inject(JSContextRef ctx, JSContextRef wctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    gboolean global = false;
    JSValueRef args[1];
    JSValueRef e = NULL;
    JSObjectRef f;
    JSStringRef script;
    char *name = NULL;
    char *body = NULL;
    int count = 0;
    double debug = -1;
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("inject: missing argument"));
        return NIL;
    }
    if (argc > 1 && !JSValueIsNull(ctx, argv[1])) 
    {
        args[0] = js_context_change(ctx, wctx, argv[1], exc);
        count = 1;
    }
    if (argc > 2)
        debug = JSValueToNumber(ctx, argv[2], exc);
    if (argc > 3 && JSValueIsBoolean(ctx, argv[3])) 
        global = JSValueToBoolean(ctx, argv[3]);

    if (JSValueIsObject(ctx, argv[0]) && (f = js_value_to_function(ctx, argv[0], exc)) != NULL)
    {
        body = get_body(ctx, f, exc);
        if (body == NULL)
            return NIL;
        script = JSStringCreateWithUTF8CString(body);
        name = js_get_string_property(ctx, f, "name");
    }
    else 
    {
        script = JSValueToStringCopy(ctx, argv[0], exc);
        if (script == NULL) 
            return NIL;
    }


    if (global) 
        JSEvaluateScript(wctx, script, NULL, NULL, 0, &e);
    else 
    {
        JSObjectRef func = JSObjectMakeFunction(wctx, NULL, 0, NULL, script, NULL, 0, NULL);
        if (func != NULL && JSObjectIsFunction(ctx, func)) 
        {
            JSValueRef wret = JSObjectCallAsFunction(wctx, func, func, count, count == 1 ? args : NULL, &e) ;
            if (exc != NULL) 
            {
                char *retx = js_value_to_json(wctx, wret, -1, NULL);
                // This could be replaced with js_context_change
                if (retx) 
                {
                    ret = js_char_to_value(ctx, retx);
                    g_free(retx);
                }
            }
        }
    }
    if (e != NULL && !isnan(debug) && debug > 0)
    {
        int line = 0;
        fprintf(stderr, "DWB SCRIPT EXCEPTION: An error occured injecting %s.\n", name == NULL || *name == '\0' ? "[anonymous]" :  name);
        js_print_exception(wctx, e, NULL, 0, (int)(debug-1), &line);

        fputs("==> DEBUG [SOURCE]\n", stderr);
        if (body == NULL)
            body = js_string_to_char(ctx, script, -1);
        char **lines = g_strsplit(body, "\n", -1);

        fprintf(stderr, "    %s\n", line < 3 ? "BOF" : "...");
        for (int i=MAX(line-2, 0); lines[i] != NULL && i < line + 1; i++)
            fprintf(stderr, "%s %d > %s\n", i == line-1 ? "-->" : "   ", i+ ((int) debug), lines[i]);
        fprintf(stderr, "    %s\n", line + 2 >= (int)g_strv_length(lines) ? "EOF" : "...");

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
deferred_destroy(JSContextRef ctx, JSObjectRef this, DeferredPriv *priv) 
{
    g_return_if_fail(this != NULL);

    if (priv == NULL)
        priv = JSObjectGetPrivate(this);

    if (priv->resolve)
        JSValueUnprotect(ctx, priv->resolve);
    if (priv->reject)
        JSValueUnprotect(ctx, priv->reject);
    if (priv->next)
        JSValueUnprotect(ctx, priv->next);

    JSObjectSetPrivate(this, NULL);

    g_free(priv);

    JSValueUnprotect(ctx, this);
}

static JSObjectRef
deferred_new(JSContextRef ctx) 
{
    DeferredPriv *priv = g_malloc(sizeof(DeferredPriv));
    priv->resolve = priv->reject = priv->next = NULL;

    JSObjectRef ret = JSObjectMake(ctx, s_deferred_class, priv);
    JSValueProtect(ctx, ret);
    priv->is_fulfilled = false;

    return ret;
}
static PathCallback * 
path_callback_new(JSContextRef ctx, JSObjectRef object, gboolean dir_only)
{
    g_return_val_if_fail(object != NULL, NULL);
    PathCallback *pc = g_malloc(sizeof(PathCallback));
    pc->callback = object;
    JSValueProtect(ctx, object);
    pc->dir_only = dir_only;
    return pc;
}
static void
path_callback_free(PathCallback *pc)
{
    if (pc != NULL && TRY_CONTEXT_LOCK)
    {
        if (s_global_context != NULL)
            JSValueUnprotect(s_global_context, pc->callback);
        CONTEXT_UNLOCK;
        g_free(pc);
    }
}
/** 
 * Registers functions for the done and fail chain
 *
 * @name then
 * @memberOf Deferred.prototype
 * @function
 *
 *
 * @param {Deferred~resolveCallback} ondone
 *      A callback function that will be called when the deferred is resolved.
 *      If the function returns a deferred the original deferred will be
 *      replaced with the new deferred.

 * @param {Deferred~rejectCallback} onfail
 *      A callback function that will be called when the deferred is rejected.
 *      If the function returns a deferred the original deferred will be
 *      replaced with the new deferred.
 * @returns {Deferred}
 *      A new deferred that can be used to chain callbacks.
 * */
/** 
 * Called when a Deferred is resolved
 * @callback Deferred~resolveCallback
 * @param {...Object} arguments 
 *      Variable number of arguments passed to Deferred.resolve
 * */
/** 
 * Called when a Deferred is rejected
 * @callback Deferred~rejectCallback
 * @param {...Object} arguments 
 *      Variable number of arguments passed to Deferred.reject
 * */
static JSValueRef 
deferred_then(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL) 
        return NIL;

    if (argc > 0)
    {
        priv->resolve = js_value_to_function(ctx, argv[0], NULL);
        JSValueProtect(ctx, priv->resolve);
    }
    if (argc > 1) 
    {
        priv->reject = js_value_to_function(ctx, argv[1], NULL);
        JSValueProtect(ctx, priv->reject);
    }

    priv->next = deferred_new(ctx);
    JSValueProtect(ctx, priv->next);

    return priv->next;
}
static DeferredPriv * 
deferred_transition(JSContextRef ctx, JSObjectRef old, JSObjectRef new)
{
    DeferredPriv *opriv = JSObjectGetPrivate(old);
    DeferredPriv *npriv = JSObjectGetPrivate(new);

    npriv->resolve = opriv->resolve;
    if (npriv->resolve)
        JSValueProtect(ctx, npriv->resolve);
    npriv->reject = opriv->reject;
    if (npriv->reject)
        JSValueProtect(ctx, npriv->reject);
    npriv->next = opriv->next;
    if (npriv->next)
        JSValueProtect(ctx, npriv->next);

    deferred_destroy(ctx, old, opriv);
    return npriv;
}
/**
 * Resolves a deferred, the done-chain is called when a deferred is resolved
 *
 * @name resolve
 * @memberOf Deferred.prototype
 * @function
 *
 * @param {...Object} arguments Arguments passed to the <i>done</i> callbacks
 */
static JSValueRef 
deferred_resolve(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;

    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL || priv->is_fulfilled)
        return UNDEFINED;

    if (priv->resolve) 
        ret = JSObjectCallAsFunction(ctx, priv->resolve, this, argc, argv, exc);

    priv->is_fulfilled = true;

    JSObjectRef next = priv->next;
    deferred_destroy(ctx, this, priv);

    if (next) 
    {
        if ( ret && JSValueIsObjectOfClass(ctx, ret, s_deferred_class) ) 
        {
            JSObjectRef o = JSValueToObject(ctx, ret, NULL);
            deferred_transition(ctx, next, o)->reject = NULL;
        }
        else 
        {
            if (ret && !JSValueIsUndefined(ctx, ret))
            {
                JSValueRef args[] = { ret };
                deferred_resolve(ctx, f, next, 1, args, exc);
            }
            else 
                deferred_resolve(ctx, f, next, argc, argv, exc);

        }
    }
    return UNDEFINED;
}
/**
 * Rejects a deferred, the fail-chain is called when a deferred is resolved
 *
 * @name reject
 * @memberOf Deferred.prototype
 * @function
 *
 * @param {...Object} arguments Arguments passed to the <i>fail</i> callbacks
 */
static JSValueRef 
deferred_reject(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;

    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL || priv->is_fulfilled)
        return UNDEFINED;

    if (priv->reject) 
        ret = JSObjectCallAsFunction(ctx, priv->reject, this, argc, argv, exc);

    priv->is_fulfilled = true;

    JSObjectRef next = priv->next;
    deferred_destroy(ctx, this, priv);

    if (next) 
    {
        if ( ret && JSValueIsObjectOfClass(ctx, ret, s_deferred_class) ) 
        {
            JSObjectRef o = JSValueToObject(ctx, ret, NULL);
            deferred_transition(ctx, next, o)->resolve = NULL;
        }
        else 
        {
            if (ret && !JSValueIsUndefined(ctx, ret))
            {
                JSValueRef args[] = { ret };
                deferred_reject(ctx, f, next, 1, args, exc);
            }
            else 
                deferred_reject(ctx, f, next, argc, argv, exc);
        }
    }
    return UNDEFINED;
}/*}}}*/
/** 
 * Wether this Deferred was resolved or rejected.
 *
 * @name isFulfilled
 * @memberOf Deferred.prototype
 * @type Boolean
 * @readonly
 * */

static JSValueRef 
deferred_is_fulfilled(JSContextRef ctx, JSObjectRef this, JSStringRef js_name, JSValueRef* exception) 
{
    gboolean resolved = true;
    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv != NULL)
        resolved = priv->is_fulfilled;
    return JSValueMakeBoolean(ctx, resolved);
}

void
sigdata_append(gulong sigid, GObject *instance)
{
    SigData *data = g_malloc(sizeof(SigData));
    data->id = sigid;
    data->instance = instance;
    g_ptr_array_add(s_gobject_signals, data);

}
void
sigdata_remove(gulong sigid, GObject *instance)
{
    for (guint i=0; i<s_gobject_signals->len; i++)
    {
        SigData *data = g_ptr_array_index(s_gobject_signals, i);
        if (data->id == sigid && data->instance == instance)
        {
            g_ptr_array_remove_index_fast(s_gobject_signals, i);
            g_free(data);
            return;
        }
    }
}

/* CALLBACK {{{*/
/* callback_data_new {{{*/
static CallbackData * 
callback_data_new(GObject *gobject, JSObjectRef object, JSObjectRef callback, StopCallbackNotify notify)  
{
    CallbackData *c = NULL;
    if (!TRY_CONTEXT_LOCK)
        return c;
    if (s_global_context == NULL)
        goto error_out;

    c = g_malloc(sizeof(CallbackData));
    c->gobject = gobject != NULL ? g_object_ref(gobject) : NULL;
    if (object != NULL) 
    {
        JSValueProtect(s_global_context, object);
        c->object = object;
    }
    if (object != NULL) 
    {
        JSValueProtect(s_global_context, callback);
        c->callback = callback;
    }
    c->notify = notify;
error_out:
    CONTEXT_UNLOCK;
    return c;
}/*}}}*/

/* callback_data_free {{{*/
static void
callback_data_free(CallbackData *c) 
{
    if (c != NULL) 
    {
        if (c->gobject != NULL) 
            g_object_unref(c->gobject);
        if (TRY_CONTEXT_LOCK)
        {
            if (s_global_context == NULL)
            {
                if (c->object != NULL) 
                    JSValueUnprotect(s_global_context, c->object);
                if (c->callback != NULL) 
                    JSValueUnprotect(s_global_context, c->callback);
            }
            CONTEXT_UNLOCK;
        }
        g_free(c);
    }
}/*}}}*/

static void 
ssignal_free(SSignal *sig) 
{
    if (sig != NULL) 
    {
        if (TRY_CONTEXT_LOCK)
        {
            if (s_global_context == NULL)
            {
                if (sig->func)
                    JSValueUnprotect(s_global_context, sig->func);
                if (sig->object)
                    JSValueUnprotect(s_global_context, sig->object);
            }
            CONTEXT_UNLOCK;
        }
        if (sig->query)
            g_free(sig->query);
        g_free(sig);
    }
}

static SSignal * 
ssignal_new()
{
    SSignal *sig = g_malloc(sizeof(SSignal)); 
    sig->query = g_malloc(sizeof(GSignalQuery));
    sig->func = NULL;
    sig->object = NULL;
    return sig;
}
static SSignal * 
ssignal_new_with_query(guint signal_id)
{
    SSignal *s = ssignal_new();
    if (s)
    {
        g_signal_query(signal_id, s->query);
    }
    return s;
}

/* make_callback {{{*/
static void 
make_callback(JSContextRef ctx, JSObjectRef this, GObject *gobject, const char *signalname, JSValueRef value, StopCallbackNotify notify, JSValueRef *exception) 
{
    JSObjectRef func = js_value_to_function(ctx, value, exception);
    if (func != NULL) 
    {
        CallbackData *c = callback_data_new(gobject, this, func, notify);
        if (c != NULL)
            g_signal_connect_swapped(gobject, signalname, G_CALLBACK(callback), c);
    }
}/*}}}*/

/* callback {{{*/
/** 
 * @callback WebKitDownload~statusCallback
 * @param {WebKitDownload} download 
 *      The download
 * */
static void 
callback(CallbackData *c) 
{
    gboolean ret = false;
    if (!TRY_CONTEXT_LOCK)
        return;
    if (s_global_context != NULL)
    {
        JSValueRef val[] = { c->object != NULL ? c->object : NIL };
        JSValueRef jsret = call_as_function_debug(s_global_context, c->callback, c->callback, 1, val);
        if (JSValueIsBoolean(s_global_context, jsret))
            ret = JSValueToBoolean(s_global_context, jsret);
        if (ret || (c != NULL && c->gobject != NULL && c->notify != NULL && c->notify(c))) 
        {
            g_signal_handlers_disconnect_by_func(c->gobject, callback, c);
            callback_data_free(c);
        }
    }
    CONTEXT_UNLOCK;
}/*}}}*/
/*}}}*/

/* TABS {{{*/
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

/* tabs_get_nth {{{*/
/** 
 * Gets the WebKitWebView of the nth tab, counting at 0
 * @name nth
 * @memberOf tabs
 * @function
 *
 * @param {Number} number Number of the tab
 *
 * @returns {WebKitWebView} 
 *      The corresponding {@link WebKitWebView}
 * */
static JSValueRef 
tabs_get_nth(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) 
        return NIL;

    double n = JSValueToNumber(ctx, argv[0], exc);
    if (isnan(n))
        return NIL;
    GList *nth = g_list_nth(dwb.state.views, (int)n);
    if (nth == NULL)
        return NIL;
    return VIEW(nth)->script_wv;
}/*}}}*/
/*}}}*/

/* WEBVIEW {{{*/

static GList *
find_webview(JSObjectRef o) 
{
    for (GList *r = dwb.state.fview; r; r=r->next)
        if (VIEW(r)->script_wv == o)
            return r;
    for (GList *r = dwb.state.fview->prev; r; r=r->prev)
        if (VIEW(r)->script_wv == o)
            return r;
    return NULL;
}
/* wv_status_cb {{{*/
/** 
 * Callback that will be called if the load-status changes, return true to stop
 * the emission
 *
 * @callback WebKitWebView#loadUriCallback 
 * @param {WebKitWebView} wv The webview which loaded the uri
 * */
static gboolean 
wv_status_cb(CallbackData *c) 
{
    WebKitLoadStatus status = webkit_web_view_get_load_status(WEBKIT_WEB_VIEW(c->gobject));
    if (status == WEBKIT_LOAD_FINISHED || status == WEBKIT_LOAD_FAILED) 
        return true;
    return false;
}/*}}}*/

/* wv_load_uri {{{*/
/**
 * Load an uri in a webview
 * @name loadUri
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * @param {String} uri 
 *      The uri to load
 * @param {WebKitWebView#loadUriCallback} [callback] 
 *      A callback function that will be called when the load status changes,
 *      return true to stop the emission
 *
 * @returns {Boolean}
 *      true if the uri was loaded
 * */
static JSValueRef 
wv_load_uri(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) 
        return JSValueMakeBoolean(ctx, false);

    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL) 
    {
        char *uri = js_value_to_char(ctx, argv[0], -1, exc);
        if (uri == NULL)
            return false;
        webkit_web_view_load_uri(wv, uri);
        g_free(uri);
        if (argc > 1)  
            make_callback(ctx, this, G_OBJECT(wv), "notify::load-status", argv[1], wv_status_cb, exc);

        return JSValueMakeBoolean(ctx, true);
    }
    return JSValueMakeBoolean(ctx, false);
}/*}}}*/

/**
 * Stops any ongoing loading
 *
 * @name stopLoading
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * */
static JSValueRef 
wv_stop_loading(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);;
    if (wv != NULL)
        webkit_web_view_stop_loading(wv);
    return UNDEFINED;
}

/**
 * Loads a history item, can be used to navigate forward/backwards in history
 *
 * @name history
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * @param {Number} steps 
 *      Number of steps, pass a negative value to go back in history
 * */
/* wv_history {{{*/
static JSValueRef 
wv_history(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("webview.history: missing argument."));
        return UNDEFINED;
    }
    double steps = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(steps)) {
        WebKitWebView *wv = JSObjectGetPrivate(this);
        if (wv != NULL)
            webkit_web_view_go_back_or_forward(wv, (int)steps);
    }
    return UNDEFINED;
}/*}}}*/

/**
 * Reloads the current site
 *
 * @name reload
 * @memberOf WebKitWebView.prototype
 * @function 
 *
 * */
/* wv_reload {{{*/
static JSValueRef 
wv_reload(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL)
        webkit_web_view_reload(wv);
    return UNDEFINED;
}/*}}}*/

/* wv_inject {{{*/
static JSValueRef 
wv_inject(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv = JSObjectGetPrivate(this);
    if (wv != NULL) 
    {
        JSContextRef wctx = webkit_web_frame_get_global_context(webkit_web_view_get_main_frame(wv));
        return inject(ctx, wctx, function, this, argc, argv, exc);
    }
    return NIL;
}/*}}}*/
#if WEBKIT_CHECK_VERSION(1, 10, 0) && CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)

cairo_surface_t *
wv_to_surface(JSContextRef ctx, WebKitWebView *wv, unsigned int argc, const JSValueRef *argv, JSValueRef *exc)
{
    cairo_surface_t *sf, *scaled_surface = NULL; 
    cairo_t *cr;
    int w, h; 
    gboolean keep_aspect = false;
    double aspect, new_width, new_height, width, height;
    double sw, sh;

    if (argc > 1)
    {
        width = JSValueToNumber(ctx, argv[0], exc);
        height = JSValueToNumber(ctx, argv[1], exc);
        if (!isnan(width) && !isnan(height)) 
        {
            if (argc > 2 && JSValueIsBoolean(ctx, argv[2])) 
                keep_aspect = JSValueToBoolean(ctx, argv[2]);

            if (keep_aspect && (width <= 0 || height <= 0))
                return NULL;

            sf = webkit_web_view_get_snapshot(wv);
            w = cairo_image_surface_get_width(sf);
            h = cairo_image_surface_get_height(sf);

            aspect = (double)w/h;
            new_width = width;
            new_height = height;

            if (width <= 0 || keep_aspect)
                new_width = height * aspect;
            if ((width > 0 && height <= 0) || keep_aspect)
                new_height = width / aspect;
            if (keep_aspect) 
            {
                if (new_width > width) 
                {
                    new_width = width;
                    new_height = new_width / aspect;
                }
                else if (new_height > height) 
                {
                    new_height = height;
                    new_width = new_height * aspect;
                }
            }

            if (width <= 0 || height <= 0)
                sw = sh = MIN(width / w, height / h);
            else 
            {
                sw = width / w;
                sh = height / h;
            }

            scaled_surface = cairo_surface_create_similar_image(sf, CAIRO_FORMAT_RGB24, new_width, new_height);
            cr = cairo_create(scaled_surface);

            cairo_save(cr);
            cairo_scale(cr, sw, sh);

            cairo_set_source_surface(cr, sf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

            cairo_destroy(cr);
            cairo_surface_destroy(sf);
        }
    }
    else 
    {
        scaled_surface = webkit_web_view_get_snapshot(wv);
    }
    return scaled_surface;
}

cairo_status_t 
write_png64(GString *buffer, const unsigned char *data, unsigned int length)
{
    char *base64 = g_base64_encode(data, length);
    if (base64 != NULL)
    {
        g_string_append(buffer, base64);
        g_free(base64);
        return CAIRO_STATUS_SUCCESS;
    }
    return CAIRO_STATUS_WRITE_ERROR;
}
/** 
 * Renders a webview to a base64 encoded png
 *
 * @name toPng64
 * @memberOf WebKitWebView.prototype
 * @function 
 * @type String
 * @requires webkitgtk >= 1.10
 * @example
 * var png = tabs.current.toPng64(250, 250, true); 
 * tabs.current.inject(function() {
 *     var img = document.createElement("img");
 *     img.src = "data:image/png;base64," + arguments[0];
 *     document.body.appendChild(img);
 * }, png);
 *
 *
 * @param {Number} width
 *      The width of the png, if width is < 0 and height is > 0 the image will have the same aspect ratio as the original webview, optional.
 * @param {Number} height
 *      The height of the png, if height is < 0 and width is > 0 the image will have the same aspect ratio as the original webview, optional, mandatory if width is set.
 * @param {Boolean} keepAspect
 *      Whether to keep the ascpect ratio, if set to true the new image will have the same aspect ratio as the original webview, width and height are taken as maximum sizes and must both be > 0, optional.
 *
 * @returns A base64 encoded png-String
 *
 * */
static JSValueRef 
wv_to_png64(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv;
    cairo_status_t status = -1;
    cairo_surface_t *scaled_surface;
    JSValueRef result = NIL;

    wv = JSObjectGetPrivate(this);
    g_return_val_if_fail(wv != NULL, NIL);

    scaled_surface = wv_to_surface(ctx, wv, argc, argv, exc);
    if (scaled_surface != NULL)
    {
        GString *s = g_string_new(NULL);
        status = cairo_surface_write_to_png_stream(scaled_surface, (cairo_write_func_t)write_png64, s);
        cairo_surface_destroy(scaled_surface);

        if (status == CAIRO_STATUS_SUCCESS)
            result = js_char_to_value(ctx, s->str);
        g_string_free(s, true);
    }
    return result;
}
/** 
 * Renders a webview to a png file
 *
 * @name toPng
 * @memberOf WebKitWebView.prototype
 * @function 
 * @type Number
 * @requires webkitgtk >= 1.10
 *
 *
 * @param {String} filename
 *      The filename for the png.
 * @param {Number} width
 *      The width of the png, if width is < 0 and height is > 0 the image will have the same aspect ratio as the original webview, optional.
 * @param {Number} height
 *      The height of the png, if height is < 0 and width is > 0 the image will have the same aspect ratio as the original webview, optional, mandatory if width is set.
 * @param {Boolean} keepAspect
 *      Whether to keep the ascpect ratio, if set to true the new image will have the same aspect ratio as the original webview, width and height are taken as maximum sizes and must both be > 0, optional.
 *
 * @returns A cairo_status_t, 0 on success, -1 if an error occured
 * */
static JSValueRef 
wv_to_png(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitWebView *wv;
    cairo_status_t status = -1;
    cairo_surface_t *scaled_surface;
    char *filename;
    if (argc < 1 || (wv = JSObjectGetPrivate(this)) == NULL || (JSValueIsNull(ctx, argv[0])) || (filename = js_value_to_char(ctx, argv[0], -1, NULL)) == NULL) 
    {
        return JSValueMakeNumber(ctx, status);
    }
    scaled_surface = wv_to_surface(ctx, wv, argc-1, argc > 1 ? argv + 1 : NULL, exc);
    if (scaled_surface != NULL)
    {
        status = cairo_surface_write_to_png(scaled_surface, filename);
        cairo_surface_destroy(scaled_surface);
    }
    else 
    {
        return JSValueMakeNumber(ctx, -1);
    }
    return JSValueMakeNumber(ctx, status);
}
#endif
/* wv_get_main_frame {{{*/
/** 
 * The main frame
 *
 * @name mainFrame
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebFrame
 * */
static JSValueRef 
wv_get_main_frame(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv != NULL) 
    {
        WebKitWebFrame *frame = webkit_web_view_get_main_frame(wv);
        return make_object(ctx, G_OBJECT(frame));
    }
    return NIL;
}/*}}}*/

/** 
 * The focused frame
 *
 * @name focusedFrame
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebFrame
 * */
/* wv_get_focused_frame {{{*/
static JSValueRef 
wv_get_focused_frame(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv != NULL) 
    {
        WebKitWebFrame *frame = webkit_web_view_get_focused_frame(wv);
        return make_object(ctx, G_OBJECT(frame));
    }
    return NIL;
}/*}}}*/

/* wv_get_all_frames {{{*/
/** 
 * All frames of a webview, including the main frame
 *
 * @name allFrames
 * @memberOf WebKitWebView.prototype
 * @type Array[WebKitWebFrame]
 * */
static JSValueRef 
wv_get_all_frames(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    int argc, n = 0;
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    argc = g_slist_length(VIEW(gl)->status->frames);

    JSValueRef argv[argc];
    n=0;

    for (GSList *sl = VIEW(gl)->status->frames; sl; sl=sl->next) 
        argv[n++] = make_object(ctx, G_OBJECT(sl->data));

    return JSObjectMakeArray(ctx, argc, argv, exception);
}/*}}}*/

/** 
 * The tabnumber of the webview, starting at 0
 * 
 * @name number
 * @memberOf WebKitWebView.prototype
 * @type Number
 *
 * */
/* wv_get_number {{{*/
static JSValueRef 
wv_get_number(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = dwb.state.views;
    for (int i=0; gl; i++, gl=gl->next) 
    {
        if (object == VIEW(gl)->script_wv) 
            return JSValueMakeNumber(ctx, i); 
    }
    return JSValueMakeNumber(ctx, -1); 
}/*}}}*/

/** 
 * The main widget for tab labels, used for coloring tabs, child of gui.tabBox.
 *
 * @name tabWidget
 * @memberOf WebKitWebView.prototype
 * @type GtkEventBox
 * */
static JSValueRef 
wv_get_tab_widget(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(VIEW(gl)->tabevent), true);
}
/** 
 * Horizontal box, child of wv.tabWidget.
 *
 * @name tabBox
 * @memberOf WebKitWebView.prototype
 * @type GtkBox
 * */
static JSValueRef 
wv_get_tab_box(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(VIEW(gl)->tabbox), true);
}
/** 
 * Text label of a tab, child of wv.tabBox.
 *
 * @name tabLabel
 * @memberOf WebKitWebView.prototype
 * @type GtkLabel
 * */
static JSValueRef 
wv_get_tab_label(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(VIEW(gl)->tablabel), true);
}
/** 
 * Favicon widget, child of wv.tabBox
 *
 * @name tabIcon
 * @memberOf WebKitWebView.prototype
 * @type GtkImage
 * */
static JSValueRef 
wv_get_tab_icon(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(VIEW(gl)->tabicon), true);
}

/** 
 * The parent widget of every webview, it is used for scrolling the webview
 *
 * @name scrolledWindow
 * @memberOf WebKitWebView.prototype
 * @type GtkScrolledWindow
 * */
static JSValueRef 
wv_get_scrolled_window(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    GList *gl = find_webview(object);
    if (gl == NULL)
        return NIL;
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(VIEW(gl)->scroll), true);
}
/** 
 * The history of the webview
 *
 * @name historyList
 * @memberOf WebKitWebView.prototype
 * @type WebKitWebBackForwardList
 * */
static JSValueRef 
wv_get_history_list(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    WebKitWebView *wv = JSObjectGetPrivate(object);
    if (wv == NULL)
        return NIL;
    return make_object(ctx, G_OBJECT(webkit_web_view_get_back_forward_list(wv)));
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
        return JSObjectMake(ctx, s_soup_header_class, headers);
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
        return JSObjectMake(ctx, s_soup_header_class, headers);
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
 *    var text = arguments[0];
 *    document.body.innerHTML = text;
 * }
 * signals.connect("documentLoaded", function(wv) {
 *    wv.inject(injectable, "foo", 3);
 * });
 *
 * @param {String|Function} code
 *      The script to inject, either a string or a function. If it is a function
 *      the body will be wrapped inside a new function.
 * @param {Object} arg
 *      If the script isn’t injected into the global scope the script is wrapped
 *      inside a function. arg then is accesible via arguments in the injected
 *      script, optional
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
 * @param {Number} [encoding]
 *      The character encoding, if omitted or null <i>UTF-8</i> is assumed.
 * @param {Boolean} [baseUri]
 *      The base uri, if present it must either use the uri-scheme <i>dwb-chrome:</i>
 *      or <i>file:</i>, otherwise the request will be ignored. 
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
        return UNDEFINED;
    WebKitWebFrame *frame = JSObjectGetPrivate(this);
    if (frame != NULL) 
    {
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
                {
                    base_uri = js_value_to_char(ctx, argv[3], -1, exc);
                }
            }
        }
        webkit_web_frame_load_string(frame, content, mime_type, encoding, base_uri ? base_uri : "");
        g_free(content);
        g_free(mime_type);
        g_free(encoding);
        g_free(base_uri);
    }
    return UNDEFINED;
}
/*}}}*/

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
    if (! (m->map->prop & CP_OVERRIDE))
        CLEAR_COMMAND_TEXT();

    if (s_global_context != NULL)
    {
        if (arg->p == NULL) 
            json = util_create_json(3, CHAR, "key", m->key, 
                    INTEGER, "modifier", m->mod,  
                    INTEGER, "nummod", dwb.state.nummod);
        else 
            json = util_create_json(4, CHAR, "key", m->key, 
                    INTEGER, "modifier", m->mod,  
                    INTEGER, "nummod", dwb.state.nummod, 
                    CHAR, "arg", arg->p);

        JSValueRef argv[] = { js_json_to_value(s_global_context, json) };
        call_as_function_debug(s_global_context, arg->js, arg->js, 1, argv);
    }

    g_free(json);

    return STATUS_OK;
}/*}}}*/

static void 
unbind_free_keymap(JSContextRef ctx, GList *l)
{
    KeyMap *m = l->data;
    JSValueUnprotect(ctx, m->map->arg.js);
    g_free(m->map->n.first);
    g_free(m->map->n.second);
    g_free(m->map);
    g_free(m);
    dwb.keymap = g_list_delete_link(dwb.keymap, l);
}
/* global_unbind{{{*/
/** 
 * Unbind a shortcut previously bound with <b>bind</b>
 * @name unbind 
 * @function
 *
 * @param {String|bindCallback} command|callback Either the function or the command
 *                          passed to {@link bind}
 * @returns {Boolean}
 *      Whether the shortcut was unbound
 *
 * */
static JSValueRef 
global_unbind(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
        return JSValueMakeBoolean(ctx, false);

    GList *l = NULL;
    if (JSValueIsString(ctx, argv[0])) 
    {
        char *name = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);
        for (l = dwb.keymap; l && g_strcmp0(((KeyMap*)l->data)->map->n.first, name); l=l->next)
            ;
        g_free(name);
    }
    else if (JSValueIsObject(ctx, argv[0])) 
    {
        for (l = dwb.keymap; l; l=l->next)
            if ( ((KeyMap*)l->data)->map->arg.js && 
                    JSValueIsEqual(ctx, argv[0], ((KeyMap*)l->data)->map->arg.js, exc) )
                break;
    }
    if (l != NULL) 
    {
        for (GList *gl = dwb.override_keys; gl; gl=gl->next)
        {
            KeyMap *m = gl->data;
            if (m->map->prop & CP_SCRIPT) 
                dwb.override_keys = g_list_delete_link(dwb.override_keys, l);
        }
        unbind_free_keymap(ctx, l);
        return JSValueMakeBoolean(ctx, true);
    }
    return JSValueMakeBoolean(ctx, false);
}/*}}}*/
/* global_bind {{{*/
/** 
 * Bind a function to a shortcut or commandline command, use {@link unbind} to remove
 * the command
 * @name bind 
 * @function
 *
 * @param {String} shortcut 
 *      A shortcut, the syntax is the same as in dwb:keys, pass <i>null</i> if
 *      only a commandline command should be bound 
 * @param {bindCallback} callback 
 *      A callback function that will be called if the shortcut is pressed or
 *      the command was executed from commandline
 * @param {String|OverrideKey} [command|override] 
 *      A command the can be used on dwb's commandline or an 
 *      {@link Enums and Flags.OverrideKey|OverrideKey} flag 
 *
 * @returns {Boolean}
 *      true if the shortcut/command was bound
 * @example 
 * bind("Control U", function () { 
 *      execute("tabopen " + tabs.current.uri);
 * }, "tabopen_current");
 *
 * */
static JSValueRef 
global_bind(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gchar *keystr, *callback_name;
    gboolean ret = false;
    char *name = NULL, *callback = NULL;
    guint option = CP_DONT_SAVE | CP_SCRIPT;
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

    Arg a = { .js = func };
    KeyMap *map = dwb_add_key(keystr, name, callback, (Func)scripts_eval_key, option, &a);
    if (override)
        dwb.override_keys = g_list_prepend(dwb.override_keys, map);

    ret = true;
error_out:
    g_free(keystr);
    return JSValueMakeBoolean(ctx, ret);
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
 * The webkit session
 *
 * @name session 
 * @type SoupSession
 * @readonly
 *
 * */
static JSValueRef 
global_get_webkit_session(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return s_soup_session;
}
/* global_execute {{{*/
/** 
 * Executes a command
 * @name execute 
 * @function
 *
 * @param {String} name 
 *      A command, the command syntax is the same as the syntax on dwb's
 *      commandline
 *
 * @returns {Boolean}
 *      true if execution was successful
 *
 * */
static JSValueRef 
global_execute(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    DwbStatus status = STATUS_ERROR;
    if (argc < 1) 
        return JSValueMakeBoolean(ctx, false);

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

static JSValueRef 
do_include(JSContextRef ctx, const char *path, const char *script, gboolean global, gboolean is_archive, size_t argc, const JSValueRef *argv, JSValueRef *exc)
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
            ret = JSObjectCallAsFunction(ctx, function, function, argc, argv, exc);
        }
        g_free(debug);
    }
    JSStringRelease(js_script);
    return ret;
}
/* global_include {{{*/
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
 *     io.print("bar");
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
 *          io.print("bar");
 *      }
 * });
 *
 * // including script
 * include("/path/to/script");
 * require(["foo"], function(foo) {
 *      foo.foo();
 * });
 * */

static JSObjectRef
get_exports(JSContextRef ctx, const char *path)
{
    JSObjectRef ret = g_hash_table_lookup(s_exports, path);
    if (ret == NULL)
    {
        ret = JSObjectMake(ctx, NULL, NULL);
        JSValueProtect(ctx, ret);
        g_hash_table_insert(s_exports, g_strdup(path), ret);
    }
    return ret;
}


static JSValueRef 
global_include(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    gboolean global = false;
    char *path = NULL, *content = NULL; 
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
        content = (char*) exar_search_extract(path, "main.js", NULL);
        if (content == NULL)
        {
            js_make_exception(ctx, exc, EXCEPTION("include: main.js was not found in %s."), path);
            goto error_out;
        }
        exports[0] = get_exports(ctx, path);
        is_archive = true;
    }
    else if ( (content = util_get_file_content(path, NULL)) == NULL) 
    {
        js_make_exception(ctx, exc, EXCEPTION("include: reading %s failed."), path);
        goto error_out;
    }

    const char *tmp = content;
    if (*tmp == '#') 
    {
        do {
            tmp++;
        } while(*tmp && *tmp != '\n');
        tmp++;
    }

    ret = do_include(ctx, path, tmp, global, is_archive, is_archive ? 1 : 0, is_archive ? exports : NULL, exc);

error_out: 
    g_free(content);
    g_free(path);
    return ret;
}/*}}}*/

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
 *      Path of the file in the archive
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
 * xinclude("content/bar.js");
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
 *      io.print(foo.bar);
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
    char *archive = NULL, *path = NULL, *content = NULL;
    JSValueRef ret = NIL;
    off_t fs;

    if (argc < 2)
        return NIL;
    if ((archive = js_value_to_char(ctx, argv[0], -1, exc)) == NULL)
        goto error_out;
    if ((path = js_value_to_char(ctx, argv[1], -1, exc)) == NULL)
        goto error_out;

    content = (char*)exar_extract(archive, path, &fs);
    if (content != NULL)
    {
        JSValueRef exports[] = { get_exports(ctx, archive) };
        ret = do_include(ctx, archive, content, false, true, 1, exports, exc);
    }

error_out:
    g_free(archive);
    g_free(path);
    g_free(content);
    return ret;
}


/* global_send_request {{{*/
static JSValueRef 
get_message_data(SoupMessage *msg) 
{
    const char *name, *value;
    SoupMessageHeadersIter iter;
    JSObjectRef o = NULL, ho;
    JSValueRef ret = NIL;
    JSStringRef s;

    if (!TRY_CONTEXT_LOCK)
        return NIL;

    if (s_global_context != NULL)
    {
        o = JSObjectMake(s_global_context, NULL, NULL);
        js_set_object_property(s_global_context, o, "body", msg->response_body->data, NULL);

        ho = JSObjectMake(s_global_context, NULL, NULL);

        soup_message_headers_iter_init(&iter, msg->response_headers);
        while (soup_message_headers_iter_next(&iter, &name, &value)) 
            js_set_object_property(s_global_context, ho, name, value, NULL);

        s = JSStringCreateWithUTF8CString("headers");
        JSObjectSetProperty(s_global_context, o, s, ho, kJSDefaultProperty, NULL);
        JSStringRelease(s);
        ret = o;
    }
    CONTEXT_UNLOCK;

    return ret;
}
/**
 * Callback called when a response from a request was retrieved
 *
 * @callback net~onResponse
 *
 * @param {Object} data 
 * @param {String} data.body 
 *      The message body
 * @param {Object} data.he 
 *      An object that contains all response headers
 * @param {SoupMessage} message The soup message
 * */
static void
request_callback(SoupSession *session, SoupMessage *message, JSObjectRef function) 
{
    if (!TRY_CONTEXT_LOCK)
        return;

    if (s_global_context != NULL)
    {
        if (message->response_body->data != NULL) 
        {
            JSValueRef o = get_message_data(message);
            JSValueRef vals[] = { o, make_object(s_global_context, G_OBJECT(message))  };
            call_as_function_debug(s_global_context, function, function, 2, vals);
        }
        JSValueUnprotect(s_global_context, function);
    }
    CONTEXT_UNLOCK;
}
static void 
set_request(JSContextRef ctx, SoupMessage *msg, JSValueRef val, JSValueRef *exc)
{
    char *content_type = NULL, *body = NULL;
    JSObjectRef data = JSValueToObject(ctx, val, exc);
    if (data == NULL)
        return;
    content_type = js_get_string_property(ctx, data, "contentType");
    if (content_type != NULL)
    {
        body = js_get_string_property(ctx, data, "data");
        if (body != NULL) {
            soup_message_set_request(msg, content_type, SOUP_MEMORY_COPY, body, strlen(body));
        }
    }
    g_free(content_type);
    g_free(body);
}
/** 
 * Sends a http-request
 * @name sendRequest
 * @memberOf net
 * @function
 *
 * @param {String} uri          
 *      The uri the request will be sent to.
 * @param {net~onResponse} callback   
 *      A callback that will be called when the request is finished
 * @param {String} [method]     The http request method, default GET
 * @param {Object} [data]       An object if method is POST
 * @param {String} data.contentType The content type
 * @param {String} data.data    The data that will be sent with the request
 *
 * @returns {Boolean}
 *      true if the request was sent
 * */
static JSValueRef 
net_send_request(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gboolean ret = -1;
    char *method = NULL, *uri = NULL;
    SoupMessage *msg;
    JSObjectRef function;
    if (argc < 2) 
        return JSValueMakeNumber(ctx, -1);

    uri = js_value_to_char(ctx, argv[0], -1, exc);
    if (uri == NULL) 
        return JSValueMakeNumber(ctx, -1);

    function = js_value_to_function(ctx, argv[1], exc);
    if (function == NULL)
        goto error_out;

    if (argc > 2) 
        method = js_value_to_char(ctx, argv[2], -1, exc);

    msg = soup_message_new(method == NULL ? "GET" : method, uri);
    if (msg == NULL)
        goto error_out;
    if (argc > 3 && method != NULL && !g_ascii_strcasecmp("POST", method)) 
        set_request(ctx, msg, argv[3], exc);

    JSValueProtect(ctx, function);
    soup_session_queue_message(webkit_get_default_session(), msg, (SoupSessionCallback)request_callback, function);
    ret = 0;

error_out: 
    g_free(uri);
    g_free(method);
    return JSValueMakeNumber(ctx, ret);
}/*}}}*/

/** 
 * Sends a http-request synchronously
 * @name sendRequestSync
 * @memberOf net
 * @function
 *
 * @param {String} uri          The uri the request will be sent to.
 * @param {String} [method]     The http request method, default GET
 *
 * @returns {Object}
 *      Object that contains the response body, the response headers and the
 *      http status code of the request.  
 * */
static JSValueRef 
net_send_request_sync(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *method = NULL, *uri = NULL;
    SoupMessage *msg;
    guint status;
    JSValueRef val;
    JSObjectRef o;
    JSStringRef js_key;
    JSValueRef js_value;

    if (argc < 1) 
        return NIL;

    uri = js_value_to_char(ctx, argv[0], -1, exc);
    if (uri == NULL) 
        return NIL;

    if (argc > 1) 
        method = js_value_to_char(ctx, argv[1], -1, exc);

    msg = soup_message_new(method == NULL ? "GET" : method, uri);
    if (argc > 2)
        set_request(ctx, msg, argv[2], exc);

    status = soup_session_send_message(webkit_get_default_session(), msg);
    val = get_message_data(msg);

    js_key = JSStringCreateWithUTF8CString("status");
    js_value = JSValueMakeNumber(ctx, status);

    o = JSValueToObject(ctx, val, exc);
    JSObjectSetProperty(ctx, o, js_key, js_value, kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly, exc);

    JSStringRelease(js_key);
    return o;
}
/* timeout_callback {{{*/
/**
 * @callback timer~startCallback
 * @returns {Boolean}
 *      Return true to stop the timer
 * */
static gboolean
timeout_callback(JSObjectRef obj) 
{
    gboolean ret = false;
    if (!TRY_CONTEXT_LOCK)
        return ret;

    if (s_global_context != NULL)
    {
        JSValueRef val = call_as_function_debug(s_global_context, obj, obj, 0, NULL);
        if (val == NULL)
            ret = false;
        else 
            ret = !JSValueIsBoolean(s_global_context, val) || JSValueToBoolean(s_global_context, val);
    }
    CONTEXT_UNLOCK;

    return ret;
}/*}}}*/

/* global_timer_stop {{{*/
/**
 * Stops a timer started by {@link timerStart}
 * @name stop 
 * @memberOf timer
 * @function
 * 
 * @param {Number} id A timer id retrieved from {@link timer.start|start}
 *
 * @returns {Boolean}
 *      true if the timer was stopped
 * */
static JSValueRef 
timer_stop(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gdouble sigid;
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("timerStop: missing argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    if (!isnan(sigid = JSValueToNumber(ctx, argv[0], exc))) 
    {
        gboolean ret = g_source_remove((int)sigid);
        GSList *source = g_slist_find(s_timers, GINT_TO_POINTER(sigid));
        if (source)
            s_timers = g_slist_delete_link(s_timers, source);

        return JSValueMakeBoolean(ctx, ret);
    }
    return JSValueMakeBoolean(ctx, false);
}/*}}}*/

/**
 * Calls a function reqeatedly or after a timeout, similar to window.setInterval
 * or window.setTimeout that are available in the webcontext.
 * @name start 
 * @memberOf timer
 * @function
 * @example 
 * // equivalent to window.setInterval
 * timer.start(1000, function() {
 *      ...
 * });
 * // equivalent to window.setTimeout
 * timer.start(1000, function() {
 *      ...
 *      return false;
 * });
 * 
 * @param {Number} interval The interval in milliseconds, the minimum interval
 *                          is 10 milliseconds
 * @param {timer~startCallback} callback 
 *      The callback that will be called, if the callback returns <i>false</i>
 *      the timer will be stopped
 *
 * @returns {Number}
 *      A timer id that can be passed to {@link timer.stop|stop}
 * */

/* global_timer_start {{{*/
static JSValueRef 
timer_start(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2) 
    {
        js_make_exception(ctx, exc, EXCEPTION("timerStart: missing argument."));
        return JSValueMakeNumber(ctx, -1);
    }
    double msec = 10;
    if (isnan(msec = JSValueToNumber(ctx, argv[0], exc)))
        return JSValueMakeNumber(ctx, -1);

    JSObjectRef func = js_value_to_function(ctx, argv[1], exc);
    if (func == NULL)
        return JSValueMakeNumber(ctx, -1);

    JSValueProtect(ctx, func);

    int ret = g_timeout_add_full(G_PRIORITY_DEFAULT, (int)msec, (GSourceFunc)timeout_callback, func, (GDestroyNotify)scripts_unprotect);
    s_timers = g_slist_prepend(s_timers, GINT_TO_POINTER(ret));
    return JSValueMakeNumber(ctx, ret);
}/*}}}*/
/*}}}*/

/* UTIL {{{*/
/* net_domain_from_host {{{*/
/**
 * Gets the base domain name from a hostname where the base domain name is the
 * effective second level domain name, e.g. for www.example.com it will be
 * example.com, for www.example.co.uk it will be example.co.uk.
 *
 * @name domainFromHost 
 * @memberOf net
 * @function
 *
 * @param {String} hostname A hostname
 *
 * @returns {String} 
 *      The effective second level domain
 *
 * */
static JSValueRef 
net_domain_from_host(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1) 
    {
        js_make_exception(ctx, exc, EXCEPTION("domainFromHost: missing argument."));
        return JSValueMakeBoolean(ctx, false);
    }
    char *host = js_value_to_char(ctx, argv[0], -1, exc);
    const char *domain = domain_get_base_for_host(host);
    if (domain == NULL)
        return NIL;

    JSValueRef ret = js_char_to_value(ctx, domain);
    g_free(host);
    return ret;
}/*}}}*//*}}}*/

/**
 * Parses an uri to a {@link SoupUri} object
 *
 * @name parseUri 
 * @memberOf net
 * @function
 *
 * @param {String} uri The uri to parse
 *
 * @returns {@link SoupUri} 
 *      A parsed uri or null if the uri isn't valid according to RFC 3986.
 *
 * */
static JSValueRef 
net_parse_uri(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL;
    if (argc > 0)
    {
        char *uri = js_value_to_char(ctx, argv[0], -1, NULL);
        if (uri != NULL)
        {
            SoupURI *suri = soup_uri_new(uri);
            if (suri != NULL)
            {
                ret = suri_to_object(ctx, suri, exc);
                soup_uri_free(suri);
            }
            g_free(uri);
        }
    }
    return ret;
}
/**
 * Gets all cookies from the cookie jar. 
 *
 * @name allCookies 
 * @memberOf net
 * @function
 * @since 1.5
 *
 * @returns {Array[{@link Cookie}]}
 *      An array of {@link Cookie|cookies}
 *
 * */
static JSValueRef 
net_all_cookies(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;
    GSList *cookies = dwb_soup_get_all_cookies();
    if (cookies != NULL)
    {
        guint l = g_slist_length(cookies), i=0;
        JSValueRef *args = g_malloc(l * sizeof (JSValueRef));
        for (GSList *l = cookies; l; l=l->next, i++)
        {
            args[i] = scripts_make_cookie(l->data);
        }
        ret = JSObjectMakeArray(ctx, i, args, exc);
        g_free(args);
        g_slist_free(cookies);
    }
    else 
        ret = JSObjectMakeArray(ctx, 0, NULL, exc);
    return ret;
}
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
    if (!TRY_CONTEXT_LOCK) 
        return;

    if (s_global_context != NULL)
    {
        const char *text = GET_TEXT();
        JSValueRef val[] = { js_char_to_value(s_global_context, text) };
        call_as_function_debug(s_global_context, s_completion_callback, s_completion_callback, 1, val);
        completion_clean_completion(false);
        dwb_change_mode(NORMAL_MODE, true);
    }
    CONTEXT_UNLOCK;
}
/** 
 * Initializes tab completion.
 * @name tabComplete
 * @memberOf util
 * @function
 * 
 * @param {String} label 
 *      The command line label
 * @param {Array[Object]} items[] 
 *      An array of labels, 
 * @param {String} items[].left
 *      Left completion label
 * @param {String} items[].right
 *      Right completion label
 * @param {util~onTabComplete} callback Callback function, the first argument will be the
 *                            returned string from the url bar.
 * @param {Boolean} [readonly] Whether the items are readonly, default false 
 *
 * */
static JSValueRef 
sutil_tab_complete(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 3 || !JSValueIsInstanceOfConstructor(ctx, argv[1], s_array_contructor, exc)) 
    {
        js_make_exception(ctx, exc, EXCEPTION("tabComplete: invalid argument."));
        return UNDEFINED;
    }
    s_completion_callback = js_value_to_function(ctx, argv[2], exc);
    if (s_completion_callback == NULL)
        return UNDEFINED;

    dwb.state.script_comp_readonly = false;
    if (argc > 3 && JSValueIsBoolean(ctx, argv[3])) 
    {
        dwb.state.script_comp_readonly = JSValueToBoolean(ctx, argv[3]);
    }

    char *left, *right, *label;
    js_array_iterator iter;
    JSValueRef val;
    JSObjectRef cur = NULL;
    Navigation *n;

    label = js_value_to_char(ctx, argv[0], JS_STRING_MAX, exc);
    JSObjectRef o = JSValueToObject(ctx, argv[1], exc);
    js_array_iterator_init(ctx, &iter, o);
    while((val = js_array_iterator_next(&iter, exc))) 
    {
        cur = JSValueToObject(ctx, val, exc);
        if (cur == NULL)
            goto error_out;
        left = js_get_string_property(ctx, cur, "left");
        right = js_get_string_property(ctx, cur, "right");
        n = g_malloc(sizeof(Navigation));
        n->first = left; 
        n->second = right;
        dwb.state.script_completion = g_list_prepend(dwb.state.script_completion, n);
    }
    dwb.state.script_completion = g_list_reverse(dwb.state.script_completion);
    dwb_set_status_bar_text(dwb.gui.lstatus, label, NULL, NULL, true);

    entry_focus();
    completion_complete(COMP_SCRIPT, false);

error_out:
    js_array_iterator_finish(&iter);

    for (GList *l = dwb.state.script_completion; l; l=l->next) 
    {
        n = l->data;
        g_free(n->first); 
        g_free(n->second);
        g_free(n);
    }
    g_free(label);
    g_list_free(dwb.state.script_completion);
    dwb.state.script_completion = NULL;
    return UNDEFINED;
}
/**
 * Callback that will be called when <i>Return</i> or <i>Escape</i> was pressed after {@link util.pathComplete} was invoked.
 *
 * @callback util~onPathComplete 
 *
 * @param {String} path The path or <i>null</i> if <i>Escape</i> was pressed.
 *
 * @since 1.3
 * */
gboolean 
path_completion_callback(GtkEntry *entry, GdkEventKey *e, PathCallback *pc)
{
    gboolean evaluate = false, clear = false;
    if (e->keyval == GDK_KEY_Escape)
    {
        clear = true;
        goto finish;
    }
    else if (IS_RETURN_KEY(e))
    {
        evaluate = clear = true;
        goto finish;
    }
    else if (DWB_TAB_KEY(e))
    {
        completion_complete_path(e->state & GDK_SHIFT_MASK, pc->dir_only);
        return true;
    }
    else if (dwb_eval_override_key(e, CP_OVERRIDE_ENTRY))
        return true;
finish: 
    completion_clean_path_completion();
    if (clear)
    {
        JSValueRef argv[1];
        if (TRY_CONTEXT_LOCK)
        {
            if (s_global_context != NULL)
            {
                if (evaluate)
                {
                    const char *text = GET_TEXT();
                    argv[0] = js_char_to_value(s_global_context, text);
                }
                else 
                    argv[0] = NIL;

                call_as_function_debug(s_global_context, pc->callback, pc->callback, 1, argv);
            }
            else 
            {
                g_free(pc);
            }
            CONTEXT_UNLOCK;
        }
        entry_snoop_end(G_CALLBACK(path_completion_callback), pc);
        path_callback_free(pc);
    }
    return false;
}
/** 
 * Initializes filename completion.
 * @name pathComplete
 * @memberOf util
 * @function
 * 
 * @param {util~onPathComplete} callback 
 *       Callback function called when a path was chosen or escape was pressed
 * @param {String} [label]
 *      The command line label
 * @param {String} [initialPath]
 *      The initial path, defaults to the current working directory
 * @param {Boolean} [dirOnly]
 *      Whether to complete only directories, default false.
 *
 * @since 1.3
 * */
static JSValueRef 
sutil_path_complete(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *status_text = NULL, 
         *initial_path = NULL;
    gboolean dir_only = false;

    if (argc == 0)
        return UNDEFINED;

    JSObjectRef callback = js_value_to_function(ctx, argv[0], exc);
    if (callback == NULL)
        return UNDEFINED;

    if (argc > 1)
    {
        status_text = js_value_to_char(ctx, argv[1], -1, exc);
        if (status_text != NULL)
        {
            dwb_set_status_bar_text(dwb.gui.lstatus, status_text, NULL, NULL, true);
        }
    }
    if (argc > 2)
        initial_path = js_value_to_char(ctx, argv[2], -1, exc);

    if (argc > 3)
    {
        dir_only = JSValueToBoolean(ctx, argv[3]);
    }
    
    PathCallback *pc = path_callback_new(ctx, callback, dir_only);

    if (initial_path == NULL)
    {
        initial_path = g_get_current_dir();
    }
    entry_snoop(G_CALLBACK(path_completion_callback), pc);
    entry_set_text(initial_path ? initial_path : "/");

    g_free(status_text);
    g_free(initial_path);
    return UNDEFINED;
}
/**
 * Escapes text for usage with pango markup
 *
 * @name markupEscape 
 * @memberOf util 
 * @function
 *
 * @param {String} text The text to escape
 *
 * @returns {String} The escaped text or null
 *
 * */
static JSValueRef 
sutil_markup_escape(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *string = NULL, *escaped = NULL;
    if (argc > 0) 
    {
        string = js_value_to_char(ctx, argv[0], -1, exc);
        if (string != NULL) 
        {
            escaped = g_markup_escape_text(string, -1);
            g_free(string);
            if (escaped != NULL) 
            {
                JSValueRef ret = js_char_to_value(ctx, escaped);
                g_free(escaped);
                return ret;
            }
        }
    }
    return NIL;
}
/* global_checksum {{{*/
/** 
 * Computes a checksum of a string
 * @name checksum 
 * @memberOf util
 * @function
 *
 * @param {String} data The data 
 * @param {ChecksumType} [type] The {@link Enums and Flags.ChecksumType|ChecksumType}, defaults to sha256
 * @returns {Boolean} Whether the shortcut was unbound
 *
 * */
static JSValueRef 
sutil_checksum(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    char *checksum = NULL;
    guchar *original = NULL;
    JSValueRef ret;

    if (argc < 1) 
        return NIL;

    original = (guchar*)js_value_to_char(ctx, argv[0], -1, exc);
    if (original == NULL)
        return NIL;

    GChecksumType type = G_CHECKSUM_SHA256;
    if (argc > 1) 
    {
        type = JSValueToNumber(ctx, argv[1], exc);
        if (isnan(type)) 
        {
            ret = NIL;
            goto error_out;
        }
        type = MIN(MAX(type, G_CHECKSUM_MD5), G_CHECKSUM_SHA256);
    }
    checksum = g_compute_checksum_for_data(type, original, -1);

    ret = js_char_to_value(ctx, checksum);

error_out:
    g_free(original);
    g_free(checksum);
    return ret;
}/*}}}*/

/** 
 * Change the mode, changeable modes are Modes.NormalMode, Modes.InsertMode and
 * Modes.CaretMode
 *
 * @name changeMode
 * @memberOf util
 * @function
 *
 * @param {Modes} mode The new mode
 * */

static JSValueRef 
sutil_change_mode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return UNDEFINED;
    double mode = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(mode))
    {
        if ((int) mode == NORMAL_MODE)
        {
            dwb_change_mode(NORMAL_MODE, true);
        }
        else if ((int)mode & (INSERT_MODE | CARET_MODE))
        {
            dwb_change_mode((int)mode);
        }
    }
    return UNDEFINED;
}


static JSValueRef 
sutil_base64_encode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSStringRef string;
    gsize length;
    const JSChar *chars;
    guchar *data; 
    char *base64; 
    JSValueRef ret;

    if (argc == 0)
        return NIL;
    string = JSValueToStringCopy(ctx, argv[0], exc);
    if (string == NULL)
        return NIL;

    length = JSStringGetLength(string);
    chars = JSStringGetCharactersPtr(string);
    data = g_malloc0(length * sizeof(guchar));

    for (guint i=0; i<length; i++)
    {
        data[i] = chars[i] & 0xff;
    }
    base64 = g_base64_encode(data, length);
    ret = js_char_to_value(ctx, base64);

    g_free(base64);
    g_free(data);
    JSStringRelease(string);

    return ret;
}

static JSValueRef 
sutil_base64_decode(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gsize length;
    gchar *base64; 
    guchar *data;
    gushort *js_data;
    JSStringRef string;
    JSValueRef ret = NIL;
    if (argc == 0)
        return NIL;
    base64 = js_value_to_char(ctx, argv[0], -1, exc);
    if (base64 == NULL)
        return NIL;
    data = g_base64_decode(base64, &length);
    js_data = g_malloc0(length * sizeof(gushort));
    for (guint i=0; i<length; i++)
    {
        js_data[i] = data[i];
    }
    string = JSStringCreateWithCharacters(js_data, length);
    ret = JSValueMakeString(ctx, string);

    g_free(base64);
    g_free(data);
    g_free(js_data);
    JSStringRelease(string);

    return ret;
}

/**
 * Gets the current mode 
 *
 * @name getMode 
 * @memberOf util 
 * @function 
 *
 * @returns {Enums and Flags.Modes} The current mode
 * */
static JSValueRef 
sutil_get_mode(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    return JSValueMakeNumber(ctx, BASIC_MODES(dwb.state.mode));
}

/** 
 * Gets the body of a function, useful for injecting scripts
 *
 * @name getBody
 * @memberOf util
 * @function 
 * 
 * @param {Function} function A function
 *
 * @returns {String} The body of the function
 * */
static JSValueRef 
sutil_get_body(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return NIL;
    JSValueRef ret = NIL;
    JSObjectRef func = js_value_to_function(ctx, argv[0], exc);
    char *body = get_body(ctx, func, exc);
    if (body != NULL)
    {
        ret = js_char_to_value(ctx, body);
        g_free(body);
    }
    return ret;
}
/** 
 * Dispatches a keyboard or button event
 *
 * @name dispatchEvent
 * @memberOf util
 * @function 
 * 
 * @param {Object} event 
 *      Event details, see {@link signals~onButtonPress|buttonPress}, 
 *      {@link signals~onButtonRelease|buttonRelease}, 
 *      {@link signals~onKeyPress|keyPress} or 
 *      {@link signals~onKeyRelease|keyRelease} 
 *      for details.
 * @param {Number} event.type
 *      Type of the event, can be either buttonpress (4), doubleclick (5),
 *      tripleclick (6), buttonrelease (7), keypress (8) or keyrelease
 *      (9).
 *
 * @returns {Boolean} Whether the event was dispatched
 * */
static JSValueRef 
sutil_dispatch_event(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    gboolean result = false;
    if (argc < 1)
        return JSValueMakeBoolean(ctx, false);
    double type = js_val_get_double_property(ctx, argv[0], "type", exc);
    if (isnan(type) || (!(IS_KEY_EVENT(type)) && !(IS_BUTTON_EVENT(type))))
        return JSValueMakeBoolean(ctx, false);

    double state = js_val_get_double_property(ctx, argv[0], "state", exc);
    if (isnan(state))
        state = 0;
    GdkEvent *event = gdk_event_new(type);
    if (IS_KEY_EVENT(type)) 
    {
        double keyval = js_val_get_double_property(ctx, argv[0], "keyval", exc);
        if (isnan(keyval))
            goto error_out;
        event->key.window = g_object_ref(gtk_widget_get_window(dwb.gui.window));
        event->key.keyval = keyval;
        event->key.state = state;
        GdkKeymapKey *key; 
        gint n;
        if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_default(), keyval, &key, &n))
        {
            event->key.hardware_keycode = key[0].keycode;
            g_free(key);
        }
        gtk_main_do_event(event);
        result = true;
    }
    else if (IS_BUTTON_EVENT(type))
    {
        double button = js_val_get_double_property(ctx, argv[0], "button", exc);
        if (isnan(button))
            goto error_out;

        event->button.button = button;
        event->button.window = g_object_ref(gtk_widget_get_window(VIEW(dwb.state.fview)->web));
        event->button.state = state;

        double x = js_val_get_double_property(ctx, argv[0], "x", exc);
        event->button.x = isnan(x) ? 0 : x;

        double y = js_val_get_double_property(ctx, argv[0], "y", exc);
        event->button.y = isnan(y) ? 0 : y;
        
        double x_root = js_val_get_double_property(ctx, argv[0], "xRoot", exc);
        double y_root = js_val_get_double_property(ctx, argv[0], "yRoot", exc);
        if (isnan(x_root)  || isnan(y_root))
        {
            GdkDisplay *dpy = gdk_display_open(NULL);
            int cx, cy;
            gdk_display_get_pointer(dpy, NULL, &cx, &cy, NULL);
            if (isnan(x_root))
                x_root = cx;
            if (isnan(y_root))
                y_root = cy;
            gdk_display_close(dpy);
        }
        event->button.x_root = x_root;
        event->button.y_root = y_root;

        double time = js_val_get_double_property(ctx, argv[0], "time", exc);
        event->button.time = isnan(time) ? 0 : time;
        gtk_main_do_event(event);
        result = true;
    }
error_out:
    gdk_event_free(event);
    return JSValueMakeBoolean(ctx, result);
}
static GdkAtom 
atom_from_jsvalue(JSContextRef ctx, JSValueRef val, JSValueRef *exc)
{
    double type = JSValueToNumber(ctx, val, exc);
    if (isnan(type))
        return NULL;
    if ((int)type == SELECTION_PRIMARY)
        return GDK_SELECTION_PRIMARY;
    else if ((int)type == SELECTION_CLIPBOARD)
        return GDK_NONE;
    else
        return NULL;
}
/**
 * Sets content of the system clipboard
 * @name set
 * @memberOf clipboard
 * @function 
 *
 * @param {Selection} selection The {@link Enums and Flags.Selection|Selection} to set
 * @param {String} text The text to set
 *
 * */
static JSValueRef 
clipboard_set(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2)
        return UNDEFINED;
    GdkAtom atom = atom_from_jsvalue(ctx, argv[0], exc);
    if (atom == NULL)
        atom = GDK_NONE;
    char *text = js_value_to_char(ctx, argv[1], -1, exc);
    if (text != NULL)
    {
        GtkClipboard *cb = gtk_clipboard_get(atom);
        gtk_clipboard_set_text(cb, text, -1);
        g_free(text);
    }
    return UNDEFINED;
}
/** 
 * Callback called when the clipboard was received
 * @callback clipboard~onGotClipboard
 *
 * @param {String} text 
 *      The text content of the clipboard
 * */
static void
got_clipboard(GtkClipboard *cb, const char *text, JSObjectRef callback)
{
    if (!TRY_CONTEXT_LOCK)
        return;

    if (s_global_context != NULL)
    {
        JSValueRef args[] = { text == NULL ? NIL : js_char_to_value(s_global_context, text) };
        call_as_function_debug(s_global_context, callback, callback, 1, args);
        JSValueUnprotect(s_global_context, callback);
    }
    CONTEXT_UNLOCK;
}
/**
 * Gets content of the system clipboard
 * @name get
 * @memberOf clipboard
 * @function 
 *
 * @param {Selection} selection 
 *      The {@Link Enums and Flags.Selection|Selection} to get
 * @param {clipboard~onGotClipboard} [callback] 
 *      A callback function that is called when the clipboard content was
 *      retrieved, if a callback function is used the clipboard will be fetched
 *      asynchronously, otherwise it will be fetched synchronously
 *
 * @returns {String|undefined}
 *      The content of the clipboard or undefined if a callback function is used
 * */
static JSValueRef 
clipboard_get(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 1)
        return NIL;
    GdkAtom atom = atom_from_jsvalue(ctx, argv[0], exc);
    if (atom == NULL)
        atom = GDK_NONE;
    GtkClipboard *clipboard = gtk_clipboard_get(atom);
    if (argc > 1) 
    {
        JSObjectRef callback = js_value_to_function(ctx, argv[1], exc);
        if (callback != NULL)
        {
            JSValueProtect(ctx, callback);
            gtk_clipboard_request_text(clipboard, (GtkClipboardTextReceivedFunc)got_clipboard, callback);
        }
    }
    else 
    {
        JSValueRef ret = NIL;
        char *text = gtk_clipboard_wait_for_text(clipboard);
        if (text != NULL)
        {
            ret = js_char_to_value(ctx, text);
            g_free(text);
        }
        return ret;
    }
    return NIL;
}

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
    double n;
    if (argc > 0 && !isnan(n = JSValueToNumber(ctx, argv[0], NULL)))
    {
        WebKitWebBackForwardList *list = JSObjectGetPrivate(this);
        g_return_val_if_fail(list != NULL, NIL);

        return make_object(ctx, G_OBJECT(webkit_web_back_forward_list_get_nth_item(list, n)));
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
}/*}}}*/

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
}/*}}}*/

/* data_get_cache_dir {{{*/
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
}/*}}}*/

/* data_get_config_dir {{{*/
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
}/*}}}*/

/* data_get_system_data_dir {{{*/
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
}/*}}}*/

/* data_get_user_data_dir {{{*/
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
}/*}}}*/
/*}}}*/

/* SYSTEM {{{*/
/* system_get_env {{{*/
/** 
 * Get the current process id 
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
}/*}}}*/

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

/* spawn_output {{{*/
static gboolean
spawn_output(GIOChannel *channel, GIOCondition condition, SpawnData *data) 
{
    char *content = NULL; 
    gboolean result = true;
    gsize length;
    int fd, status;
    gboolean is_stdout = data->is_stdout;

    if (condition == G_IO_HUP || condition == G_IO_ERR || condition == G_IO_NVAL) 
    {
        if (data->finished & SPAWN_FINISHED)
        {
            if (TRY_CONTEXT_LOCK)
            {
                if (s_global_context != NULL)
                {
                    JSValueRef argv[] = { JSValueMakeNumber(s_global_context, data->status) };
                    if (data->status != 0)
                        deferred_reject(s_global_context, data->deferred, data->deferred, 1, argv, NULL);
                    else if (condition == G_IO_HUP && is_stdout)
                        deferred_resolve(s_global_context, data->deferred, data->deferred, 1, argv, NULL);
                    JSValueUnprotect(s_global_context, data->callback);
                }
                CONTEXT_UNLOCK;
            }
            g_free(data);
        }
        else 
        {
            data->finished |= SPAWN_CLOSED;
            fd = g_io_channel_unix_get_fd(channel);
            g_io_channel_shutdown(channel, true, NULL);
            g_io_channel_unref(channel);
            data->channel = NULL;
            close(fd);
        }
        result = false;
    }
    else 
    {
        status = g_io_channel_read_line(channel, &content, &length, NULL, NULL);
        if (status == G_IO_STATUS_AGAIN || status == G_IO_STATUS_EOF)
        {
            result = true;
        }
        else if (status == G_IO_STATUS_NORMAL)
        {
            if (content != NULL && TRY_CONTEXT_LOCK)
            {
                if (s_global_context != NULL)
                {
                    JSValueRef arg = js_char_to_value(s_global_context, content);
                    if (arg != NULL)
                    {
                        JSValueRef argv[] = { arg };
                        call_as_function_debug(s_global_context, data->callback, data->callback, 1, argv);
                    }
                }
                CONTEXT_UNLOCK;
            }
        }
        g_free(content);
    }
    return result;
}/*}}}*/

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

/* {{{*/

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

void 
spawn_finish_data(SpawnData *data, int status)
{
    if (data != NULL)
    {
        data->finished |= SPAWN_FINISHED;
        data->status = status;
        if (data->channel != NULL)
            g_io_channel_flush(data->channel, NULL);
        if (data->finished & SPAWN_CLOSED)
            g_free(data);
    }
}
void 
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

    spawn_finish_data(data[0], fail);
    spawn_finish_data(data[1], fail);

    g_free(data);
}

/* system_spawn {{{*/
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
 *      onFinished : function(result) {
 *          io.print("Process terminated with status " + result.status);
 *          io.print("Stdout is :" + result.stdout);
 *          io.print("Stderr is :" + result.stderr);
 *      }
 * });
 * // Equivalently using the deferred
 * system.spawn("foo").always(function(result) {
 *      io.print("Process terminated with status " + result.status);
 *      io.print("Stdout is :" + result.stdout);
 *      io.print("Stderr is :" + result.stderr);
 * });
 * // Only use stdout if the process terminates successfully
 * system.spawn("foo").done(function(result) {
 *     io.print(result.stdout);
 * });
 * // Only use stderr if the process terminates with an error
 * system.spawn("foo").fail(function(result) {
 *     io.print(result.stderr);
 * });
 * // Using environment variables
 * system.spawn('sh -c "echo $foo"', {
 *      environment : { foo : "bar" }
 * }).then(function(result) { 
 *      io.print(result.stdout); 
 * });
 *
 *
 * @param {String} command The command to execute
 * @param {Object} [options] 
 * @param {Function} [options.onStdout] 
 *     A callback function that is called when a line from
 *     stdout was read. The function takes one parameter, the line that has been read.
 *     To get the complete stdout use either <i>onFinished</i> or the
 *     Deferred returned from <i>system.spawn</i>.
 * @param {Function} [options.onStderr] 
 *     A callback function that is called when a line from
 *     stderr was read. The function takes one parameter, the line that has been read. 
 *     To get the complete stderr use either <i>onFinished</i> or the
 *     Deferred returned from <i>system.spawn</i>.
 * @param {Function} [options.onFinished] 
 *     A callback that will be called when the child process has terminated. The
 *     callback takes one argument, an object that contains stdout, stderr and
 *     status, i.e. the return code of the child process.
 * @param {String} [options.stdin] 
 *     String that will be piped to stdin of the child process. 
 * @param {Object} [options.environment] 
 *     Hash of environment variables that will be set in the childs environment
 *
 *
 * @returns {Deferred}
 *      A deferred, it will be resolved if the child exits normally, it will be
 *      rejected if the child process exits abnormally. The argument passed to
 *      resolve and reject is an Object containing stdout, stderr and status,
 *      i.e. the return code of the child process.
 * */
static JSValueRef 
system_spawn(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NIL; 
    int outfd, errfd, infd = -1;
    char **srgv = NULL, *cmdline = NULL;
    char **envp = NULL;
    int srgc;
    GIOChannel *out_channel = NULL, *err_channel = NULL;
    JSObjectRef oc = NULL, ec = NULL;
    SpawnData *out_data = NULL, *err_data = NULL;
    GPid pid;
    char *pipe_stdin = NULL;
    gint spawn_options = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;


    if (argc == 0) 
        return NIL;

    if (!TRY_CONTEXT_LOCK)
        return NIL;

    if (s_global_context == NULL)
        goto error_out;

    cmdline = js_value_to_char(ctx, argv[0], -1, exc);
    if (cmdline == NULL) 
        goto error_out;

    if (argc > 1) 
        oc = js_value_to_function(ctx, argv[1], NULL);
    
    if (argc > 2) 
        ec = js_value_to_function(ctx, argv[2], NULL);

    if (argc > 3)
        pipe_stdin = js_value_to_char(ctx, argv[3], -1, exc);

    if (argc > 4)
        envp = get_environment(ctx, argv[4], exc);

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

    JSObjectRef deferred = deferred_new(s_global_context);



    if (oc != NULL) 
    {
        out_channel = g_io_channel_unix_new(outfd);

        out_data = g_malloc(sizeof(SpawnData));
        SPAWN_DATA_INIT(out_data, out_channel, oc, deferred, true);

        JSValueProtect(ctx, oc);
        g_io_channel_set_flags(out_channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch(out_channel, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, (GIOFunc)spawn_output, out_data);
        g_io_channel_set_close_on_unref(out_channel, true);
    }
    if (ec != NULL) 
    {
        err_channel = g_io_channel_unix_new(errfd);

        err_data = g_malloc(sizeof(SpawnData));
        SPAWN_DATA_INIT(err_data, err_channel, ec, deferred, false);

        JSValueProtect(ctx, ec);
        g_io_channel_set_flags(err_channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch(err_channel, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, (GIOFunc)spawn_output, err_data);
        g_io_channel_set_close_on_unref(err_channel, true);
    }
    if (pipe_stdin != NULL && infd != -1)
    {
        if (write(infd, pipe_stdin, strlen(pipe_stdin)) == -1 || write(infd, "\n", 1) == -1)
            perror("system.spawn");
    }
    if (infd != -1)
        close(infd);

    SpawnData **data = g_malloc_n(2, sizeof(SpawnData*));
    data[0] = out_data;
    data[1] = err_data;
    g_child_watch_add(pid, (GChildWatchFunc)watch_spawn, data);
    ret = deferred;

error_out:
    CONTEXT_UNLOCK;
    g_free(pipe_stdin);
    g_strfreev(envp);
    g_free(cmdline);
    g_strfreev(srgv);
    return ret;
}/*}}}*/

/* system_file_test {{{*/
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
 * */
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
/* system_mkdir {{{*/
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

}/*}}}*/

/*}}}*/

/* IO {{{*/
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
        return UNDEFINED;

    char *message = js_value_to_char(ctx, argv[0], -1, exc);
    if (message != NULL) 
    {
        message = util_strescape_char(message, '%', '%');
        dwb_set_normal_message(dwb.state.fview, true, message);
        g_free(message);
    }
    return UNDEFINED;
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
        return UNDEFINED;

    char *message = js_value_to_char(ctx, argv[0], -1, exc);
    if (message != NULL) 
    {
        message = util_strescape_char(message, '%', '%');
        dwb_set_error_message(dwb.state.fview, message);
        g_free(message);
    }
    return UNDEFINED;
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
 * Print messages to stdout or stderr
 *
 * @name print
 * @memberOf io
 * @function
 *
 * @param {String}  text  The text to print
 * @param {String} [stream] The stream, either <i>"stdout"</i> or <i>"stderr"</i>, default <i>"stdout"</i>
 * */
static JSValueRef 
io_print(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0) 
        return UNDEFINED;

    FILE *stream = stdout;
    if (argc >= 2) 
    {
        if (js_string_equals(ctx, argv[1], "stderr"))
            stream = stderr;
    }

    char *out = NULL;
    double dout;
    char *json = NULL;
    int type = JSValueGetType(ctx, argv[0]);
    switch (type) 
    {
        case kJSTypeString : 
            out = js_value_to_char(ctx, argv[0], -1, exc);
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
                        return UNDEFINED;
                }
                fprintf(stream, "%s\n", out);
                g_free(out);
            }
            break;
        case kJSTypeBoolean : 
            fprintf(stream, "%s\n", JSValueToBoolean(ctx, argv[0]) ? "true" : "false");
            break;
        case kJSTypeNumber : 
            dout = JSValueToNumber(ctx, argv[0], exc);
            if (!isnan(dout)) 
                if ((int)dout == dout) 
                    fprintf(stream, "%d\n", (int)dout);
                else 
                    fprintf(stream, "%f\n", dout);
            else 
                fprintf(stream, "NAN\n");
            break;
        case kJSTypeUndefined : 
            fprintf(stream, "undefined\n");
            break;
        case kJSTypeNull : 
            fprintf(stream, "null\n");
            break;
        case kJSTypeObject : 
            json = js_value_to_json(ctx, argv[0], -1, NULL);
            if (json != NULL) 
            {
                fprintf(stream, "%s\n", json);
                g_free(json);
            }
            break;
        default : break;
    }
    return UNDEFINED;
}/*}}}*/
/*}}}*/

static JSObjectRef 
hwv_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    GObject *wv = G_OBJECT(webkit_web_view_new());
    return make_object_for_class(ctx, s_webview_class, wv, false);
}

/** 
 * Moves a widget in a GtkBox to a new Position
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
    double position = JSValueToNumber(ctx, argv[1], exc);
    if (isnan(position))
        return UNDEFINED;
    gtk_box_reorder_child(GTK_BOX(widget), child, (int)position);
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

static JSValueRef 
widget_container_add(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, UNDEFINED);

    if (! GTK_IS_CONTAINER(widget))
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

    gtk_container_add(GTK_CONTAINER(widget), child);
    return UNDEFINED;
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
        return JSObjectMake(ctx, s_widget_class, widget);
    }
    return JSValueToObject(ctx, NIL, NULL);
}
/**
 * Called when a menu item was activated that was added to the popup menu,
 * <i>this</i> will refer to the GtkMenuItem.
 *
 * @callback GtkMenu~onMenuActivate
 * */
static void 
menu_callback(GtkMenuItem *item, JSObjectRef callback)
{
    if (!TRY_CONTEXT_LOCK)
        return;
    if (s_global_context != NULL)
    {
        JSObjectRef this =  make_object_for_class(s_global_context, s_widget_class, G_OBJECT(item), true);
        call_as_function_debug(s_global_context, callback, this, 0, NULL);
    }
    CONTEXT_UNLOCK;
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
 *      Callback called when the item is clicked, if omitted it will be a
 *      separator
 * @param {String} [items[].label] 
 *      Label of the item, if omitted it will be a separator
 * @param {Number} [items[].position] 
 *      Position of the item or separator starting at 0, if omitted it will be appended
 *
 * @example 
 * signals.connect("contextMenu", function(wv, menu) {
 *      menu.addItems([
 *          // append separator
 *          null, 
 *          // append a menu item
 *          { 
 *              label : "Copy current url", 
 *              callback : function() 
 *              {
 *                  clipboard.set(Selection.clipboard, wv.url);
 *                  io.notify(this.label + " was activated");
 *              }
 *          }
 *      ]);
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
            if (JSValueIsNull(ctx, current))
            {
                item = gtk_separator_menu_item_new();
                goto create;
            }

            o = JSValueToObject(ctx, current, exc);
            if (o == NULL)
                continue;
            if (JSObjectHasProperty(ctx, o, str_position))
            {
                current = JSObjectGetProperty(ctx, o, str_position, exc);
                p = JSValueToNumber(ctx, current, exc);
                if (isnan(p))
                    position = -1;
                else 
                    position = (int) p;
            }
            if (JSObjectHasProperty(ctx, o, str_label) && JSObjectHasProperty(ctx, o, str_callback))
            {
                label_value = JSObjectGetProperty(ctx, o, str_label, exc);
                label = js_value_to_char(ctx, label_value, -1, exc);
                if (label == NULL)
                    goto error;

                callback_value = JSObjectGetProperty(ctx, o, str_callback, exc);
                callback = js_value_to_function(ctx, callback_value, exc);
                if (callback == NULL)
                    goto error;

                item = gtk_menu_item_new_with_label(label);
                g_signal_connect(item, "activate", G_CALLBACK(menu_callback), callback);
            }
            else 
            {
                item = gtk_separator_menu_item_new();
            }
create: 
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            if (position != -1)
                gtk_menu_reorder_child(menu, item, position);
            gtk_widget_show(item);
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
    return JSObjectMake(ctx, s_download_class, download);
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
/*}}}*/
// TODO: Documentation
void 
server_handler_cb(SoupServer *server, SoupMessage *message, const char *path, GHashTable *query, SoupClientContext *ctx, JSObjectRef callback)
{
    JSValueRef argv[3];
    GHashTableIter iter;
    gpointer key = NULL, value = NULL;

    if (!TRY_CONTEXT_LOCK)
        return;
    if (s_global_context != NULL)
    {

        argv[0] = make_object_for_class(s_global_context, s_message_class, G_OBJECT(message), true);
        argv[1] = js_char_to_value(s_global_context, path);

        if (query != NULL)
        {
            JSObjectRef js_query = JSObjectMake(s_global_context, NULL, NULL);
            g_hash_table_iter_init(&iter, query);
            while (g_hash_table_iter_next(&iter, &key, &value) && key != NULL)
                js_set_object_property(s_global_context, js_query, key, value, NULL);
            argv[2] = js_query;
        }
        else 
            argv[2] = NIL;

        call_as_function_debug(s_global_context, callback, 
                make_object_for_class(s_global_context, s_soup_server_class, G_OBJECT(server), true), 
                3, argv);
    }
    CONTEXT_UNLOCK;
}
static JSValueRef 
server_handler(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc < 2)
        return UNDEFINED;
    SoupServer *server = JSObjectGetPrivate(this);
    if (server == NULL)
        return UNDEFINED;
    char *path = js_value_to_char(ctx, argv[0], 2048, exc);

    JSObjectRef callback = js_value_to_function(ctx, argv[1], exc);
    if (callback != NULL)
    {
        JSValueProtect(ctx, callback);
        soup_server_add_handler(server, path, (SoupServerCallback)server_handler_cb, callback, (GDestroyNotify)object_destroy_cb);
    }

    g_free(path);
    return UNDEFINED;
}
// TODO: Documentation
static JSValueRef 
server_run(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupServer *server = JSObjectGetPrivate(this);
    if (server != NULL)
        soup_server_run_async(server);
    return UNDEFINED;
}
// TODO: Documentation
static JSValueRef 
server_stop(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupServer *server = JSObjectGetPrivate(this);
    if (server != NULL)
        soup_server_quit(server);
    return UNDEFINED;
}
// TODO: Documentation
static JSValueRef 
server_shutdown(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    SoupServer *server = JSObjectGetPrivate(this);
    if (server != NULL)
    {
        GSList *l = g_slist_find(s_servers, server);
        s_servers = g_slist_delete_link(s_servers, l); 
        JSObjectSetPrivate(this, NULL);
        SERVER_DO_SHUTDOWN(server);
    }
    return UNDEFINED;
}
// TODO: Documentation
static JSObjectRef 
server_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    if (argc > 0 && JSValueIsNumber(ctx, argv[0]))
    {
        double port = JSValueToNumber(ctx, argv[0], exception);
        struct sockaddr_in address;
        int sock_fd, success;

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons((int) port);

        sock_fd =  socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1)
        {
            js_make_exception(ctx, exception, "Server: cannot create socket!", (int)port);
            return JSObjectMake(ctx, NULL, NULL);
        }
        success = bind(sock_fd, (struct sockaddr *)&address, sizeof(address));
        close(sock_fd);
        if (success == 0)
        {
            SoupServer *server = soup_server_new("port", (int)port, NULL);
            s_servers = g_slist_prepend(s_servers, server);
            return make_object_for_class(ctx, s_soup_server_class, G_OBJECT(server), true);
        }
        else 
        {
            js_make_exception(ctx, exception, "Server: Port %d is already bound!", (int)port);
            return JSObjectMake(ctx, NULL, NULL);
        }

    }
    return JSValueToObject(ctx, NIL, exception);
}

JSObjectRef 
scripts_make_cookie(SoupCookie *cookie)
{
    g_return_val_if_fail(cookie != NULL, NULL);
    return make_boxed(cookie, s_cookie_class);
}
static JSObjectRef 
cookie_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    SoupCookie *cookie = soup_cookie_new("", "", "", "", 0);
    return JSObjectMake(ctx, s_cookie_class, cookie);
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
/** 
 * The main window
 * @name window
 * @memberOf gui
 * @type GtkWindow
 * */
static JSValueRef
gui_get_window(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.window), true);
}
/** 
 * The main container. Child of window
 * @name mainBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_main_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.vbox), true);
}
/** 
 * The box used for tab labels. Child of mainBox
 * @name tabBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_tab_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
#if _HAS_GTK3
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.tabbox), true);
#else
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.tabcontainer), true);
#endif
}
/** 
 * The box used for the main content. Child of mainBox
 * @name contentBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_content_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.mainbox), true);
}
/** 
 * The outmost statusbar widget, used for setting the statusbars colors, child of mainBox.
 * @name statusWidget
 * @memberOf gui
 * @type GtkEventBox
 * */
static JSValueRef
gui_get_status_widget(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.statusbox), true);
}
/** 
 * Used for the statusbar alignment, child of statusWidget.
 * @name statusAlignment
 * @memberOf gui
 * @type GtkAlignment
 * */
static JSValueRef
gui_get_status_alignment(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.alignment), true);
}
/** 
 * The box that contains the statusbar widgets, grandchild of statusAlignment
 * @name statusBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_status_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.status_hbox), true);
}
/** 
 * Label used for notifications, first child of statusBox
 * @name messageLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_message_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.lstatus), true);
}
/** 
 * The entry, second child of statusBox
 * @name entry
 * @memberOf gui
 * @type GtkEntry
 * */
static JSValueRef
gui_get_entry(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.entry), true);
}
/** 
 * The uri label, third child of statusBox
 * @name uriLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_uri_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.urilabel), true);
}
/** 
 * Label used for status information, fourth child of statusBox
 * @name statusLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_status_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) 
{
    return make_object_for_class(ctx, s_secure_widget_class, G_OBJECT(dwb.gui.rstatus), true);
}
/*}}}*/

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
            JSObjectRef scripts = js_get_object_property(s_global_context, JSContextGetGlobalObject(s_global_context), "Signal");
            if (scripts)
            {
                JSObjectRef connect = js_get_object_property(s_global_context, scripts, "connect");
                if (connect)
                {
                    JSValueRef js_value = js_char_to_value(ctx, tmp);
                    JSValueRef argv[] = { js_value, func };
                    JSObjectCallAsFunction(s_global_context, connect, func, 2, argv, exception);
                }
            }

            g_free(name);
            return true;
        }
    }

    for (int i = SCRIPTS_SIG_FIRST; i<SCRIPTS_SIG_LAST; i++) 
    {
        if (strcmp(name, s_sigmap[i].name)) 
            continue;

        if (JSValueIsNull(ctx, value)) 
        {
            s_sig_objects[i] = NULL;
            dwb.misc.script_signals &= ~(1<<i);
        }
        else if ( (o = js_value_to_function(ctx, value, exception)) != NULL) 
        {
            s_sig_objects[i] = o;
            dwb.misc.script_signals |= (1<<i);
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
    int numargs, i, additional = 0;
    gboolean ret = false;
    JSObjectRef function = s_sig_objects[sig->signal];
    if (function == NULL)
        return false;

    if (!TRY_CONTEXT_LOCK)
        return false;

    if (s_global_context != NULL) 
    {
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
                val[i++] = make_object(s_global_context, G_OBJECT(sig->objects[j]));
            else 
                val[i++] = NIL;
        }

        if (sig->json != NULL)
        {
            JSValueRef vson = js_json_to_value(s_global_context, sig->json);
            val[i++] = vson == NULL ? NIL : vson;
        }
        if (sig->arg != NULL)
        {
            switch (sig->arg->n)
            {
                case BOOLEAN : val[i++] = JSValueMakeBoolean(s_global_context, sig->arg->b); break;
                case INTEGER : val[i++] = JSValueMakeNumber(s_global_context, sig->arg->i); break;
                case DOUBLE  : val[i++] = JSValueMakeNumber(s_global_context, sig->arg->d); break;
                case CHAR    : val[i++] = js_char_to_value(s_global_context, sig->arg->p); break;
            }
        }

        JSValueRef js_ret = call_as_function_debug(s_global_context, function, function, numargs, val);

        if (JSValueIsBoolean(s_global_context, js_ret)) 
            ret = JSValueToBoolean(s_global_context, js_ret);
    }
    CONTEXT_UNLOCK;

    return ret;
}/*}}}*/
/*}}}*/

/* OBJECTS {{{*/
/* make_object {{{*/
static void 
object_destroy_cb(JSObjectRef o) 
{
    if (!TRY_CONTEXT_LOCK)
        return;
    if (s_global_context != NULL)
    {
        JSObjectSetPrivate(o, NULL);
        JSValueUnprotect(s_global_context, o);
    }
    CONTEXT_UNLOCK;
}

static JSObjectRef 
make_object_for_class(JSContextRef ctx, JSClassRef class, GObject *o, gboolean protect) 
{
    JSObjectRef retobj = g_object_get_qdata(o, s_ref_quark);
    if (retobj != NULL)
        return retobj;

    retobj = JSObjectMake(ctx, class, o);
    if (protect) 
    {
        g_object_set_qdata_full(o, s_ref_quark, retobj, (GDestroyNotify)object_destroy_cb);
        JSValueProtect(ctx, retobj);
    }
    else 
        g_object_set_qdata_full(o, s_ref_quark, retobj, NULL);

    return retobj;
}

static JSObjectRef 
make_boxed(gpointer boxed, JSClassRef klass)
{
    JSObjectRef ret = NULL;
    if (!TRY_CONTEXT_LOCK)
        return NULL;
    if (s_global_context != NULL)
    {
        ret = JSObjectMake(s_global_context, klass, boxed);
    }
    CONTEXT_UNLOCK;
    return ret;
}

static JSObjectRef 
make_object(JSContextRef ctx, GObject *o) 
{
    if (o == NULL) 
    {
        JSValueRef v = NIL;
        return JSValueToObject(ctx, v, NULL);
    }
    JSClassRef class;
    if (WEBKIT_IS_WEB_VIEW(o)) 
        class = s_webview_class;
    else if (WEBKIT_IS_WEB_FRAME(o))
        class = s_frame_class;
    else if (WEBKIT_IS_DOWNLOAD(o)) 
        class = s_download_class;
    else if (SOUP_IS_MESSAGE(o)) 
        class = s_message_class;
    else if (WEBKIT_IS_WEB_BACK_FORWARD_LIST(o))
        class = s_history_class;
    else if (GTK_IS_MENU(o))
        class = s_menu_class;
    else if (GTK_IS_WIDGET(o))
        class = s_secure_widget_class;
    else 
        class = s_gobject_class;
    return make_object_for_class(ctx, class, o, true);
}/*}}}*/

/** 
 * Callback called for GObject signals, <b>this</b> will refer to the object
 * that connected to the signal
 * @callback GObject~connectCallback
 *
 * @param {...Object} varargs
 *      Variable number of additional arguments, see the correspondent
 *      gtk/glib/webkit documentation. Note that the first argument is omitted and
 *      <i>this</i> will correspond to the first parameter and that only
 *      arguments of basic type and arguments derived from GObject are converted
 *      to the corresponding javascript object, otherwise the argument will be
 *      undefined (e.g.  GBoxed types and structs).
 *
 * @returns {Boolean} 
 *      Return true to stop the emission. Note that this signal handler is
 *      connected after dwb's default handler so it will not prevent dwb's
 *      handlers to be executed
 * */
static gboolean 
connect_callback(SSignal *sig, ...) 
{
    va_list args;
    JSValueRef cur;
    JSValueRef argv[sig->query->n_params];
    gboolean result = false;

    if (!TRY_CONTEXT_LOCK)
        return result;
    if (s_global_context == NULL)
        goto error_out;

    va_start(args, sig);
#define CHECK_NUMBER(GTYPE, TYPE) G_STMT_START if (gtype == G_TYPE_##GTYPE) { \
    TYPE MM_value = va_arg(args, TYPE); \
    cur = JSValueMakeNumber(s_global_context, MM_value); goto apply;} G_STMT_END
    for (guint i=0; i<sig->query->n_params; i++) 
    {
        GType gtype = sig->query->param_types[i], act;
        while ((act = g_type_parent(gtype))) 
            gtype = act;
        CHECK_NUMBER(INT, gint);
        CHECK_NUMBER(UINT, guint);
        CHECK_NUMBER(LONG, glong);
        CHECK_NUMBER(ULONG, gulong);
        CHECK_NUMBER(FLOAT, gdouble);
        CHECK_NUMBER(DOUBLE, gdouble);
        CHECK_NUMBER(ENUM, gint);
        CHECK_NUMBER(INT64, gint64);
        CHECK_NUMBER(UINT64, guint64);
        CHECK_NUMBER(FLAGS, guint);
        if (sig->query->param_types[i] == G_TYPE_BOOLEAN) 
        {
            gboolean value = va_arg(args, gboolean);
            cur = JSValueMakeBoolean(s_global_context, value);
        }
        else if (sig->query->param_types[i] == G_TYPE_STRING) 
        {
            char *value = va_arg(args, char *);
            cur = js_char_to_value(s_global_context, value);
        }
        else if (G_TYPE_IS_CLASSED(gtype)) 
        {
            GObject *value = va_arg(args, gpointer);
            if (value != NULL) // avoid conversion to JSObjectRef
                cur = make_object(s_global_context, value);
            else 
                cur = NIL;
        }
        else 
        {
            va_arg(args, void*);
            cur = UNDEFINED;
        }

apply:
        argv[i] = cur;
    }
#undef CHECK_NUMBER
    JSValueRef ret = call_as_function_debug(s_global_context, sig->func, sig->object, sig->query->n_params, argv);
    if (JSValueIsBoolean(s_global_context, ret)) 
    {
        result = JSValueToBoolean(s_global_context, ret);
    }
error_out:
    CONTEXT_UNLOCK;
    return result;
}
static void
on_disconnect_object(SSignal *sig, GClosure *closure)
{
    ssignal_free(sig);
}
/** 
 * Called when a property of an object changes, <b>this</b> will refer to the object
 * that connected to the signal.
 * @callback GObject~notifyCallback
 *
 * */
static void
notify_callback(GObject *o, GParamSpec *param, SSignal *sig)
{
    if (!TRY_CONTEXT_LOCK) 
        return;
    if (s_global_context != NULL)
    {
        g_signal_handlers_block_by_func(o, G_CALLBACK(notify_callback), sig);
        call_as_function_debug(s_global_context, sig->func, make_object(s_global_context, o), 0, NULL);
        g_signal_handlers_unblock_by_func(o, G_CALLBACK(notify_callback), sig);
    }
    CONTEXT_UNLOCK;
}
/**
 * Connect to a GObject-signal. Note that all signals are connected using the
 * signal::- or with notify::-prefix. If connecting to a signal the
 * signal::-prefix must be omitted. The callback function will have the same
 * parameters as the GObject signal callback without the first parameter,
 * however some parameters may be undefined if they cannot be converted to
 * javascript objects. All signal handlers are executed after dwb’s default
 * handler.
 *
 * @memberOf GObject.prototype
 * @name connect 
 * @function
 *
 * @param {String} name The signal name.
 * @param {GObject~connectCallback} callback Callback that will be called when the signal is emitted.
 * @param {Boolean} [after] Whether to connect after the default signal handler.
 *
 * @returns {Number} The signal id of this signal
 * */
static JSValueRef 
gobject_connect(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    GConnectFlags flags = 0;
    gulong id = 0;
    SSignal *sig;
    char *name = NULL;
    guint signal_id;

    if (argc < 2) 
        return JSValueMakeNumber(ctx, 0);

    name = js_value_to_char(ctx, argv[0], PROP_LENGTH, exc);
    if (name == NULL) 
        goto error_out;

    JSObjectRef func = js_value_to_function(ctx, argv[1], exc);
    if (func == NULL) 
        goto error_out;

    GObject *o = JSObjectGetPrivate(this);
    if (o == NULL)
        goto error_out;

    if (argc > 2 && JSValueIsBoolean(ctx, argv[2]) && JSValueToBoolean(ctx, argv[2]))
        flags |= G_CONNECT_AFTER;

    if (strncmp(name, "notify::", 8) == 0) 
    {
        JSValueProtect(ctx, func);
        SSignal *sig = ssignal_new();
        sig->func = func;
        id = g_signal_connect_data(o, name, G_CALLBACK(notify_callback), sig, (GClosureNotify)on_disconnect_object, flags);
        if (id > 0)
        {
            sig->id = id;
            sigdata_append(sig->id, o); 
        }
        else 
            ssignal_free(sig);
    }
    else
    {
        signal_id = g_signal_lookup(name, G_TYPE_FROM_INSTANCE(o));

        flags |= G_CONNECT_SWAPPED;

        if (signal_id == 0)
            goto error_out;

        sig = ssignal_new_with_query(signal_id);
        if (sig == NULL) 
            goto error_out;

        if (sig->query == NULL || sig->query->signal_id == 0) 
        {
            ssignal_free(sig);
            goto error_out;
        }

        sig->func = func;
        JSValueProtect(ctx, func);

        id = g_signal_connect_data(o, name, G_CALLBACK(connect_callback), sig, (GClosureNotify)on_disconnect_object, flags);
        if (id > 0) 
        {
            sig->id = id;
            JSValueProtect(ctx, this);
            sig->object = this;
            sigdata_append(id, o);
        }
        else 
            ssignal_free(sig);
    }

error_out: 
    g_free(name);
    return JSValueMakeNumber(ctx, id);
}
/** 
 * Blocks emission of a signal
 *
 * @name blockSignal 
 * @memberOf GObject.prototype
 * @function
 *
 * @param {Number} id The signal id retrieved from GObject#connect
 * */

static JSValueRef 
gobject_block_signal(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    double sigid;
    if (argc > 0 && !isnan(sigid = JSValueToNumber(ctx, argv[0], exc))) 
    {
        GObject *o = JSObjectGetPrivate(this);
        if (o != NULL)
            g_signal_handler_block(o, (int)sigid);
    }
    return UNDEFINED;
}
/** 
 * Unblocks a signal that was blocked with GObject#blockSignal
 *
 * @name unblockSignal 
 * @memberOf GObject.prototype
 * @function
 *
 * @param {Number} id The signal id retrieved from GObject#connect
 * */
static JSValueRef 
gobject_unblock_signal(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    double sigid;
    if (argc > 0 && !isnan(sigid = JSValueToNumber(ctx, argv[0], exc)))
    {
        GObject *o = JSObjectGetPrivate(this);
        if (o != NULL)
            g_signal_handler_unblock(o, (int)sigid);
    }
    return UNDEFINED;
}
/**
 * Disconnects from a signal
 *
 * @name disconnect
 * @memberOf GObject.prototype
 * @function
 *
 * @param {Number} id The signal id retrieved from {@link GObject.connect}
 *
 * @returns {Boolean}
 *      Whether a signal was found and disconnected
 * */
static JSValueRef 
gobject_disconnect(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    int id;
    if (argc > 0 && JSValueIsNumber(ctx, argv[0]) && !isnan(id = JSValueToNumber(ctx, argv[0], exc)))
    {
        GObject *o = JSObjectGetPrivate(this);
        if (o != NULL && g_signal_handler_is_connected(o, id)) 
        {
            sigdata_remove(id, o);
            g_signal_handler_disconnect(o, id);
            return JSValueMakeBoolean(ctx, true);
        }
    }
    return JSValueMakeBoolean(ctx, false);
}

/* set_property_cb {{{*/
static bool
set_property_cb(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception) 
{
    return true;
}/*}}}*/

/* create_class {{{*/
static JSClassRef 
create_class(const char *name, JSStaticFunction staticFunctions[], JSStaticValue staticValues[]) 
{
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = name;
    cd.staticFunctions = staticFunctions;
    cd.staticValues = staticValues;
    cd.setProperty = set_property_cb;
    return JSClassCreate(&cd);
}/*}}}*/

/* create_object {{{*/
static JSObjectRef 
create_object(JSContextRef ctx, JSClassRef class, JSObjectRef obj, JSClassAttributes attr, const char *name, void *private) 
{
    JSObjectRef ret = JSObjectMake(ctx, class, private);
    js_set_property(ctx, obj, name, ret, attr, NULL);
    return ret;
}/*}}}*/

static void 
finalize(JSObjectRef o)
{
    GObject *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        g_object_steal_qdata(ob, s_ref_quark);
    }
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

/* set_property {{{*/
static bool
set_property(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef jsvalue, JSValueRef* exception) 
{
    char buf[PROP_LENGTH];
    char *name = js_string_to_char(ctx, js_name, -1);
    if (name == NULL)
        return false;

    uncamelize(buf, name, '-', PROP_LENGTH);
    g_free(name);

    GObject *o = JSObjectGetPrivate(object);
    g_return_val_if_fail(o != NULL, false);

    GObjectClass *class = G_OBJECT_GET_CLASS(o);
    if (class == NULL || !G_IS_OBJECT_CLASS(class))
        return false;

    GParamSpec *pspec = g_object_class_find_property(class, buf);
    if (pspec == NULL)
        return false;

    if (! (pspec->flags & G_PARAM_WRITABLE))
        return false;

    int jstype = JSValueGetType(ctx, jsvalue);
    GType gtype = G_TYPE_IS_FUNDAMENTAL(pspec->value_type) ? pspec->value_type : g_type_parent(pspec->value_type);

    if (jstype == kJSTypeNumber && 
            (gtype == G_TYPE_INT || gtype == G_TYPE_UINT || gtype == G_TYPE_LONG || gtype == G_TYPE_ULONG ||
             gtype == G_TYPE_FLOAT || gtype == G_TYPE_DOUBLE || gtype == G_TYPE_ENUM || gtype == G_TYPE_INT64 ||
             gtype == G_TYPE_UINT64 || gtype == G_TYPE_FLAGS))  
    {
        double value = JSValueToNumber(ctx, jsvalue, exception);
        if (!isnan(value))
        {
            switch (gtype) 
            {
                case G_TYPE_ENUM :
                case G_TYPE_FLAGS :
                case G_TYPE_INT : g_object_set(o, buf, (gint)value, NULL); break;
                case G_TYPE_UINT : g_object_set(o, buf, (guint)value, NULL); break;
                case G_TYPE_LONG : g_object_set(o, buf, (long)value, NULL); break;
                case G_TYPE_ULONG : g_object_set(o, buf, (gulong)value, NULL); break;
                case G_TYPE_FLOAT : g_object_set(o, buf, (gfloat)value, NULL); break;
                case G_TYPE_DOUBLE : g_object_set(o, buf, (gdouble)value, NULL); break;
                case G_TYPE_INT64 : g_object_set(o, buf, (gint64)value, NULL); break;
                case G_TYPE_UINT64 : g_object_set(o, buf, (guint64)value, NULL); break;

            }
            return true;
        }
        return false;
    }
    else if (jstype == kJSTypeBoolean && gtype == G_TYPE_BOOLEAN) 
    {
        bool value = JSValueToBoolean(ctx, jsvalue);
        g_object_set(o, buf, value, NULL);
        return true;
    }
    else if (jstype == kJSTypeString && gtype == G_TYPE_STRING) 
    {
        char *value = js_value_to_char(ctx, jsvalue, -1, exception);
        g_object_set(o, buf, value, NULL);
        g_free(value);
        return true;
    }
    return false;
}/*}}}*/

/* get_property {{{*/
static JSValueRef
get_property(JSContextRef ctx, JSObjectRef jsobj, JSStringRef js_name, JSValueRef *exception) 
{
    char buf[PROP_LENGTH];
    JSValueRef ret = NULL;

    char *name = js_string_to_char(ctx, js_name, -1);
    if (name == NULL)
        return NULL;

    uncamelize(buf, name, '-', PROP_LENGTH);
    g_free(name);

    GObject *o = JSObjectGetPrivate(jsobj);
    g_return_val_if_fail(o != NULL, NULL);

    GObjectClass *class = G_OBJECT_GET_CLASS(o);
    if (class == NULL || !G_IS_OBJECT_CLASS(class))
        return NULL;

    GParamSpec *pspec = g_object_class_find_property(class, buf);
    if (pspec == NULL)
        return NULL;

    if (! (pspec->flags & G_PARAM_READABLE))
        return NULL;

    GType gtype = pspec->value_type, act; 
    while ((act = g_type_parent(gtype))) 
        gtype = act;

#define CHECK_NUMBER(GTYPE, TYPE) G_STMT_START if (gtype == G_TYPE_##GTYPE) { \
    TYPE value; g_object_get(o, buf, &value, NULL); return JSValueMakeNumber(ctx, (double)value); \
}    G_STMT_END
        CHECK_NUMBER(INT, gint);
        CHECK_NUMBER(UINT, guint);
        CHECK_NUMBER(LONG, glong);
        CHECK_NUMBER(ULONG, gulong);
        CHECK_NUMBER(FLOAT, gfloat);
        CHECK_NUMBER(DOUBLE, gdouble);
        CHECK_NUMBER(ENUM, gint);
        CHECK_NUMBER(INT64, gint64);
        CHECK_NUMBER(UINT64, guint64);
        CHECK_NUMBER(FLAGS, guint);
#undef CHECK_NUMBER
    if (pspec->value_type == G_TYPE_BOOLEAN) 
    {
        gboolean bval;
        g_object_get(o, buf, &bval, NULL);
        ret = JSValueMakeBoolean(ctx, bval);
    }
    else if (pspec->value_type == G_TYPE_STRING) 
    {
        char *value;
        g_object_get(o, buf, &value, NULL);
        ret = js_char_to_value(ctx, value);
        g_free(value);
    }
    else if (G_TYPE_IS_CLASSED(gtype)) 
    {
        GObject *object;
        g_object_get(o, buf, &object, NULL);
        if (object == NULL)
            return NULL;
    
        JSObjectRef retobj = make_object(ctx, object);
        g_object_unref(object);
        ret = retobj;
    }
    return ret;
}/*}}}*/

static JSObjectRef
create_constructor(JSContextRef ctx, char *name, JSClassRef class, JSObjectCallAsConstructorCallback cb, JSValueRef *exc)
{
    JSObjectRef constructor = JSObjectMakeConstructor(ctx, class, cb);
    JSStringRef js_name = JSStringCreateWithUTF8CString(name);
    JSObjectSetProperty(ctx, JSContextGetGlobalObject(ctx), js_name, constructor, kJSPropertyAttributeDontDelete, exc);
    JSStringRelease(js_name);
    JSValueProtect(ctx, constructor);
    return constructor;

}
/* create_global_object {{{*/
static JSGlobalContextRef  
create_global_object() 
{
    pthread_rwlock_wrlock(&s_context_lock);
    JSClassDefinition cd; 
    s_ref_quark = g_quark_from_static_string("dwb_js_ref");
    JSGlobalContextRef ctx;

    JSStaticValue global_values[] = {
        { "global",       global_get, NULL,   kJSDefaultAttributes },
        { "session",      global_get_webkit_session, NULL,   kJSDefaultAttributes },
        { 0, 0, 0, 0 }, 
    };

    JSStaticFunction global_functions[] = { 
        { "execute",          global_execute,         kJSDefaultAttributes },
        { "exit",             global_exit,            kJSDefaultAttributes },
        { "bind",             global_bind,            kJSDefaultAttributes },
        { "unbind",           global_unbind,          kJSDefaultAttributes },
        { "include",          global_include,         kJSDefaultAttributes },
        { "_xinclude",         global_xinclude,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };

    JSClassRef class = create_class("dwb", global_functions, global_values);
    ctx = JSGlobalContextCreate(class);
    JSClassRelease(class);


    /** 
     * Get internally used data like configuration files
     * 
     * @namespace
     *      Get internally used data like configuration files
     * @name data
     * @static 
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
    /**
     * The api-version
     * @name version 
     * @readonly
     * @type Number
     * */
    js_set_object_number_property(ctx, global_object, "version", API_VERSION, NULL);


    JSStaticValue data_values[] = {
        { "profile",        data_get_profile, NULL, kJSDefaultAttributes },
        { "sessionName",    data_get_session_name, NULL, kJSDefaultAttributes },
        { "cacheDir",       data_get_cache_dir, NULL, kJSDefaultAttributes },
        { "configDir",      data_get_config_dir, NULL, kJSDefaultAttributes },
        { "systemDataDir",  data_get_system_data_dir, NULL, kJSDefaultAttributes },
        { "userDataDir",    data_get_user_data_dir, NULL, kJSDefaultAttributes },
        { 0, 0, 0,  0 }, 
    };
    class = create_class("data", NULL, data_values);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "data", NULL);
    JSClassRelease(class);

    /**
     * Static object for timed execution 
     * @namespace 
     *      Static object for timed execution 
     * @name timer
     * @static
     * */
    JSStaticFunction timer_functions[] = { 
        { "start",       timer_start,         kJSDefaultAttributes },
        { "stop",        timer_stop,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    class = create_class("timer", timer_functions, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "timer", NULL);
    JSClassRelease(class);
    /**
     * @namespace 
     *      Static object for network related tasks
     * @name net
     * @static
     * */
    JSStaticFunction net_functions[] = { 
        { "sendRequest",      net_send_request,         kJSDefaultAttributes },
        { "sendRequestSync",  net_send_request_sync,         kJSDefaultAttributes },
        { "domainFromHost",   net_domain_from_host,         kJSDefaultAttributes },
        { "parseUri",         net_parse_uri,         kJSDefaultAttributes },
        { "allCookies",       net_all_cookies,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    class = create_class("net", net_functions, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "net", NULL);
    JSClassRelease(class);


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
    create_object(ctx, class, global_object, kJSDefaultAttributes, "settings", NULL);
    JSClassRelease(class);
    /**
     * Static object for input and output
     *
     * @namespace 
     *      Static object for input and output and file operations
     * @name io
     * @static
     * */

    JSStaticFunction io_functions[] = { 
        { "print",     io_print,            kJSDefaultAttributes },
        { "prompt",    io_prompt,           kJSDefaultAttributes },
        { "confirm",   io_confirm,          kJSDefaultAttributes },
        { "read",      io_read,             kJSDefaultAttributes },
        { "write",     io_write,            kJSDefaultAttributes },
        { "dirNames",  io_dir_names,        kJSDefaultAttributes },
        { "notify",    io_notify,           kJSDefaultAttributes },
        { "error",     io_error,            kJSDefaultAttributes },
        { 0,           0,           0 },
    };
    class = create_class("io", io_functions, NULL);
    create_object(ctx, class, global_object, kJSPropertyAttributeDontDelete, "io", NULL);
    JSClassRelease(class);

    /**
     * Static object for system functions
     * 
     * @namespace 
     *      Static object for system functions such as spawning processes,
     *      getting environment variables
     * @name system
     * @static
     * */
    JSStaticFunction system_functions[] = { 
        { "_spawn",          system_spawn,           kJSDefaultAttributes },
        { "spawnSync",       system_spawn_sync,        kJSDefaultAttributes },
        { "getEnv",          system_get_env,           kJSDefaultAttributes },
        { "getPid",          system_get_pid,           kJSDefaultAttributes },
        { "fileTest",        system_file_test,            kJSDefaultAttributes },
        { "mkdir",           system_mkdir,            kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    class = create_class("system", system_functions, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "system", NULL);
    JSClassRelease(class);

    JSStaticFunction tab_functions[] = { 
        { "nth",          tabs_get_nth,        kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSStaticValue tab_values[] = { 
        { "current",      tabs_current, NULL,   kJSDefaultAttributes },
        { "number",       tabs_number,  NULL,   kJSDefaultAttributes },
        { "length",       tabs_length,  NULL,   kJSDefaultAttributes },
        { 0, 0, 0, 0 }, 
    };

    /**
     * Getting webviews
     *
     * @namespace 
     *      Static object that can be used to get webviews
     * @name tabs 
     * @static 
     * */
    class = create_class("tabs", tab_functions, tab_values);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "tabs", NULL);
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
     * // or equivalently, gi
     * signals.onNavigation = onNavigation;
     *
     * */
    cd = kJSClassDefinitionEmpty;
    cd.className = "signals";
    cd.setProperty = signal_set;
    class = JSClassCreate(&cd);

    create_object(ctx, class, global_object, kJSDefaultAttributes, "signals", NULL);
    JSClassRelease(class);

    class = create_class("extensions", NULL, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "extensions", NULL);
    JSClassRelease(class);

    class = create_class("Signal", NULL, NULL);
    create_object(ctx, class, global_object, kJSPropertyAttributeDontDelete, "Signal", NULL);
    JSClassRelease(class);

    /**
     * Utility functions
     *
     * @namespace 
     *      Miscellaneous utility functions
     * @name util 
     * @static 
     * */
    JSStaticFunction util_functions[] = { 
        { "markupEscape",     sutil_markup_escape,         kJSDefaultAttributes },
        { "getMode",          sutil_get_mode,         kJSDefaultAttributes },
        { "getBody",          sutil_get_body,         kJSDefaultAttributes },
        { "dispatchEvent",    sutil_dispatch_event,         kJSDefaultAttributes },
        { "tabComplete",      sutil_tab_complete,         kJSDefaultAttributes },
        { "pathComplete",     sutil_path_complete,    kJSDefaultAttributes },
        { "checksum",         sutil_checksum,         kJSDefaultAttributes },
        { "changeMode",       sutil_change_mode,      kJSDefaultAttributes }, 
        { "_base64Encode",    sutil_base64_encode,    kJSDefaultAttributes },
        { "_base64Decode",    sutil_base64_decode,    kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    class = create_class("util", util_functions, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "util", NULL);
    JSClassRelease(class);

    /**
     * Access to the system clipboard
     * @namespace 
     *      Accessing the system clipboard
     * @name clipboard 
     * @static 
     *
     * */
    JSStaticFunction clipboard_functions[] = { 
        { "get",     clipboard_get,         kJSDefaultAttributes },
        { "set",     clipboard_set,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    class = create_class("clipboard", clipboard_functions, NULL);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "clipboard", NULL);
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
     * */
    create_object(ctx, NULL, global_object, kJSDefaultAttributes, "console", NULL);

    /** 
     * Base class for webkit/gtk objects
     *
     * @name GObject
     * @property {Object}  ... 
     *      Variable number of properties, See the corresponding
     *      gtk/webkitgtk/libsoup documentation for a list of properties, All
     *      properties can be used in camelcase. 
     *
     * @class 
     *      Base class for webkit/gtk objects, all objects derived from GObject
     *      correspond to the original GObjects. They have the same properties,
     *      but javascript properties can also be used in camelcase. 
     *      It is discouraged from settting own properties directly on objects derived
     *      from GObject since these objects are shared between all scripts, use 
     *      {@link script.setPrivate} and {@link script.getPrivate} instead
     * @example 
     * tabs.current["zoom-level"] = 2;
     * //  is equivalent to 
     * tabs.current.zoomLevel = 2; 
     *
     * */
    JSStaticFunction default_functions[] = { 
        { "connect",            gobject_connect,                kJSDefaultAttributes },
        { "blockSignal",        gobject_block_signal,                kJSDefaultAttributes },
        { "unblockSignal",      gobject_unblock_signal,                kJSDefaultAttributes },
        { "disconnect",         gobject_disconnect,             kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "GObject";
    cd.staticFunctions = default_functions;
    cd.getProperty = get_property;
    cd.setProperty = set_property;
    cd.finalize = finalize;
    s_gobject_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_DEFAULT] = create_constructor(ctx, "GObject", s_gobject_class, NULL, NULL);

    /* Webview */
    /**
     * A GtkWidget that shows the webcontent
     *
     * @name WebKitWebView
     * @augments GObject
     * @class GtkWidget that shows webcontent
     * @borrows WebKitWebFrame#inject as prototype.inject
     * @borrows WebKitWebFrame#loadString as prototype.loadString
     * */
    JSStaticFunction wv_functions[] = { 
        { "loadUri",         wv_load_uri,             kJSDefaultAttributes },
        { "stopLoading",         wv_stop_loading,        kJSDefaultAttributes },
        { "history",         wv_history,             kJSDefaultAttributes },
        { "reload",          wv_reload,             kJSDefaultAttributes },
        { "inject",          wv_inject,             kJSDefaultAttributes },
#if WEBKIT_CHECK_VERSION(1, 10, 0) && CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 12, 0)
        { "toPng",           wv_to_png,             kJSDefaultAttributes },
        { "toPng64",         wv_to_png64,             kJSDefaultAttributes },
#endif
        { 0, 0, 0 }, 
    };
    JSStaticValue wv_values[] = {
        { "mainFrame",     wv_get_main_frame, NULL, kJSDefaultAttributes }, 
        { "focusedFrame",  wv_get_focused_frame, NULL, kJSDefaultAttributes }, 
        { "allFrames",     wv_get_all_frames, NULL, kJSDefaultAttributes }, 
        { "number",        wv_get_number, NULL, kJSDefaultAttributes }, 
        { "tabWidget",     wv_get_tab_widget, NULL, kJSDefaultAttributes }, 
        { "tabBox",        wv_get_tab_box, NULL, kJSDefaultAttributes }, 
        { "tabLabel",      wv_get_tab_label, NULL, kJSDefaultAttributes }, 
        { "tabIcon",       wv_get_tab_icon, NULL, kJSDefaultAttributes }, 
        { "historyList",    wv_get_history_list, NULL, kJSDefaultAttributes }, 
        { "scrolledWindow",wv_get_scrolled_window, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };

    cd.className = "WebKitWebView";
    cd.staticFunctions = wv_functions;
    cd.staticValues = wv_values;
    cd.parentClass = s_gobject_class;
    s_webview_class = JSClassCreate(&cd);


    s_constructors[CONSTRUCTOR_WEBVIEW] = create_constructor(ctx, "WebKitWebView", s_webview_class, NULL, NULL);

    cd = kJSClassDefinitionEmpty;
    cd.className = "HiddenWebView";
    cd.staticFunctions = wv_functions;
    cd.parentClass = s_gobject_class;

    s_constructors[CONSTRUCTOR_HIDDEN_WEB_VIEW] = create_constructor(ctx, "HiddenWebView", s_webview_class, hwv_constructor_cb, NULL);


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
        { "domain", frame_get_domain, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };

    cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitWebFrame";
    cd.staticFunctions = frame_functions;
    cd.staticValues = frame_values;
    cd.parentClass = s_gobject_class;
    s_frame_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_FRAME] = create_constructor(ctx, "WebKitWebFrame", s_frame_class, NULL, NULL);

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
        { "uri",     message_get_uri, NULL, kJSDefaultAttributes }, 
        { "firstParty",     message_get_first_party, NULL, kJSDefaultAttributes }, 
        { "requestHeaders",     message_get_request_headers, NULL, kJSDefaultAttributes }, 
        { "responseHeaders",     message_get_response_headers, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSStaticFunction message_functions[] = {
        { "setStatus",       message_set_status, kJSDefaultAttributes }, 
        { "setResponse",     message_set_response, kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };

    cd.className = "SoupMessage";
    cd.staticFunctions = message_functions;
    cd.staticValues = message_values;
    cd.parentClass = s_gobject_class;
    s_message_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_SOUP_MESSAGE] = create_constructor(ctx, "SoupMessage", s_message_class, NULL, NULL);


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
    cd.parentClass = s_gobject_class;
    s_history_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_HISTORY_LIST] = create_constructor(ctx, "WebKitWebBackForwardList", s_history_class, NULL, NULL);

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
     *     io.print(response);
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
    s_deferred_class = JSClassCreate(&cd);
    s_constructors[CONSTRUCTOR_DEFERRED] = create_constructor(ctx, "Deferred", s_deferred_class, deferred_constructor_cb, NULL);

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
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "SecureWidget";
    cd.staticFunctions = secure_widget_functions;
    cd.parentClass = s_gobject_class;
    s_secure_widget_class = JSClassCreate(&cd);

    JSStaticFunction widget_functions[] = { 
        { "destroy",                widget_destroy,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "GtkWidget";
    cd.staticFunctions = widget_functions;
    cd.parentClass = s_secure_widget_class;
    s_widget_class = JSClassCreate(&cd);
    s_constructors[CONSTRUCTOR_WIDGET] = create_constructor(ctx, "GtkWidget", s_widget_class, widget_constructor_cb, NULL);

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
    cd.parentClass = s_widget_class;
    s_menu_class = JSClassCreate(&cd);

    /** 
     * Static object that holds dwb's GtkWidgets
     *
     * @namespace 
     *      Static object that holds dwb's GtkWidgets. Most widgets can be accessed
     *      directly from scripts
     *      see <a>http://portix.bitbucket.org/dwb/resources/layout.html</a> for an
     *      overview of all widgets.
     * @name gui
     * @static
     *
     * */
    JSStaticValue gui_values[] = {
        { "window",           gui_get_window, NULL, kJSDefaultAttributes }, 
        { "mainBox",          gui_get_main_box, NULL, kJSDefaultAttributes }, 
        { "tabBox",           gui_get_tab_box, NULL, kJSDefaultAttributes }, 
        { "contentBox",       gui_get_content_box, NULL, kJSDefaultAttributes }, 
        { "statusWidget",     gui_get_status_widget, NULL, kJSDefaultAttributes }, 
        { "statusAlignment",  gui_get_status_alignment, NULL, kJSDefaultAttributes }, 
        { "statusBox",        gui_get_status_box, NULL, kJSDefaultAttributes }, 
        { "messageLabel",     gui_get_message_label, NULL, kJSDefaultAttributes }, 
        { "entry",            gui_get_entry, NULL, kJSDefaultAttributes }, 
        { "uriLabel",         gui_get_uri_label, NULL, kJSDefaultAttributes }, 
        { "statusLabel",      gui_get_status_label, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "gui";
    cd.staticValues = gui_values;
    class = JSClassCreate(&cd);
    create_object(ctx, class, global_object, kJSDefaultAttributes, "gui", NULL);
    JSClassRelease(class);

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
    cd.parentClass = s_gobject_class;
    s_download_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_DOWNLOAD] = create_constructor(ctx, "WebKitDownload", s_download_class, download_constructor_cb, NULL);

    JSStaticFunction server_functions[] = { 
        { "addHandler",          server_handler,        kJSDefaultAttributes },
        { "run",                 server_run,            kJSDefaultAttributes },
        { "stop",                server_stop,           kJSDefaultAttributes },
        { "shutdown",            server_shutdown,           kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.staticFunctions = server_functions;
    cd.parentClass = s_gobject_class;
    s_soup_server_class = JSClassCreate(&cd);

    s_constructors[CONSTRUCTOR_SERVER] = create_constructor(ctx, "Server", s_soup_server_class, server_constructor_cb, NULL);

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
    s_cookie_class = JSClassCreate(&cd);
    s_constructors[CONSTRUCTOR_COOKIE] = create_constructor(ctx, "Cookie", s_soup_server_class, cookie_constructor_cb, NULL);

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
    s_soup_header_class = JSClassCreate(&cd);
    
    s_soup_session = make_object_for_class(ctx, s_gobject_class, G_OBJECT(webkit_get_default_session()), false);
    JSValueProtect(ctx, s_soup_session);
    pthread_rwlock_unlock(&s_context_lock);
    return ctx;
}/*}}}*/
/*}}}*/

/* INIT AND END {{{*/
/* apply_scripts {{{*/
    static void 
apply_scripts() 
{
    int length = g_slist_length(s_script_list); 
    int i=0;
    JSValueRef exports[1];
    size_t argc = 0;
    char *path;

    // XXX Not needed?
    JSObjectRef *objects = g_malloc(length * sizeof(JSObjectRef));
    for (GSList *l=s_script_list; l; l=l->next, i++) 
    {
        objects[i] = JSObjectMake(s_global_context, NULL, NULL);
        js_set_property(s_global_context, objects[i], "func", l->data, 0, NULL);
    }
    if (s_init_before != NULL) 
    {
        JSValueRef argv[] = {  JSObjectMakeArray(s_global_context, length, (JSValueRef*)objects, NULL) };
        JSObjectCallAsFunction(s_global_context, s_init_before, NULL, 1, argv, NULL);
        JSValueUnprotect(s_global_context, s_init_before);
    }

    for (GSList *l = s_script_list; l; l=l->next) 
    {
        argc = 0;
        path = js_get_string_property(s_global_context, l->data, "path");
        if (path != NULL)
        {
            exports[0] = get_exports(s_global_context, path);
            argc = 1;
        }

        JSObjectCallAsFunction(s_global_context, l->data, l->data, argc, argc > 0 ? exports : NULL, NULL);
    }
    g_slist_free(s_script_list);
    s_script_list = NULL;

    if (s_init_after != NULL) 
    {
        JSObjectCallAsFunction(s_global_context, s_init_after, NULL, 0, NULL, NULL);
        JSValueUnprotect(s_global_context, s_init_after);
    }
    g_free(objects);
}/*}}}*/

/* scripts_create_tab {{{*/
void 
scripts_create_tab(GList *gl) 
{
    static gboolean applied = false;
    if (s_global_context == NULL )  
    {
        VIEW(gl)->script_wv = NULL;
        return;
    }
    if (!applied) 
    {
        apply_scripts();
        applied = true;
    }
    JSObjectRef o = make_object(s_global_context, G_OBJECT(VIEW(gl)->web));

    JSValueProtect(s_global_context, o);
    VIEW(gl)->script_wv = o;
}/*}}}*/

/* scripts_remove_tab {{{*/
void 
scripts_remove_tab(JSObjectRef obj) 
{
    if (obj == NULL || !TRY_CONTEXT_LOCK) 
        return;
    if (s_global_context != NULL) 
    {
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
        JSValueUnprotect(s_global_context, obj);
    }
    CONTEXT_UNLOCK;
}/*}}}*/

void 
init_script(const char *path, const char *script, gboolean is_archive, const char *template, int offset)
{
    char *debug = NULL;
    if (s_global_context == NULL) 
        s_global_context = create_global_object();

    if (js_check_syntax(s_global_context, script, path, 2)) 
    {
        debug = g_strdup_printf(template, path, script);
        JSObjectRef function = js_make_function(s_global_context, debug, path, offset);
        if (is_archive)
            js_set_object_property(s_global_context, function, "path", path, NULL);

        if (function != NULL) 
            s_script_list = g_slist_prepend(s_script_list, function);
    }
    g_free(debug);
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
    if (!TRY_CONTEXT_LOCK) 
        return;
    if (s_global_context != NULL)
    {
        JSStringRef js_script = JSStringCreateWithUTF8CString(script);
        JSEvaluateScript(s_global_context, js_script, NULL, NULL, 0, NULL);
        JSStringRelease(js_script);
    }
    CONTEXT_UNLOCK;
}

JSObjectRef 
get_private(JSContextRef ctx, char *name) 
{
    JSStringRef js_name = JSStringCreateWithUTF8CString(name);
    JSObjectRef global_object = JSContextGetGlobalObject(s_global_context);

    JSObjectRef ret = js_get_object_property(s_global_context, global_object, name);
    JSValueProtect(s_global_context, ret);
    JSObjectDeleteProperty(s_global_context, global_object, js_name, NULL);

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
            JSObjectRef o = make_object(s_global_context, G_OBJECT(VIEW(gl)->web));
            JSValueProtect(s_global_context, o);
            VIEW(gl)->script_wv = o;
        }
        apply_scripts();
    }
}

/* scripts_init {{{*/
gboolean 
scripts_init(gboolean force) 
{
    dwb.misc.script_signals = 0;
    if (s_global_context == NULL) 
    {
        if (force || dwb.misc.js_api == JS_API_ENABLED) 
            s_global_context = create_global_object();
        else 
            return false;
    }
    s_gobject_signals = g_ptr_array_new();
    s_exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)scripts_unprotect);

    dwb.state.script_completion = NULL;

    char *dir = util_get_data_dir(LIBJS_DIR);
    if (dir != NULL) 
    {
        GString *content = g_string_new(NULL);
        util_get_directory_content(content, dir, "js");
        if (content != NULL)  
        {
            JSStringRef js_script = JSStringCreateWithUTF8CString(content->str);
            JSEvaluateScript(s_global_context, js_script, NULL, NULL, 0, NULL);
            JSStringRelease(js_script);
        }
        g_string_free(content, true);
        g_free(dir);
    }

    UNDEFINED = JSValueMakeUndefined(s_global_context);
    JSValueProtect(s_global_context, UNDEFINED);
    NIL = JSValueMakeNull(s_global_context);
    JSValueProtect(s_global_context, NIL);

    s_init_before = get_private(s_global_context, "_initBefore");
    s_init_after = get_private(s_global_context, "_initAfter");
    //s_private = get_private(s_global_context, "_private");

    JSObjectRef o = JSObjectMakeArray(s_global_context, 0, NULL, NULL);
    s_array_contructor = js_get_object_property(s_global_context, o, "constructor");
    JSValueProtect(s_global_context, s_array_contructor); 
    return true;
}/*}}}*/

gboolean 
scripts_execute_one(const char *script) 
{
    gboolean ret = false;
    if (s_global_context != NULL)
    {
        char *debug = g_strdup_printf(SCRIPT_TEMPLATE_INCLUDE, "dwb:scripts", script);
        ret = js_execute(s_global_context, debug, NULL) != NULL;
        g_free(debug);
    }

    return ret;
}
void
scripts_unprotect(JSObjectRef obj) 
{

    if (obj == NULL || !TRY_CONTEXT_LOCK)
        return;
    if (s_global_context != NULL) 
        JSValueUnprotect(s_global_context, obj);
    CONTEXT_UNLOCK;
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
            if (js_check_syntax(s_global_context, tmp, scripts[i], 0)) 
            {
                fprintf(stderr, "Syntax of %s is correct.\n", scripts[i]);
            }
            g_free(content);
        }
    }
    scripts_end();
}

/* scripts_end {{{*/
void
scripts_end() 
{
    pthread_rwlock_wrlock(&s_context_lock);
    if (s_global_context != NULL) 
    {
        // keys
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
                unbind_free_keymap(s_global_context, l);
        }
        // signals
        for (guint i=0; i<s_gobject_signals->len; i++)
        {
            SigData *data = g_ptr_array_index(s_gobject_signals, i);
            g_signal_handler_disconnect(data->instance, data->id);
            g_free(data);
        }
        g_ptr_array_free(s_gobject_signals, false);
        s_gobject_signals = NULL;
        g_hash_table_unref(s_exports);

        
        for (int i=0; i<CONSTRUCTOR_LAST; i++) 
            JSValueUnprotect(s_global_context, s_constructors[i]);

        for (int i=SCRIPTS_SIG_FIRST; i<SCRIPTS_SIG_LAST; ++i)
            s_sig_objects[i] = 0;
        dwb.misc.script_signals = 0;

        for (GSList *timer = s_timers; timer; timer=timer->next)
            g_source_remove(GPOINTER_TO_INT(timer->data));

        for (GSList *server = s_servers; server; server=server->next)
        {
            soup_server_disconnect(server->data);
        }
        g_slist_free(s_servers);

        JSValueUnprotect(s_global_context, s_array_contructor);
        JSValueUnprotect(s_global_context, UNDEFINED);
        JSValueUnprotect(s_global_context, NIL);
        JSValueUnprotect(s_global_context, s_soup_session);
        //JSValueUnprotect(s_global_context, s_private);
        JSClassRelease(s_gobject_class);
        JSClassRelease(s_webview_class);
        JSClassRelease(s_frame_class);
        JSClassRelease(s_download_class);
        JSClassRelease(s_widget_class);
        JSClassRelease(s_menu_class);
        JSClassRelease(s_secure_widget_class);
        JSClassRelease(s_message_class);
        JSClassRelease(s_history_class);
        JSClassRelease(s_soup_header_class);
        JSClassRelease(s_soup_server_class);
        JSClassRelease(s_cookie_class);
        JSGlobalContextRelease(s_global_context);
        s_global_context = NULL;
    }
    pthread_rwlock_unlock(&s_context_lock);
}/*}}}*//*}}}*/
