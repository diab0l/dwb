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

static void 
object_destroy_cb(JSObjectRef o) 
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSObjectSetPrivate(o, NULL);
        JSValueUnprotect(ctx, o);
        scripts_release_global_context();
    }
}

JSObjectRef 
make_object_for_class(JSContextRef ctx, int iclass, GObject *o, gboolean protect)
{
    ScriptContext *sctx = scripts_get_context();
    if (sctx == NULL) 
        return JSValueToObject(ctx, NIL, NULL);

    JSObjectRef retobj = g_object_get_qdata(o, sctx->ref_quark);
    if (retobj != NULL) {
        goto finish;
    }

    retobj = JSObjectMake(ctx, sctx->classes[iclass], o);
    if (protect) 
    {
        g_object_set_qdata_full(o, sctx->ref_quark, retobj, (GDestroyNotify)object_destroy_cb);
        JSValueProtect(ctx, retobj);
    }
    else 
        g_object_set_qdata_full(o, sctx->ref_quark, retobj, NULL);

finish:
    scripts_release_context();
    return retobj;
}

bool
set_property_cb(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception) {
    return true;
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
