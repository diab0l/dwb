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

/**
 * Called when a menu item was activated that was added to the popup menu,
 * <span class="ilkw">this</span> will refer to the GtkMenuItem.
 *
 * @callback GtkMenu~onMenuActivate
 * */
static void 
menu_callback(GtkMenuItem *item, JSObjectRef callback)
{
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSObjectRef self =  make_object_for_class(ctx, CLASS_WIDGET, G_OBJECT(item), true);
        scripts_call_as_function(ctx, callback, self, 0, NULL);
        scripts_release_global_context();
    }
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
menu_add_items(JSContextRef ctx, JSObjectRef function, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSObjectRef arg;
    if (argc == 0 || (arg = JSValueToObject(ctx, argv[0], exc)) == NULL )
        return NULL;

    double p;
    int position = -1;
    char *label = NULL;
    GtkWidget *item;

    JSObjectRef callback, o;
    JSValueRef current, label_value, callback_value;
    JSObjectRef js_submenu;
    GObject *submenu;

    GtkMenu *menu = JSObjectGetPrivate(self);
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
    return NULL;

}

void 
menu_initialize(ScriptContext *sctx) {
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
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "GtkMenu";
    cd.staticFunctions = menu_functions;
    cd.parentClass = sctx->classes[CLASS_WIDGET];
    sctx->classes[CLASS_MENU] = JSClassCreate(&cd);
}
