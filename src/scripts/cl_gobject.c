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

typedef struct _SSignal {
    int id;
    GSignalQuery *query;
    JSObjectRef object;
    JSObjectRef func;
} SSignal;
#define S_SIGNAL(X) ((SSignal*)X->data)

static void 
ssignal_free(SSignal *sig) 
{
    if (sig != NULL) 
    {
        JSContextRef ctx = scripts_get_global_context();
        if (ctx != NULL) {
            if (sig->func)
                JSValueUnprotect(ctx, sig->func);
            if (sig->object)
                JSValueUnprotect(ctx, sig->object);
            scripts_release_global_context();
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
    if (o == NULL) {
        return NULL;
    }

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
    
        JSObjectRef retobj = scripts_make_object(ctx, object);
        g_object_unref(object);
        ret = retobj;
    }
    return ret;
}

/** 
 * Callback called for GObject signals, <span class="ilkw">this</span> will refer to the object
 * that connected to the signal
 * @callback GObject~connectCallback
 *
 * @param {...Object} varargs
 *      Variable number of additional arguments, see the correspondent
 *      gtk/glib/webkit documentation. Note that the first argument is omitted and
 *      <span class="ilkw">this</span> will correspond to the first parameter and that only
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

    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL)
        return false;

    va_start(args, sig);
#define CHECK_NUMBER(GTYPE, TYPE) G_STMT_START if (gtype == G_TYPE_##GTYPE) { \
    TYPE MM_value = va_arg(args, TYPE); \
    cur = JSValueMakeNumber(ctx, MM_value); goto apply;} G_STMT_END
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
            cur = JSValueMakeBoolean(ctx, value);
        }
        else if (sig->query->param_types[i] == G_TYPE_STRING) 
        {
            char *value = va_arg(args, char *);
            cur = js_char_to_value(ctx, value);
        }
        else if (G_TYPE_IS_CLASSED(gtype)) 
        {
            GObject *value = va_arg(args, gpointer);
            if (value != NULL) // avoid conversion to JSObjectRef
                cur = scripts_make_object(ctx, value);
            else 
                cur = NIL;
        }
        else 
        {
            va_arg(args, void*);
            cur = NULL;
        }

apply:
        argv[i] = cur;
    }
#undef CHECK_NUMBER
    JSValueRef ret = scripts_call_as_function(ctx, sig->func, sig->object, sig->query->n_params, argv);
    if (JSValueIsBoolean(ctx, ret)) 
    {
        result = JSValueToBoolean(ctx, ret);
    }
    scripts_release_global_context();
    return result;
}
static void
on_disconnect_object(SSignal *sig, GClosure *closure)
{
    ssignal_free(sig);
}
/** 
 * Called when a property of an object changes, <span class="ilkw">this</span> will refer to the object
 * that connected to the signal.
 * @callback GObject~notifyCallback
 *
 * */
static void
notify_callback(GObject *o, GParamSpec *param, SSignal *sig)
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        g_signal_handlers_block_by_func(o, G_CALLBACK(notify_callback), sig);
        scripts_call_as_function(ctx, sig->func, scripts_make_object(ctx, o), 0, NULL);
        g_signal_handlers_unblock_by_func(o, G_CALLBACK(notify_callback), sig);
        scripts_release_global_context();
    }
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
    if (argc == 0) {
        return NULL;
    }
    double sigid = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(sigid)) 
    {
        GObject *o = JSObjectGetPrivate(this);
        if (o != NULL)
            g_signal_handler_block(o, (int)sigid);
    }
    return NULL;
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
    if (argc == 0) {
        return NULL;
    }

    double sigid = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(sigid))
    {
        GObject *o = JSObjectGetPrivate(this);
        if (o != NULL)
            g_signal_handler_unblock(o, (int)sigid);
    }
    return NULL;
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
    if (argc == 0) {
        return JSValueMakeBoolean(ctx, false);
    }
    int id = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(id))
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

static void 
finalize(JSObjectRef o)
{
    GObject *ob = JSObjectGetPrivate(o);
    if (ob != NULL)
    {
        ScriptContext *sctx = scripts_get_context();
        g_object_steal_qdata(ob, sctx->ref_quark);
    }
}

void
gobject_initialize(ScriptContext *sctx) {
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
     * //!javascript
     *
     * var tabs = namespace("tabs");
     *
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
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "GObject";
    cd.staticFunctions = default_functions;
    cd.getProperty = get_property;
    cd.setProperty = set_property;
    cd.finalize = finalize;
    sctx->classes[CLASS_GOBJECT] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_DEFAULT] = scripts_create_constructor(sctx->global_context, "GObject", sctx->classes[CLASS_GOBJECT], NULL, NULL);
}
