/*
 * Copyright (c) 2013 Stefan Bolte <portix@gmx.net>
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

#include "dwb.h"
#include "js.h"

static void 
dispatch_event(guint keyval[3], guint state[3])
{
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = g_object_ref(gtk_widget_get_window(VIEW(dwb.state.fview)->web));
    for (int i=0; i<3 && keyval[i] != 0; i++)
    {
        event->key.keyval = keyval[i];
        event->key.state = state[i];
        GdkKeymapKey *key; 
        gint n;
        if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_default(), keyval[i], &key, &n))
        {
            event->key.hardware_keycode = key[0].keycode;
            g_free(key);
        }
        gtk_main_do_event(event);
    }
    gdk_event_free(event);
}

gboolean 
visual_caret(GdkEventKey *e)
{
    static gint visual = false;

    guint defaultMask = visual ? GDK_SHIFT_MASK : 0;

    guint keyval[3] = { 0, 0, 0 };
    guint state[3] = { defaultMask, defaultMask, defaultMask };

    switch (e->keyval)
    {
        case GDK_KEY_j: 
            keyval[0] = GDK_KEY_Down;
            break;
        case GDK_KEY_k: 
            keyval[0] = GDK_KEY_Up;
            break;
        case GDK_KEY_h: 
            keyval[0] = GDK_KEY_Left;
            break;
        case GDK_KEY_l: 
            keyval[0] = GDK_KEY_Right;
            break;
        case GDK_KEY_w: 
            keyval[0] = keyval[1] = GDK_KEY_Right;
            keyval[2] = GDK_KEY_Left;
            state[0] = state[1] = state[2] = GDK_CONTROL_MASK | defaultMask;
            break;
        case GDK_KEY_b: 
            keyval[0] = GDK_KEY_Left;
            state[0] = GDK_CONTROL_MASK | defaultMask;
            break;
        case GDK_KEY_e: 
            keyval[0] = GDK_KEY_Right;
            state[0] = GDK_CONTROL_MASK | defaultMask;
            break;
        case GDK_KEY_0: 
            keyval[0] = GDK_KEY_Home;
            break;
        case GDK_KEY_dollar: 
            keyval[0] = GDK_KEY_End;
            break;
        case GDK_KEY_v: 
            visual = true;
            dwb_set_normal_message(dwb.state.fview, false, "-- VISUAL --");
            break;
        case GDK_KEY_y: 
            webkit_web_view_copy_clipboard(CURRENT_WEBVIEW());
            dwb_set_normal_message(dwb.state.fview, true, "Yanked to clipboard");
            dwb_change_mode(NORMAL_MODE, false); 
            visual = false;
            return true;
        case GDK_KEY_Escape: 
            if (visual)
            {
                dwb_set_normal_message(dwb.state.fview, false, "-- CARET --");
                visual = false;
            }
            else 
            {
                WebKitWebSettings *settings = webkit_web_view_get_settings(CURRENT_WEBVIEW());
                g_object_set(settings, "enable-caret-browsing", FALSE, NULL);
                dwb_change_mode(NORMAL_MODE, true);
            }
            break;
        default :return false;
    }
    dispatch_event(keyval, state);
    return true;
}
