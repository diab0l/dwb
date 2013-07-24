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

#include <string.h>
#include "dwb.h"
#include "completion.h"
#include "commands.h"
#include "entry.h"
#include "scripts.h"
#include "util.h"
#include "visual.h"


/* dwb_entry_keyrelease_cb {{{*/
gboolean 
callback_entry_insert_text(GtkWidget* entry, char *new_text, int length, gpointer position) {

    const char *text = GET_TEXT();
    int newlen = strlen(text) + length + 1;
    char buffer[newlen];
    snprintf(buffer, sizeof(buffer), "%s%s", text, new_text);
    if (dwb.state.mode == QUICK_MARK_OPEN) 
        return dwb_update_find_quickmark(buffer);
    
    return false;
}
gboolean
callback_find_timeout(char *text) 
{
    if (!g_strcmp0(text, GET_TEXT())) 
        dwb_update_search();
    g_free(text);
    return false;
}
gboolean 
callback_entry_key_release(GtkWidget* entry, GdkEventKey *e) {

    if (dwb.state.mode == HINT_MODE) {
        if (e->keyval == GDK_KEY_BackSpace) 
            return dwb_update_hints(e);
        
    }
    if (dwb.state.mode == FIND_MODE) 
    {
        if (dwb.misc.find_delay > 0)
            g_timeout_add(dwb.misc.find_delay, (GSourceFunc)callback_find_timeout, g_strdup(GET_TEXT()));
        else 
            dwb_update_search();
    }
    return false;
}/*}}}*/

/* dwb_entry_keypress_cb(GtkWidget* entry, GdkEventKey *e) {{{*/
gboolean 
callback_entry_key_press(GtkWidget* entry, GdkEventKey *e) 
{
    Mode mode = dwb.state.mode;
    gboolean ret = false;
    gboolean complete = (mode == DOWNLOAD_GET_PATH || (mode & COMPLETE_PATH));
    gboolean set_text = false;

    if (dwb.state.mode & QUICK_MARK_OPEN) 
        set_text = true;
    /*  Handled by activate-callback */
    if (IS_RETURN_KEY(e))
        return dwb_entry_activate(e);
    /* Insert primary selection on shift-insert */
    if (mode == QUICK_MARK_SAVE) 
        return false;
    else if (mode & COMPLETE_BUFFER) 
    {
        completion_buffer_key_press(e);
        return true;
    }
    else if (mode & COMPLETE_SCRIPTS && !DWB_COMPLETE_KEY(e)) 
        if (dwb.state.script_comp_readonly)
            return true;
        else 
            goto skip;
    else if (e->keyval == GDK_KEY_BackSpace && !complete) 
        return false;
    else if (mode == HINT_MODE) 
        return dwb_update_hints(e);
    else if (mode == SEARCH_FIELD_MODE) 
    {
        if (DWB_TAB_KEY(e)) 
        {
            dwb_update_hints(e);
            return true;
        }
    }
    else if (!e->is_modifier && complete) 
    {
        if (DWB_TAB_KEY(e)) 
        {
            completion_complete_path(e->state & GDK_SHIFT_MASK);
            return true;
        }
        else 
            completion_clean_path_completion();
    }
    else if (mode & COMPLETION_MODE && !DWB_COMPLETE_KEY(e) && !e->is_modifier && !CLEAN_STATE(e)) 
        completion_clean_completion(set_text);
    else if (mode == FIND_MODE) 
    {
        goto skip;
    }
    else if (DWB_COMPLETE_KEY(e)) 
    {
        completion_complete(dwb_eval_completion_type(), e->state & GDK_SHIFT_MASK || e->keyval == GDK_KEY_Up);
        return true;
    }
skip:
    if (dwb_eval_override_key(e, CP_OVERRIDE_ENTRY)) 
        ret = true;
    return ret;
}/*}}}*/

/* dwb_delete_event_cb {{{*/
gboolean
callback_delete_event(GtkWidget *w) 
{
    dwb_end(0);
    return true;
}/*}}}*/

