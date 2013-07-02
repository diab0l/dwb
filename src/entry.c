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
#include "entry.h"
#include "callback.h"
static char *s_store;
static GList *s_filterlist;
static gboolean s_snoop = false;
/* dwb_entry_history_forward {{{*/
DwbStatus
entry_history_forward(GList **last) 
{
    if (s_filterlist == NULL)
        return STATUS_ERROR;

    const char *text = NULL;
    GList *prev = NULL;
    if (*last != NULL) 
    {
        if ((*last)->prev == NULL) 
        {
            text = s_store;
            GLIST_FREE0(s_filterlist);
        }
        else 
        {
            prev = (*last)->prev;
            text = prev->data;
        }
    }
    *last = prev;
    if (text != NULL) 
        entry_set_text(text);

    return STATUS_OK;
}/*}}}*/

/* entry_history_back(GList **list, GList **last) {{{ */
DwbStatus
entry_history_back(GList **list, GList **last) 
{
    char *text = NULL;
    if (*list == NULL)
        return STATUS_ERROR;

    GList *next;
    if (*last == NULL) 
    {
        const char *text = GET_TEXT();
        if (text && *text) {
            for (GList *l = *list; l; l=l->next)
            {
                if (strstr(l->data, text)) {
                    s_filterlist = g_list_prepend(s_filterlist, l->data);
                }
            }
            if (s_filterlist)
                s_filterlist = g_list_reverse(s_filterlist);
        }
        else 
            s_filterlist = g_list_copy(*list);

        next = s_filterlist;
        s_store = g_strdup(text);
    }
    else if ((*last)->next != NULL)
        next = (*last)->next;
    else 
        return STATUS_OK;

    *last = next;
    if (next) 
        text = next->data;
    if (text && *text) 
    {
        entry_set_text(text);
    }
    return STATUS_OK;
} /* }}} */

/* entry_focus() {{{*/
void 
entry_focus() 
{
    if (! (dwb.state.bar_visible & BAR_VIS_STATUS)) 
        gtk_widget_show_all(dwb.gui.bottombox);

    gtk_widget_show(dwb.gui.entry);
    gtk_widget_grab_focus(dwb.gui.entry);
    gtk_widget_set_can_focus(CURRENT_WEBVIEW_WIDGET(), false);
    gtk_editable_delete_text(GTK_EDITABLE(dwb.gui.entry), 0, -1);
}/*}}}*/

void 
entry_clear_history()
{
    dwb.state.last_com_history = NULL;
    dwb.state.last_nav_history = NULL;
    dwb.state.last_find_history = NULL;
    FREE0(s_store);
    GLIST_FREE0(s_filterlist);
}
void
entry_hide()
{
    gtk_widget_hide(dwb.gui.entry);
    entry_clear_history();
}

/* entry_insert_text(const char *) {{{*/
void 
entry_insert_text(const char *text) 
{
    int position = gtk_editable_get_position(GTK_EDITABLE(dwb.gui.entry));
    gtk_editable_insert_text(GTK_EDITABLE(dwb.gui.entry), text, -1, &position);
    gtk_editable_set_position(GTK_EDITABLE(dwb.gui.entry), position);
}/*}}}*/
/* entry_set_text(const char *) {{{*/
void 
entry_set_text(const char *text) 
{
    gtk_entry_set_text(GTK_ENTRY(dwb.gui.entry), text);
    gtk_editable_set_position(GTK_EDITABLE(dwb.gui.entry), -1);
}/*}}}*/

/* entry_move_cursor_step(GtkMovementStep, int step, gboolean delete)  {{{*/
void 
entry_move_cursor_step(GtkMovementStep step, int stepcount, gboolean del) 
{
    g_signal_emit_by_name(dwb.gui.entry, "move-cursor", step, stepcount, del);
    if (del)
        gtk_editable_delete_selection(GTK_EDITABLE(dwb.gui.entry));
}/*}}}*/

void 
entry_snoop(GCallback callback, gpointer data)
{
    s_snoop = true;
    entry_focus();
    g_signal_handlers_block_by_func(dwb.gui.entry, callback_entry_insert_text, NULL);
    g_signal_handlers_block_by_func(dwb.gui.entry, callback_entry_key_press, NULL);
    g_signal_handlers_block_by_func(dwb.gui.entry, callback_entry_key_release, NULL);
    g_signal_connect(dwb.gui.entry, "key-press-event", callback, data);
}
void 
entry_snoop_end(GCallback callback, gpointer data)
{
    s_snoop = false;
    g_signal_handlers_unblock_by_func(dwb.gui.entry, callback_entry_insert_text, NULL);
    g_signal_handlers_unblock_by_func(dwb.gui.entry, callback_entry_key_press, NULL);
    g_signal_handlers_unblock_by_func(dwb.gui.entry, callback_entry_key_release, NULL);
    g_signal_handlers_disconnect_by_func(dwb.gui.entry, callback, data);
    dwb_change_mode(NORMAL_MODE, true);
}
gboolean
entry_snooping()
{
    return s_snoop;
}
void 
entry_clear(gboolean visibility)
{
    gtk_editable_delete_text(GTK_EDITABLE(dwb.gui.entry), 0, -1);
    gtk_entry_set_visibility(GTK_ENTRY(dwb.gui.entry), visibility);
}
