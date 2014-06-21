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
 * The main window
 * @name window
 * @memberOf gui
 * @type GtkWindow
 * */
static JSValueRef
gui_get_window(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.window), true);
}
/** 
 * The main container. Child of window
 * @name mainBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_main_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.vbox), true);
}
/** 
 * The box used for tab labels. Child of mainBox
 * @name tabBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_tab_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
#if _HAS_GTK3
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.tabbox), true);
#else
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.tabcontainer), true);
#endif
}
/** 
 * The box used for the main content. Child of mainBox
 * @name contentBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_content_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.mainbox), true);
}
/** 
 * The outmost statusbar widget, used for setting the statusbars colors, child of mainBox.
 * @name statusWidget
 * @memberOf gui
 * @type GtkEventBox
 * */
static JSValueRef
gui_get_status_widget(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.statusbox), true);
}
/** 
 * Used for the statusbar alignment, child of statusWidget.
 * @name statusAlignment
 * @memberOf gui
 * @type GtkAlignment
 * */
static JSValueRef
gui_get_status_alignment(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.alignment), true);
}
/** 
 * The box that contains the statusbar widgets, grandchild of statusAlignment
 * @name statusBox
 * @memberOf gui
 * @type GtkBox
 * */
static JSValueRef
gui_get_status_box(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.status_hbox), true);
}
/** 
 * Label used for notifications, first child of statusBox
 * @name messageLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_message_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.lstatus), true);
}
/** 
 * The entry, second child of statusBox
 * @name entry
 * @memberOf gui
 * @type GtkEntry
 * */
static JSValueRef
gui_get_entry(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.entry), true);
}
/** 
 * The uri label, third child of statusBox
 * @name uriLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_uri_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.urilabel), true);
}
/** 
 * Label used for status information, fourth child of statusBox
 * @name statusLabel
 * @memberOf gui
 * @type GtkLabel
 * */
static JSValueRef
gui_get_status_label(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    ScriptContext *sctx = scripts_get_context();
    return make_object_for_class(ctx, sctx->classes[CLASS_SECURE_WIDGET], G_OBJECT(dwb.gui.rstatus), true);
}
/*}}}*/
/** 
 * Height of the tabbar, favicons will be rescaled to fit into the tab
 * @name tabBarHeight 
 * @memberOf gui
 * @type Number
 * */
static JSValueRef
gui_get_tabbar_height(JSContextRef ctx, JSObjectRef object, JSStringRef property, JSValueRef* exception) {
    return JSValueMakeNumber(ctx, dwb.misc.tabbar_height);
}
static bool 
gui_set_tabbar_height(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception) {
    double bar_height = JSValueToNumber(ctx, value, exception);
    if (!isnan(bar_height)) {
        dwb.misc.tabbar_height = (int) bar_height;
        return true;
    }
    return false;
}/*}}}*/

JSObjectRef
gui_initialize(JSContextRef ctx) {
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
     * @example
     * //!javascript
     *
     * var gui = namespace("gui");
     *
     * */
    JSObjectRef global_object = JSContextGetGlobalObject(ctx);
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
        { "tabBarHeight",     gui_get_tabbar_height, gui_set_tabbar_height, kJSPropertyAttributeDontDelete|kJSPropertyAttributeDontEnum }, 
        { 0, 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("gui", NULL, gui_values, NULL);
    JSObjectRef ret = scripts_create_object(ctx, klass, global_object, kJSDefaultAttributes, "gui", NULL);
    JSClassRelease(klass);
    return ret;
}