/* dwb_key_press_cb(GtkWidget *w, GdkEventKey *e, View *v) {{{*/
gboolean 
callback_key_press(GtkWidget *w, GdkEventKey *e) 
{
    gboolean ret = false;
    Mode mode = CLEAN_MODE(dwb.state.mode);

    if (EMIT_SCRIPT(KEY_PRESS)) 
    {
        /**
         * Emitted when a key was pressed
         * @event  keyPress
         * @memberOf signals
         * @param {signals~onKeyPress} callback 
         *      Callback function that will be called when the signal is emitted
         *
         * */
        /**
         * Callback called whenever a key is pressed 
         * @memberOf signals
         * @callback signals~onKeyPress
         *
         * @param {WebKitWebView} webview The currently focused webview
         * @param {Object}   event              
         *      The event
         * @param {Boolean} event.isModifier   
         *      Whether the key is a modifier
         * @param {Number}  event.keyCode      
         *      The hardware keycode
         * @param {Number}  event.keyVal       
         *      The keycode as listed in gdkkeysyms.h
         * @param {String}  event.name         
         *      A string representation of the key
         * @param {Modifier}  event.state      
         *      A bitmask of {@link Enums and Flags.Modifier|Modifier} pressed
         *
         * @returns {Boolean}
         *      Return true to stop emission of the signal, will also prevent
         *      dwb from handling the signal.
         *
         * */
        char *json = util_create_json(5, UINTEGER, "state", e->state, 
                UINTEGER, "keyVal", e->keyval, UINTEGER, "keyCode", e->hardware_keycode,
                BOOLEAN, "isModifier", e->is_modifier, CHAR, "name", gdk_keyval_name(e->keyval));
        ScriptSignal signal = { SCRIPTS_WV(dwb.state.fview), SCRIPTS_SIG_META(json, KEY_PRESS, 0) };
        SCRIPTS_EMIT_RETURN(signal, json, true);
    }

    if (entry_snooping())
    {
        return false;
    }
    if (mode == CARET_MODE)
        return visual_caret(e);
    else if (e->keyval == GDK_KEY_Escape) 
    {
        if (dwb.state.mode & COMPLETION_MODE) 
            completion_clean_completion(true);
        else 
            dwb_change_mode(NORMAL_MODE, true);
        ret = false;
    }
    else if(e->state == GDK_SHIFT_MASK && e->keyval == GDK_KEY_Insert) 
    {
        dwb_paste_primary();
        return true;
    }
    else if (dwb_eval_override_key(e, CP_OVERRIDE_ALL)) 
        ret = true;
    else if (mode & INSERT_MODE) 
        ret = dwb_eval_override_key(e, CP_OVERRIDE_INSERT);
    else if (gtk_widget_has_focus(dwb.gui.entry) || mode & COMPLETION_MODE) 
        ret = false;
    else if (webkit_web_view_has_selection(CURRENT_WEBVIEW()) && IS_RETURN_KEY(e)) 
        dwb_follow_selection(e);
    else if (dwb.state.mode & AUTO_COMPLETE && DWB_TAB_KEY(e)) 
    {
        completion_autocomplete(dwb.keymap, e);
        ret = true;
    }
    else if (dwb.state.mode == MARK_GET || dwb.state.mode == MARK_SET)
    {
        dwb_mark(e);
    }
    else 
    {
        if (mode & AUTO_COMPLETE) 
        {
            if (DWB_TAB_KEY(e)) 
                completion_autocomplete(NULL, e);
            else if (IS_RETURN_KEY(e)) 
            {
                completion_eval_autocompletion();
                return true;
            }
        }
        ret = dwb_eval_key(e);
    }
    return ret;
}/*}}}*/

/* dwb_key_release_cb {{{*/
gboolean 
callback_key_release(GtkWidget *w, GdkEventKey *e) 
{
    if (EMIT_SCRIPT(KEY_RELEASE)) 
    {
        /**
         * Emitted when a key was released
         * @event  keyRelease
         * @memberOf signals
         * @param {signals~onKeyRelease} callback 
         *      Callback function that will be called when the signal is emitted
         *
         * */
        /**
         * Callback called whenever a key is pressed 
         * @callback signals~onKeyRelease
         *
         * @param {WebKitWebView} webview The currently focused webview
         * @param {Object}   event              The event
         * @param {Boolean}  event.isModifier   Whether the key is a modifier
         * @param {Number}  event.keyCode      The hardware keycode
         * @param {Number}  event.keyVal       The keycode as listed in gdkkeysyms.h
         * @param {String}  event.name         A string representation of the
         *                                     key
         * @param {Modifier}  event.state      
         *      A bitmask of {@link Enums and Flags.Modifier|Modifier} pressed
         * @returns {Boolean}
         *      Return true to stop emission of the signal, will also prevent
         *      dwb from handling the signal.
         *
         * */
        char *json = util_create_json(5, UINTEGER, "state", e->state, 
                UINTEGER, "keyVal", e->keyval, UINTEGER, "keyCode", e->hardware_keycode,
                BOOLEAN, "isModifier", e->is_modifier, CHAR, "name", gdk_keyval_name(e->keyval));
        ScriptSignal signal = { SCRIPTS_WV(dwb.state.fview), SCRIPTS_SIG_META(json, KEY_RELEASE, 0) };
        SCRIPTS_EMIT_RETURN(signal, json, true);
    }
    if (DWB_TAB_KEY(e)) 
        return true;

    return false;
}/*}}}*/

#ifdef WITH_LIBSOUP_2_38
void 
callback_dns_resolve(SoupAddress *address, guint status, GList *gl) 
{
    char *uri = NULL;
    View *v = VIEW(gl);
    if (status == SOUP_STATUS_OK) 
        uri = g_strconcat("http://", v->status->request_uri, NULL);
    else 
        uri = dwb_get_searchengine(v->status->request_uri);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(v->web), uri);
    g_free(uri);
    g_free(v->status->request_uri);
    v->status->request_uri = NULL;
}
#ifndef  _HAS_GTK3
void 
callback_tab_container_heigth(GtkWidget *tabcontainer, GdkRectangle *rect, gpointer data)
{
    GtkAllocation a;
    if (dwb.state.views)
    {
        gtk_widget_get_allocation(VIEW(dwb.state.views)->tabevent, &a);
        if (rect->height > 0 && a.height > 0)
        {
            // also add 1px padding
            dwb_limit_tabs(rect->height / (a.height+1));
        }
    }
}
#endif
#endif
