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
#include "ipc.h"
#include "soup.h"
#include <dwbrc.h>
#include <string.h>
#include <stdlib.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static Atom s_atoms[6];
static GdkAtom s_readatom;
static Display *s_dpy;
static Window s_win;

enum {
    DWB_ATOM_READ = 0, 
    DWB_ATOM_WRITE,
    DWB_ATOM_STATUS,
    DWB_ATOM_HOOK,
    DWB_ATOM_BIND,
    UTF8_STRING, 
};
guint id;

static long 
get_number(const char *text)
{
    char *endptr;
    long n;
    n = strtol(text, &endptr, 10);
    if (*endptr == '\0')
        return n;
    return -1;
}

static int 
get_hooks(char **list, int count)
{
    int hooks = 0;
    const struct {
        int hook;
        const char *name;
    } hook_mapping[] = {
        { IPC_HOOK_HOOK, "hook" }, 
        { IPC_HOOK_NAVIGATION, "navigation" }, 
        { IPC_HOOK_LOAD_FINISHED, "load_finished" }, 
        { IPC_HOOK_LOAD_COMMITTED, "load_committed" }, 
        { IPC_HOOK_CLOSE_TAB, "close_tab" }, 
        { IPC_HOOK_NEW_TAB, "new_tab" }, 
        { IPC_HOOK_FOCUS_TAB, "focus_tab" }, 
        { 0, 0 }
    };
    for (int i=0; hook_mapping[i].hook != 0; i++)
    {
        for (int j=0; j<count; j++)
        {
            if (STREQ(list[j], hook_mapping[i].name))
            {
                hooks |= hook_mapping[i].hook;
            }
        }
    }
    return hooks;
}

static void 
send_hook_list(const char *action, char **list, int count)
{
    GString *response = g_string_new(action);
    for (int i=0; i<count; i++)
        g_string_append_printf(response, " %s", list[i]);
    ipc_send_hook("hook", response->str);
    g_string_free(response, true);
}
static DwbStatus 
bind_callback(KeyMap *map, Arg *a)
{
    char *data = g_strdup_printf("%d %s", g_list_position(dwb.state.views, dwb.state.fview), CURRENT_URL());
    char *argv[2] = { a->arg, data };
    dwbrc_set_property_list(s_dpy, s_win, s_atoms[DWB_ATOM_BIND], argv, 2);
    return STATUS_OK;
}

static int 
parse_commands(char **list, int count)
{
    int status = 0;
    char *text = NULL; 
    if (count < 2)
    {
        return 37;
    }
    if (STREQ(list[0], "execute"))
    {
        char *nlist[count];
        char *command; 

        for (int i=0; i<count-1; i++)
            nlist[i] = list[i+1];
        nlist[count-1] = NULL;

        command = g_strjoinv(" ", nlist);
        status = dwb_parse_command_line(command);
        g_free(command);
    }
    else if (STREQ(list[0], "prompt"))
    {
        text = dwb_prompt(true, list[1]);
    }
    else if (STREQ(list[0], "pwd_prompt"))
    {
        text = dwb_prompt(false, list[1]);
    }
    else if (STREQ(list[0], "setting"))
    {
        WebSettings *s = g_hash_table_lookup(dwb.settings, list[1]);
        if (s == NULL) 
            dwbrc_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "not found");
        else 
        {
            switch (s->type) 
            {
                case INTEGER : 
                    dwbrc_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "%d", s->arg_local.i);
                    break;
                case DOUBLE : 
                    dwbrc_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "%.2f", s->arg_local.d);
                    break;
                case BOOLEAN : 
                    dwbrc_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], s->arg_local.b ? "true" : "false");
                    break;
                case CHAR : 
                    dwbrc_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], s->arg_local.p);
                    break;
                default : break;
            }
        }
    }
    else if (STREQ(list[0], "get") && count > 1)
    {
        GList *l = dwb.state.fview;
        int n;
        int argc = 1;
        if (count == 3)
        {
            if ((n = get_number(list[argc++])) != -1)
            {
                l = g_list_nth(dwb.state.views, n - 1);
                if (l == NULL)
                {
                    return -1;
                }
            }
        }
        if (STREQ(list[argc], "uri"))
        {
            text = g_strdup(webkit_web_view_get_uri(WEBVIEW(l)));
        }
        else if (STREQ(list[argc], "domain"))
        {
            const char *domain = dwb_soup_get_domain(webkit_web_view_get_main_frame(WEBVIEW(l)));
            text = g_strdup(domain != NULL ? domain : "null");

        }
        else if (STREQ(list[argc], "host"))
        {
            const char *host = dwb_soup_get_host(webkit_web_view_get_main_frame(WEBVIEW(l)));
            text = g_strdup(host != NULL ? host : "null");

        }
        else if (STREQ(list[argc], "title"))
        {
            text = g_strdup(webkit_web_view_get_title(WEBVIEW(l)));
        }
        else if (STREQ(list[argc], "ntabs"))
        {
            text = g_strdup_printf("%d", g_list_length(dwb.state.views));
        }
        else if (STREQ(list[argc], "all_uris"))
        {
            GString *s = g_string_new(NULL);
            for (GList *l = dwb.state.views; l; l=l->next)
                g_string_append_printf(s, "%s%s", l == dwb.state.views ? "" : "\n", webkit_web_view_get_uri(WEBVIEW(l)));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_titles"))
        {
            GString *s = g_string_new(NULL);
            for (GList *l = dwb.state.views; l; l=l->next)
                g_string_append_printf(s, "%s%s", l == dwb.state.views ? "" : "\n", webkit_web_view_get_title(WEBVIEW(l)));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_hosts"))
        {
            GString *s = g_string_new(NULL);
            for (GList *l = dwb.state.views; l; l=l->next)
                g_string_append_printf(s, "%s%s\n", l == dwb.state.views ? "" : "\n", dwb_soup_get_host(webkit_web_view_get_main_frame(WEBVIEW(l))));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_domains"))
        {
            GString *s = g_string_new(NULL);
            for (GList *l = dwb.state.views; l; l=l->next)
                g_string_append_printf(s, "%s%s\n", l == dwb.state.views ? "" : "\n", dwb_soup_get_domain(webkit_web_view_get_main_frame(WEBVIEW(l))));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "current_tab"))
        {
            dwbrc_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE],
                    "%d", g_list_position(dwb.state.views, dwb.state.fview) + 1);
        }
    }
    else if (STREQ(list[0], "clear_hooks"))
    {
        if (dwb.state.ipc_hooks & IPC_HOOK_HOOK)
            send_hook_list("clear", &list[1], count-1);
        dwb.state.ipc_hooks &= ~get_hooks(&list[1], count-1);
    }
    else if (STREQ(list[0], "hook") || STREQ(list[0], "add_hooks"))
    {
        if (dwb.state.ipc_hooks & IPC_HOOK_HOOK)
            send_hook_list("add", &list[1], count-1);
        dwb.state.ipc_hooks |= get_hooks(&list[1], count-1);
    }
    else if (STREQ(list[0], "bind"))
    {
        char *com, *shortcut;
        int options = 0;
        for (int i=1; i<count; i++)
        {
            char **binds = g_strsplit(list[i], ":", -1);
            if (g_strv_length(binds) != 2)
                continue;
            else 
            {
                if (STREQ(binds[0], "none"))
                    com = NULL;
                else 
                {
                    com = g_strdup(binds[0]);
                    options |= CP_COMMANDLINE;
                }
                if (STREQ(binds[1], "none"))
                    shortcut = NULL;
                else 
                    shortcut = g_strdup(binds[1]);
                Arg a = { .arg = g_strdup(list[i]) };
                dwb_add_key(shortcut, com, g_strdup("dwbrc"), (Func)bind_callback, options, &a);
            }

        }
    }
    else 
    {
        return 37;
    }
    if (text != NULL)
    {
        dwbrc_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], text);
        g_free(text);
    }
    return status;
}

