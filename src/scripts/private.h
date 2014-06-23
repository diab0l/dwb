#ifndef __DWB_SCRIPT_PRIVATE_H__
#define __DWB_SCRIPT_PRIVATE_H__

typedef struct ScriptContext_s ScriptContext;

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <errno.h>
#include <JavaScriptCore/JavaScript.h>
#include <glib.h>
#include <cairo.h>
#include <exar.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../dwb.h"
#include "../scripts.h" 
#include "../session.h" 
#include "../util.h" 
#include "../js.h" 
#include "../soup.h" 
#include "../domain.h" 
#include "../application.h" 
#include "../completion.h" 
#include "../entry.h" 
#include "../secret.h" 
#include "../scripts.h"
#include "shared.h"
#include "ns_util.h"
#include "ns_system.h"
#include "ns_tabs.h"
#include "ns_data.h"
#include "ns_io.h"
#include "ns_gui.h"
#include "ns_timer.h"
#include "ns_global.h"
#include "ns_clipboard.h"
#include "ns_net.h"
#include "ns_keyring.h"
#include "cl_deferred.h"
#include "cl_gobject.h"
#include "cl_webview.h"
#include "cl_dom.h"
#include "cl_frame.h"
#include "cl_message.h"
#include "cl_gtimer.h"
#include "callback.h"


enum {
    CLASS_GOBJECT,
    CLASS_WEBVIEW, 
    CLASS_FRAME, 
    CLASS_DOWNLOAD, 
    CLASS_WIDGET, 
    CLASS_MENU, 
    CLASS_SECURE_WIDGET, 
    CLASS_HIDDEN_WEBVIEW, 
    CLASS_MESSAGE, 
    CLASS_DEFERRED, 
    CLASS_HISTORY,
    CLASS_SOUP_HEADER, 
    CLASS_COOKIE,
#if WEBKIT_CHECK_VERSION(1, 10, 0)
    CLASS_FILE_CHOOSER,
#endif
    CLASS_DOM_OBJECT, 
    CLASS_DOM_EVENT, 
    CLASS_TIMER,
    CLASS_LAST,
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
    CONSTRUCTOR_COOKIE,
    CONSTRUCTOR_MENU,
    CONSTRUCTOR_ARRAY,
    CONSTRUCTOR_TIMER, 
    CONSTRUCTOR_LAST,
};

enum {
    NAMESPACE_CLIPBOARD, 
    NAMESPACE_CONSOLE, 
    NAMESPACE_DATA, 
    NAMESPACE_EXTENSIONS, 
    NAMESPACE_GUI, 
    NAMESPACE_IO, 
    NAMESPACE_NET, 
    NAMESPACE_SIGNALS, 
    NAMESPACE_SYSTEM, 
    NAMESPACE_TABS, 
    NAMESPACE_TIMER, 
    NAMESPACE_UTIL, 
#ifdef WITH_LIBSECRET
    NAMESPACE_KEYRING, 
#endif
    NAMESPACE_LAST,
};

struct ScriptContext_s {
    JSGlobalContextRef global_context;

    JSObjectRef sig_objects[SCRIPTS_SIG_LAST];

    GSList *script_list;
    GSList *autoload;
    GSList *timers;
    GSList *created_widgets;
    GPtrArray *gobject_signals;
    GHashTable *exports;

    JSClassRef classes[CLASS_LAST];
    JSObjectRef constructors[CONSTRUCTOR_LAST];
    JSValueRef namespaces[NAMESPACE_LAST];

    JSObjectRef session;

    JSObjectRef init_before;
    JSObjectRef init_after;

    JSObjectRef complete;

    GQuark ref_quark;
    gboolean keymap_dirty;
}; 


char * 
scripts_get_body(JSContextRef ctx, JSObjectRef func, JSValueRef *exc);

ScriptContext *
scripts_get_context();

JSContextRef 
scripts_get_global_context();

void
scripts_release_global_context();

JSValueRef 
scripts_get_nil(void);

JSClassRef 
scripts_create_class(const char *name, JSStaticFunction [], JSStaticValue [], JSObjectGetPropertyCallback );

JSObjectRef 
scripts_create_object(JSContextRef, JSClassRef, JSObjectRef, JSClassAttributes, const char *, void *); 

JSValueRef 
scripts_include(JSContextRef, const char *, const char *, gboolean, gboolean , size_t, const JSValueRef *, JSValueRef *exc);

JSObjectRef
scripts_get_exports(JSContextRef ctx, const char *path);

JSObjectRef 
suri_to_object(JSContextRef ctx, SoupURI *uri, JSValueRef *exception);

void 
sigdata_append(gulong sigid, GObject *instance);
void 
sigdata_remove(gulong sigid, GObject *instance);

JSObjectRef 
scripts_create_constructor(JSContextRef, char *, JSClassRef, JSObjectCallAsConstructorCallback, JSValueRef *);

JSObjectRef 
scripts_make_object(JSContextRef ctx, GObject *o);

char *
uncamelize(char *uncamel, const char *camel, char rep, size_t length);

#define NIL (scripts_get_nil())

#define EXCEPTION(X)   "DWB EXCEPTION : "X

#define kJSDefaultProperty  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )
#define kJSDefaultAttributes  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )

#define PROP_LENGTH 128

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
    return NULL; \
}

#endif
