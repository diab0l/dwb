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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include "dwbremote.h"
#define STREQ(x, y) (strcmp((x), (y)) == 0)
#define STRNEQ(x, y, n) (strncmp((x), (y), (n)) == 0)
#define CHECK_REMAINING(argc, argv) (argc > 2 || (argc == 2 && *argv[1] == ':'))
#define CHECK_ARG(arg, sopt, lopt) ((STREQ(sopt, arg) || STREQ(lopt, arg)))
#define PARSE_ARG(argc, argv, sopt, lopt) (CHECK_ARG(*argv, sopt, lopt) && CHECK_REMAINING(argc, argv))

static int s_opts;

enum {
    OPT_SHOW_WID = 1<<0, 
    OPT_SNOOP = 1<<1, 
};

static void 
help()
{
    printf( "USAGE: \n" 
            "   dwbremote [options] <command> [arguments]\n\n"
            "OPTIONS: \n"
            "   -a --all                Send command to all windows\n"
            "   -c --class <class>      Search for window id by WM_CLASS <class>\n"
            "   -i --id    <windowid>   Send commands to window with id <windowid>\n"
            "   -h --help               Show this help and exit\n"
            "   -l --list               List all dwb window ids\n"
            "   -n --name  <class>      Search for window id by WM_NAME <name>\n"
            "   -p --pid   <pid>        Send commands to instance with process id <pid>\n"
            "   -s --show-id            Print the window id in every response\n"
            "   -v --version            Print version information and exit\n");
}
static void
version() 
{
    printf("    This is : "NAME"\n"
           "    Version : "VERSION"\n"
           "      Built : "__DATE__" "__TIME__"\n"
           "  Copyright : "COPYRIGHT"\n"
           "    License : "LICENSE"\n");
    exit(0);
}

static Bool 
is_dwb_win(Display *dpy, Window win)
{
    Atom a;
    dwbremote_get_int_property(dpy, win, XInternAtom(dpy, DWB_ATOM_IPC_SERVER_STATUS, False), &a);
    return a != None;
}

static Bool 
cmp_pid(Display *dpy, Window win, void *data)
{
    pid_t orig_pid = *(pid_t *)data;
    pid_t g_pid = 0;
    
    Atom actual_atom; 
    int actual_format; 
    unsigned long n_items, bytes_after; 
    unsigned char *id;
    XGetWindowProperty(dpy, win, XInternAtom(dpy, "_NET_WM_PID", False), 0, 32, False, XA_CARDINAL, 
            &actual_atom, &actual_format, &n_items, &bytes_after, (unsigned char**)&id);
    if (id)
    {
        g_pid = *(pid_t *)id;
    }

    return g_pid == orig_pid;
}

static Bool 
cmp_class(Display *dpy, Window win, void *data)
{
    char *orig_class = (char *) data;
    XClassHint hint;
    int status = XGetClassHint(dpy, win, &hint);
    return status != 0 && hint.res_class && STREQ(hint.res_class, orig_class);
}

static Bool 
cmp_name(Display *dpy, Window win, void *data)
{
    char *orig_name = (char *) data;
    XClassHint hint;
    int status = XGetClassHint(dpy, win, &hint);
    return status != 0 && hint.res_class && STREQ(hint.res_name, orig_name);
}

static Bool 
cmp_winid(Display *dpy, Window win, void *data)
{
    (void) dpy;
    return win == *(Window *)data;
}

static Bool 
append_win(Window win, Window **win_return, int *n_ret)
{
    for (int i=0; i<*n_ret; i++)
    {
        if ((*win_return)[i] == win)
            return False;
    }

    *win_return = realloc(*win_return, (*n_ret + 1) * sizeof(Window));
    if (*win_return == NULL)
    {
        fprintf(stderr, "Cannot realloc %zu bytes!", (*n_ret+1) * sizeof(Window));
        exit(1);
    }
    (*win_return)[*n_ret] = win;
    (*n_ret)++;
    return True;
}