static gboolean 
on_property_notify(GtkWidget *widget, GdkEventProperty *e, gpointer data)
{
    static int status, count;
    char **list;

    if (e->state == GDK_PROPERTY_NEW_VALUE && e->atom == s_readatom)
    {
        if (!dwbrc_get_property(s_dpy, s_win, s_atoms[DWB_ATOM_READ],  &list, &count))
        {
            return false;
        }

        status = parse_commands(list, count);

        XDeleteProperty(s_dpy, s_win, s_atoms[DWB_ATOM_READ]);
        dwbrc_set_status(s_dpy, s_win, status);
        //XChangeProperty(s_dpy, s_win, s_atoms[DWB_ATOM_STATUS], XA_INTEGER, 32, PropModeReplace, (unsigned char*)&status, 1);

        XFreeStringList(list);

        return true;
    }
    return false;
}
void 
ipc_send_hook(char *name, const char *format, ...)
{
    va_list arg_list; 
    char buffer[1024];
    if (format != NULL)
    {
        va_start(arg_list, format);
        vsnprintf(buffer, sizeof(buffer), format, arg_list);
        va_end(arg_list);
        char *argv[] = { name, buffer };
        dwbrc_set_property_list(s_dpy, s_win, s_atoms[DWB_ATOM_HOOK], argv, 2);
    }
    else 
        dwbrc_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_HOOK], name);
}
void 
ipc_start(GtkWidget *widget)
{
    dwb.state.ipc_hooks = 0;

    GdkWindow *gdkwin = gtk_widget_get_window(widget);

    s_dpy = gdk_x11_get_default_xdisplay();
    s_win = GDK_WINDOW_XID(gdkwin);

    dwbrc_set_status(s_dpy, s_win, 0);

    GdkEventMask mask = gdk_window_get_events(gdkwin);
    gdk_window_set_events(gdkwin, mask | GDK_PROPERTY_CHANGE_MASK);

    s_atoms[DWB_ATOM_READ] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_READ, false);
    s_atoms[DWB_ATOM_WRITE] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_WRITE, false);
    s_atoms[DWB_ATOM_STATUS] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_STATUS, false);
    s_atoms[DWB_ATOM_HOOK] = XInternAtom(s_dpy, DWB_ATOM_IPC_HOOK, false);
    s_atoms[DWB_ATOM_BIND] = XInternAtom(s_dpy, DWB_ATOM_IPC_BIND, false);
    s_atoms[UTF8_STRING] = XInternAtom(s_dpy, "UTF8_STRING", false);

    s_readatom = gdk_atom_intern(DWB_ATOM_IPC_SERVER_READ, false);

    g_signal_connect(G_OBJECT(widget), "property-notify-event", G_CALLBACK(on_property_notify), NULL);
}
