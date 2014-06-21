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
#include "secret.h" 
#include "scripts.h"
#include "script_util.h"
#include "script_system.h"
#include "script_deferred.h"
#include "script_tabs.h"
#include "script_data.h"
#include "script_io.h"
#include "script_gui.h"
#include "script_timer.h"
#include "script_global.h"
#include "script_clipboard.h"
#include "script_dom.h"
#include "script_net.h"
#include "script_keyring.h"


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
scripts_call_as_function(JSContextRef ctx, JSObjectRef func, JSObjectRef this, size_t argc, const JSValueRef argv[]);

JSValueRef 
scripts_get_nil(void);

JSObjectRef 
make_object_for_class(JSContextRef ctx, JSClassRef class, GObject *o, gboolean protect);

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

#define NIL (scripts_get_nil())

#define EXCEPTION(X)   "DWB EXCEPTION : "X

#define kJSDefaultProperty  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )
#define kJSDefaultAttributes  (kJSPropertyAttributeDontDelete | kJSPropertyAttributeReadOnly )

#endif
