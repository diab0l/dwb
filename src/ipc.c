/*
 * Copyright (c) 2013-2014 Stefan Bolte <portix@gmx.net>
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
#include "session.h"
#include <dwbremote.h>
#include "soup.h"
#include <string.h>
#include <stdlib.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#define IPC_EXECUTE(list) do { char *_ex_command = g_strjoinv(" ", list); dwb_parse_command_line(_ex_command); g_free(_ex_command); } while(0)

#define OPTNL(cond) ((cond) ? "" : "\n")

enum {
    DWB_ATOM_READ = 0, 
    DWB_ATOM_WRITE,
    DWB_ATOM_HOOK,
    DWB_ATOM_BIND,
    DWB_ATOM_STATUS,
    DWB_ATOM_FOCUS_ID,
    UTF8_STRING, 
    DWB_ATOM_LAST,
};

static Atom s_atoms[DWB_ATOM_LAST];
static GdkAtom s_readatom;
static Display *s_dpy;
static Window s_win;
static gulong s_sig_property;
static gulong s_sig_focus;
static Window s_root; 

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
#define HOOK_MAP(hook) { IPC_HOOK_##hook, #hook }
    int hooks = 0;
    static const struct {
        int hook;
        const char *name;
    } hook_mapping[] = {
        HOOK_MAP(hook), 
        HOOK_MAP(navigation), 
        HOOK_MAP(load_finished), 
        HOOK_MAP(load_committed), 
        HOOK_MAP(close_tab), 
        HOOK_MAP(new_tab), 
        HOOK_MAP(focus_tab), 
        HOOK_MAP(execute), 
        HOOK_MAP(change_mode),
        HOOK_MAP(download_finished),
        HOOK_MAP(document_finished),
        { 0, 0 }
    };
#undef HOOK_MAP
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
    dwbremote_set_property_list(s_dpy, s_win, s_atoms[DWB_ATOM_BIND], argv, 2);
    return STATUS_OK;
}

static int 
parse_commands(char **list, int count)
{
    int status = 0;
    char *text = NULL; 
    if (count < 1 || (*list[0] != ':' && count < 2))
        return 37;
    if (*list[0] == ':')
    {
        char *nlist[count + 1];

        nlist[0] = &(list[0][1]);
        for (int i=1; i<count; i++)
            nlist[i] = list[i];
        nlist[count] = NULL;

        IPC_EXECUTE(nlist);
    }
    else if (STREQ(list[0], "execute"))
    {
        char *nlist[count];

        for (int i=0; i<count-1; i++)
            nlist[i] = list[i+1];
        nlist[count-1] = NULL;

        IPC_EXECUTE(nlist);
    }
    else if (STREQ(list[0], "prompt"))
    {
        text = dwb_prompt(true, list[1]);
    }
    else if (STREQ(list[0], "pwd_prompt"))
    {
        text = dwb_prompt(false, list[1]);
    }
    else if (STREQ(list[0], "confirm"))
    {
        gboolean confirm = dwb_confirm(dwb.state.fview, list[1]);
        CLEAR_COMMAND_TEXT();
        text = g_strdup(confirm ? "true" : "false");
    }
    else if (STREQ(list[0], "get") && count > 1)
    {
        GList *l = dwb.state.fview;
        int n;
        int argc = 1;
        if (count == 3)
        {
            if ((n = get_number(list[argc])) != -1)
            {
                argc++;
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
            int i=1;
            for (GList *l = dwb.state.views; l; l=l->next, i++)
            {
                const char *uri = VIEW(l)->status->deferred && VIEW(l)->status->deferred_uri ? VIEW(l)->status->deferred_uri : webkit_web_view_get_uri(WEBVIEW(l));
                g_string_append_printf(s, "%s%d %s", OPTNL(l == dwb.state.views), i, uri);
            }
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_titles"))
        {
            GString *s = g_string_new(NULL);
            int i=1;
            for (GList *l = dwb.state.views; l; l=l->next, i++)
                g_string_append_printf(s, "%s%d %s", OPTNL(l == dwb.state.views), i, webkit_web_view_get_title(WEBVIEW(l)));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_hosts"))
        {
            GString *s = g_string_new(NULL);
            int i=1;
            for (GList *l = dwb.state.views; l; l=l->next, i++)
                g_string_append_printf(s, "%s%d %s", OPTNL(l == dwb.state.views), i, dwb_soup_get_host(webkit_web_view_get_main_frame(WEBVIEW(l))));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "all_domains"))
        {
            GString *s = g_string_new(NULL);
            int i=1;
            for (GList *l = dwb.state.views; l; l=l->next, i++)
                g_string_append_printf(s, "%s%d %s", OPTNL(l == dwb.state.views), i, dwb_soup_get_domain(webkit_web_view_get_main_frame(WEBVIEW(l))));
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "current_tab"))
        {
            dwbremote_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE],
                    "%d", g_list_position(dwb.state.views, dwb.state.fview) + 1);
        }
        else if (STREQ(list[argc], "history"))
        {
            GString *s = g_string_new(NULL);
            WebKitWebBackForwardList *bf_list = webkit_web_view_get_back_forward_list(WEBVIEW(l));
            int blength = webkit_web_back_forward_list_get_back_length(bf_list);
            int flength = webkit_web_back_forward_list_get_forward_length(bf_list);
            for (int i=-blength; i<=flength; i++)
            {
                WebKitWebHistoryItem *item = webkit_web_back_forward_list_get_nth_item(bf_list, i);
                g_string_append_printf(s, "%s%d %s", OPTNL(i == -blength), i, webkit_web_history_item_get_uri(item));
            }
            text = s->str;
            g_string_free(s, false);
        }
        else if (STREQ(list[argc], "profile"))
        {
            text = g_strdup(dwb.misc.profile);
        }
        else if (STREQ(list[argc], "session"))
        {
            text = g_strdup(session_get_name());
        }
        else if (STREQ(list[argc], "setting") && count > 2)
        {
            WebSettings *s = g_hash_table_lookup(dwb.settings, list[argc+1]);
            if (s == NULL) 
                dwbremote_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "(not found)");
            else 
            {
                switch (s->type) 
                {
                    case INTEGER : 
                        dwbremote_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "%d", s->arg_local.i);
                        break;
                    case DOUBLE : 
                        dwbremote_set_formatted_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], "%.2f", s->arg_local.d);
                        break;
                    case BOOLEAN : 
                        dwbremote_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], s->arg_local.b ? "true" : "false");
                        break;
                    case CHAR : 
                    case COLOR_CHAR : 
                        dwbremote_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], s->arg_local.p ? s->arg_local.p : "(none)");
                        break;
                    default : break;
                }
            }
        }
        else 
        {
            return 137;
        }
    }
    else if (STREQ(list[0], "clear_hooks"))
    {
        if (dwb.state.ipc_hooks & IPC_HOOK_hook)
            send_hook_list("clear", &list[1], count-1);
        dwb.state.ipc_hooks &= ~get_hooks(&list[1], count-1);
    }
    else if (STREQ(list[0], "hook") || STREQ(list[0], "add_hooks"))
    {
        if (dwb.state.ipc_hooks & IPC_HOOK_hook)
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
                dwb_add_key(shortcut, com, g_strdup("dwbremote"), (Func)bind_callback, options, &a);
            }

        }
    }
    else 
    {
        return 37;
    }
    if (text != NULL)
    {
        dwbremote_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_WRITE], text);
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
        if (!dwbremote_get_property(s_dpy, s_win, s_atoms[DWB_ATOM_READ],  &list, &count))
        {
            return false;
        }

        status = parse_commands(list, count);

        XDeleteProperty(s_dpy, s_win, s_atoms[DWB_ATOM_READ]);
        dwbremote_set_int_property(s_dpy, s_win, s_atoms[DWB_ATOM_STATUS], status);

        XFreeStringList(list);

        return true;
    }
    return false;
}
static int 
xerror_handler(Display *dpy, XErrorEvent *event)
{
    return 0;
}

static gboolean 
on_focus_in(GtkWidget *widget, GdkEventFocus *e, gpointer data)
{
    static int last_id = 0;
    Window win;
    // Dirty: If focus-in-event is a result of a destroyed window XQueryTree will also
    // return the already destroyed window (strangely the destroy event is
    // emitted after the focus event) which results in a BadWindow Error.
    // We ignore the error, otherwise gtk3 will crash.
    XSetErrorHandler(xerror_handler);
    int new_id = dwbremote_get_last_focus_id(s_dpy, s_root, s_atoms[DWB_ATOM_FOCUS_ID], &win);
    XSetErrorHandler(NULL);
    if (new_id > last_id)
    {
        last_id = (++new_id);
        dwbremote_set_int_property(s_dpy, s_win, s_atoms[DWB_ATOM_FOCUS_ID], new_id);
    }
    return false;
}

void 
ipc_send_hook(char *name, const char *format, ...)
{
    va_list arg_list; 
    char buffer[4096];
    if (format != NULL)
    {
        va_start(arg_list, format);
        vsnprintf(buffer, sizeof(buffer), format, arg_list);
        va_end(arg_list);
        char *argv[] = { name, buffer };
        dwbremote_set_property_list(s_dpy, s_win, s_atoms[DWB_ATOM_HOOK], argv, 2);
    }
    else 
        dwbremote_set_property_value(s_dpy, s_win, s_atoms[DWB_ATOM_HOOK], name);
}
void 
ipc_end(GtkWidget *widget)
{
    if (s_sig_property != 0)
    {
        g_signal_handler_disconnect(widget, s_sig_property);
        XDeleteProperty(s_dpy, s_win, s_atoms[DWB_ATOM_STATUS]);
        s_sig_property = 0;
    }
    if (s_sig_focus != 0)
    {
        g_signal_handler_disconnect(widget, s_sig_focus);
        XDeleteProperty(s_dpy, s_win, s_atoms[DWB_ATOM_FOCUS_ID]);
        s_sig_focus = 0;
    }
}
void 
ipc_start(GtkWidget *widget)
{
    dwb.state.ipc_hooks = 0;

    GdkWindow *gdkwin = gtk_widget_get_window(widget);

    s_dpy = gdk_x11_get_default_xdisplay();
    s_root = RootWindow(s_dpy, DefaultScreen(s_dpy));
    s_win = GDK_WINDOW_XID(gdkwin);

    GdkEventMask mask = gdk_window_get_events(gdkwin);
    gdk_window_set_events(gdkwin, mask | GDK_PROPERTY_CHANGE_MASK | GDK_FOCUS_CHANGE_MASK);

    s_atoms[DWB_ATOM_READ] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_READ, false);
    s_atoms[DWB_ATOM_WRITE] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_WRITE, false);
    s_atoms[DWB_ATOM_HOOK] = XInternAtom(s_dpy, DWB_ATOM_IPC_HOOK, false);
    s_atoms[DWB_ATOM_BIND] = XInternAtom(s_dpy, DWB_ATOM_IPC_BIND, false);
    s_atoms[DWB_ATOM_STATUS] = XInternAtom(s_dpy, DWB_ATOM_IPC_SERVER_STATUS, false);
    s_atoms[DWB_ATOM_FOCUS_ID] = XInternAtom(s_dpy, DWB_ATOM_IPC_FOCUS_ID, false);
    s_atoms[UTF8_STRING] = XInternAtom(s_dpy, "UTF8_STRING", false);

    dwbremote_set_int_property(s_dpy, s_win, s_atoms[DWB_ATOM_STATUS], 0);

    Window win;
    int new_id = dwbremote_get_last_focus_id(s_dpy, s_root, s_atoms[DWB_ATOM_FOCUS_ID], &win);
    dwbremote_set_int_property(s_dpy, s_win, s_atoms[DWB_ATOM_FOCUS_ID], new_id + 1);

    s_readatom = gdk_atom_intern(DWB_ATOM_IPC_SERVER_READ, false);

    s_sig_property = g_signal_connect(G_OBJECT(widget), "property-notify-event", G_CALLBACK(on_property_notify), NULL);
    s_sig_focus = g_signal_connect(G_OBJECT(widget), "focus-in-event", G_CALLBACK(on_focus_in), NULL);
}