static void 
get_wins(Display *dpy, Window win, Window **win_return, int *n_ret, void *data, Bool (*cmp)(Display *, Window win, void *data), Bool stop_query)
{
    Window unused_win; 
    Window *children; 
    unsigned int n_children;

    if (XQueryTree(dpy, win, &unused_win, &unused_win, &children, &n_children) == 0)
        return;
    for (unsigned int i=0; i<n_children; i++)
    {
        if (is_dwb_win(dpy, children[i]) && (cmp == NULL || cmp(dpy, children[i], data)))
        {
            if (append_win(children[i], win_return, n_ret) && stop_query)
                break;
        }
        get_wins(dpy, children[i], win_return, n_ret, data, cmp, stop_query);
    }
}

static Bool 
consume_arg(const char *opt, const char *lopt, int *argc, char ***argv)
{
    if (CHECK_ARG(**argv, opt, lopt)) 
    {
        (*argv)++; 
        (*argc)--;
        return True;
    }
    return False;
}

static unsigned long 
parse_number(char *str)
{
    char *end; 
    int base = STRNEQ(str, "0x", 2) ?  16 : 10;
    unsigned long ret = strtoul(str, &end, base);
    if (*end != '\0')
    {
        fprintf(stderr, "Failed to parse number '%s'\n", str);
        return 0;
    }
    return ret;
}

static void 
send_command(Display *dpy, Window win, long event_mask, char **argv, int argc)
{
    dwbremote_set_property_list_by_name(dpy, win, DWB_ATOM_IPC_CLIENT_WRITE, argv, argc);
    XSelectInput(dpy, win, event_mask);
}
static int 
process_one(Display *dpy, Window win, Atom read_atom, char **argv, int argc)
{
    XEvent e;
    XPropertyEvent *pe;
    Atom atr;
    int status;
    char **list; 
    int count;

    send_command(dpy, win, PropertyChangeMask, argv, argc);
    while(1)
    {
        XNextEvent(dpy, &e);
        pe = &(e.xproperty);
        if (pe->atom == read_atom && dwbremote_get_property(dpy, pe->window, read_atom, &list, &count))
        {
            if (count > 0)
            {
                if (s_opts & OPT_SHOW_WID)
                    printf("%lu ", win);
                printf("%s\n", list[0]);
                XFreeStringList(list);
            }
            XDeleteProperty(dpy, pe->window, read_atom);
        }
        else if (pe->atom == XInternAtom(dpy, DWB_ATOM_IPC_SERVER_STATUS, False))
        {
            status = dwbremote_get_int_property(dpy, pe->window, XInternAtom(dpy, DWB_ATOM_IPC_SERVER_STATUS, False), &atr);
            if (atr != None)
                return status;
            else 
                return 2;
        }
    }
}
static void 
process_multiple(Display *dpy, Atom read_atom, char **argv, int argc, Window *all_wins, int n_wins)
{
    XEvent e;
    XPropertyEvent *pe;
    char **list; 
    int count;

    while (1)
    {
        XNextEvent(dpy, &e);
        if (e.type == DestroyNotify)
        {
            int non_zero = 0;;
            Window win = ((XDestroyWindowEvent*)(&e.xdestroywindow))->window;
            for (int i=0; i<n_wins; i++)
            {
                if (all_wins[i] == win)
                    all_wins[i] = 0;
                else if (all_wins[i] != 0)
                    non_zero++;
            }
            if (non_zero == 1)
                continue;
            else 
                break;

        }
        if (e.type != PropertyNotify)
            continue;
        pe = &(e.xproperty);
        if (pe->atom == read_atom && dwbremote_get_property(dpy, pe->window, read_atom, &list, &count))
        {
            if (count > 0)
            {
                for (int i=argc; i>0; i--)
                {
                    if (!strcmp(list[0], argv[i-1]))
                    {
                        if (s_opts & OPT_SHOW_WID)
                            printf("%lu ", pe->window);
                        printf("%s", argv[i-1]);
                        if (count > 1)
                        {
                            printf(" %s", list[1]);
                        }
                        putchar('\n');
                    }
                }
                fflush(stdout);
                XFreeStringList(list);
            }
            XDeleteProperty(dpy, pe->window, read_atom);
        }
    }
}


