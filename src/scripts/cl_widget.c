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

static JSObjectRef 
hwv_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    GObject *wv = G_OBJECT(webkit_web_view_new());
    return make_object_for_class(ctx, CLASS_HIDDEN_WEBVIEW, wv, false);
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
        return NULL;
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, NULL);

    if (! (GTK_IS_BOX(widget) || GTK_IS_MENU(widget)))
    {
        js_make_exception(ctx, exc, EXCEPTION("Widget.reorderChild: Not a GtkBox or GtkMenu"));
        return NULL;
    }

    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return NULL;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return NULL;
    double position = JSValueToNumber(ctx, argv[1], exc);
    if (isnan(position))
        return NULL;
    if (GTK_IS_BOX(widget)) {
        gtk_box_reorder_child(GTK_BOX(widget), child, (int)position);
    }
    else {
        gtk_menu_reorder_child(GTK_MENU(widget), child, (int)position);
    }
    return NULL;
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
        return NULL;

    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, NULL);

    if (! GTK_IS_BOX(widget))
    {
        js_make_exception(ctx, exc, EXCEPTION("Widget.packStart: Not a GtkBox"));
        return NULL;
    }

    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return NULL;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return NULL;


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
    return NULL;
}

JSValueRef
container_child_func(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc, void (*func)(GtkContainer *, GtkWidget *)) {
    GtkWidget *widget = JSObjectGetPrivate(this);
    g_return_val_if_fail(widget != NULL, NULL);

    if (! GTK_IS_CONTAINER(widget))
    {
        js_make_exception(ctx, exc, EXCEPTION("Not a GtkContainer"));
        return NULL;
    }
    JSObjectRef jschild = JSValueToObject(ctx, argv[0], exc);
    if (jschild == NULL)
        return NULL;
    GtkWidget *child = JSObjectGetPrivate(jschild);
    if (child == NULL || !GTK_IS_WIDGET(child))
        return NULL;
    func(GTK_CONTAINER(widget), child);
    return NULL;
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

    g_return_val_if_fail(widget != NULL, NULL);

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
    g_return_val_if_fail(widget != NULL, NULL);
    ScriptContext *sctx = scripts_get_context();
    if (sctx != NULL) {
        sctx->created_widgets = g_slist_remove_all(sctx->created_widgets, widget);
        gtk_widget_destroy(widget);
        scripts_release_context();
    }
    return NULL;
}

static JSObjectRef 
widget_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    GType type = 0;
    if (argc > 0)
    {
        ScriptContext *sctx = scripts_get_context();
        if (sctx == NULL) 
            return JSValueToObject(ctx, NIL, exception);
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
        sctx->created_widgets = g_slist_prepend(sctx->created_widgets, widget);
        JSObjectRef ret = scripts_make_object(ctx, G_OBJECT(widget));
        scripts_release_context();
        return ret;
    }
    return JSValueToObject(ctx, NIL, NULL);
}

void 
widget_initialize(ScriptContext *sctx) {
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
    JSClassDefinition cd = kJSClassDefinitionEmpty;

    cd.className = "SecureWidget";
    cd.staticFunctions = secure_widget_functions;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_SECURE_WIDGET] = JSClassCreate(&cd);

    JSStaticFunction widget_functions[] = { 
        { "destroy",                widget_destroy,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    cd = kJSClassDefinitionEmpty;
    cd.className = "GtkWidget";
    cd.staticFunctions = widget_functions;
    cd.parentClass = sctx->classes[CLASS_SECURE_WIDGET];
    sctx->classes[CLASS_WIDGET] = JSClassCreate(&cd);
    sctx->constructors[CONSTRUCTOR_WIDGET] = scripts_create_constructor(sctx->global_context, "GtkWidget", sctx->classes[CLASS_WIDGET], widget_constructor_cb, NULL);

    cd = kJSClassDefinitionEmpty;
    cd.className = "HiddenWebView";
    cd.staticFunctions = widget_functions;
    cd.parentClass = sctx->classes[CLASS_WEBVIEW];
    sctx->classes[CLASS_HIDDEN_WEBVIEW] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_HIDDEN_WEB_VIEW] = scripts_create_constructor(sctx->global_context, "HiddenWebView", sctx->classes[CLASS_HIDDEN_WEBVIEW], hwv_constructor_cb, NULL);

}