int 
main(int argc, char **argv)
{
    Display *dpy; 
    Window win = 0;
    int ret = 0;
    static Atom read_atom;
    Window root;
    int n_wins = 0;
    int get_multiple = 0;
    Window *all_wins = NULL;

    int pargc = argc - 1;
    char **pargv = argv + 1;


    if (argc < 2)
    {
        help();
        return 1;
    }

    if (STREQ("-h", *pargv) || STREQ("--help", *pargv))
    {
        help(0);
        return 0;
    }
    if (STREQ("-v", *pargv) || STREQ("--version", *pargv))
        version();

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "Cannot open display!\n");
        return 1;
    }

    root = RootWindow(dpy, DefaultScreen(dpy));

    for (; pargc > 0 && **pargv == '-'; pargc--, pargv++)
    {
        if (STREQ("-l", *pargv) || STREQ("--list", *pargv))
        {
            get_wins(dpy, root, &all_wins, &n_wins, NULL, NULL, False);
            for (int i=0; i<n_wins; i++)
                printf("%lu\n", all_wins[i]);
            goto finish;
        }
        if (CHECK_ARG(*pargv, "-a", "--all"))
            get_wins(dpy, root, &all_wins, &n_wins, NULL, NULL, False);
        else if (CHECK_ARG(*pargv, "-s", "--show-id"))
            s_opts |= OPT_SHOW_WID;
        else if (consume_arg("-i", "--id", &pargc, &pargv))
        {
            unsigned long wid = parse_number(*pargv);
            get_wins(dpy, root, &all_wins, &n_wins, (void *) &wid, cmp_winid, True);
        }
        else if (consume_arg("-p", "--pid", &pargc, &pargv))
        {
            pid_t pid = (pid_t)parse_number(*pargv);
            if (pid != 0)
                get_wins(dpy, root, &all_wins, &n_wins, (void *) &pid, cmp_pid, True);
        }
        else if (consume_arg("-c", "--class", &pargc, &pargv))
            get_wins(dpy, root, &all_wins, &n_wins, (void *) *pargv, cmp_class, False);
        else if (consume_arg("-n", "--name", &pargc, &pargv))
            get_wins(dpy, root, &all_wins, &n_wins, (void *) *pargv, cmp_name, False);
        else 
        {
            fprintf(stderr, "Unknown option %s\n", *pargv);
            help(1);
        }
    }
    if (pargc < 2 && (pargc < 1 || *pargv[0] != ':'))
    {
        help();
        goto finish;
    }
    if (all_wins == NULL)
    {
        char *window_id = getenv("DWB_WINID");
        if (window_id != NULL)
            win = parse_number(window_id);
        if (win != 0)
            append_win(win, &all_wins, &n_wins);
    }
    if (all_wins == NULL)
    {
        Window win = 0; 
        dwbremote_get_last_focus_id(dpy, root, XInternAtom(dpy, DWB_ATOM_IPC_FOCUS_ID, False), &win);
        if (win == 0)
        {
            fprintf(stderr, "Failed to get window id\n");
            goto finish;
        }
        append_win(win, &all_wins, &n_wins);
    }

    if (STREQ(*pargv, "hook"))
    {
        get_multiple = 1;
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_HOOK, False);
    }
    else if (STREQ(*pargv, "bind"))
    {
        get_multiple = 1;
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_BIND, False);
    }
    else 
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_CLIENT_READ, False);

    if (get_multiple)
    {
        for (int i=0; i<n_wins; i++)
            send_command(dpy, all_wins[i], PropertyChangeMask | StructureNotifyMask, pargv, pargc);
        process_multiple(dpy, read_atom, pargv, pargc, all_wins, n_wins);
    }
    else 
    {
        for (int i=0; i<n_wins; i++)
            ret += process_one(dpy, all_wins[i], read_atom, pargv, pargc);
    }
finish: 
    if (all_wins != NULL)
        free(all_wins);
    XCloseDisplay(dpy);
    return ret;
}
