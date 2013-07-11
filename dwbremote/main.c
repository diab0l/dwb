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

#ifndef NAME
#define NAME "dwbremote"
#endif
#ifndef VERSION
#define VERSION __DATE__
#endif
#ifndef COPYRIGHT
#define COPYRIGHT "(C) 2010-2013 Stefan Bolte"
#endif
#ifndef LICENSE
#define LICENSE "GNU General Public License, version 3 or later"
#endif

enum {
    GET_ONCE = 0,
    GET_MULTIPLE, 
};

static void 
help(int ret)
{
    printf( "USAGE: \n" 
            "   dwbremote [options] <command> <arguments>\n\n"
            "OPTIONS: \n"
            "   -c --class <class>      Search for window id by WM_CLASS <class>\n"
            "   -i --id    <windowid>   Send commands to window with id <windowid>\n"
            "   -h --help               Show this help and exit\n"
            "   -l --list               List all dwb window ids\n"
            "   -n --name  <class>      Search for window id by WM_NAME <name>\n"
            "   -p --pid   <pid>        Send commands to instance with process id <pid>\n"
            "   -v --version            Print version information and exit\n");
    exit(ret);
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
    dwbremote_get_status(dpy, win, &a);
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

static void 
query_windows(Display *dpy, Window win, void *data, Window *winret, Bool (*cmp)(Display *, Window win, void *data))
{
    Window unused_win; 
    Window *children; 
    unsigned int n_children;

    if (XQueryTree(dpy, win, &unused_win, &unused_win, &children, &n_children) == 0)
        return;
    for (unsigned int i=0; i<n_children; i++)
    {
        if (is_dwb_win(dpy, children[i]) && cmp(dpy, children[i], data))
        {
            *winret = children[i];
            break;
        }
        else 
            query_windows(dpy, children[i], data, winret, cmp);
    }
}
static void * 
xrealloc(void *ptr, size_t size)
{
    void *ret = realloc(ptr, size);
    if (ret == NULL)
    {
        fprintf(stderr, "Cannot realloc %zu bytes!", size);
        exit(1);
    }
    return ret;
}

static void 
get_wins(Display *dpy, Window win, Window **win_return, int *n_ret)
{
    Window unused_win; 
    Window *children; 
    unsigned int n_children;

    if (XQueryTree(dpy, win, &unused_win, &unused_win, &children, &n_children) == 0)
        return;
    for (unsigned int i=0; i<n_children; i++)
    {
        if (is_dwb_win(dpy, children[i]))
        {
            *win_return = xrealloc(*win_return, (*n_ret + 1) * sizeof(Window));
            (*win_return)[*n_ret] = children[i];
            (*n_ret)++;
        }
        get_wins(dpy, children[i], win_return, n_ret);
    }
}

static Bool 
consume_arg(const char *opt, const char *lopt, int *argc, char ***argv, Bool condition)
{
    if (STREQ(**argv, opt) || STREQ(**argv, lopt)) 
    {
        (*argv)++; 
        (*argc)--;
        if (*argc < 3)
            help(1);
        if (!condition)
            fprintf(stderr, "Ignoring option '%s %s'\n", lopt, **argv);
        return condition;
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

int 
main(int argc, char **argv)
{
    Display *dpy; 
    Window win = 0;
    int type = GET_ONCE;
    int ret = 0;
    Atom read_atom;
    XEvent e;
    XPropertyEvent *pe;
    Atom atr; 
    int status;
    char **list; 
    int count;
    Window root;
    Window *all_wins = NULL;
    int n_wins = 0;

    int pargc = argc - 1;
    char **pargv = argv + 1;


    if (argc < 2)
        help(1);

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "Cannot open display!\n");
        return 1;
    }

    root = RootWindow(dpy, DefaultScreen(dpy));

    for (; pargc > 0 && **pargv == '-'; pargc--, pargv++)
    {
        if (STREQ("-h", *pargv) || STREQ("--help", *pargv))
        {
            XCloseDisplay(dpy);
            help(0);
            return 0;
        }
        if (STREQ("-v", *pargv) || STREQ("--version", *pargv))
        {
            XCloseDisplay(dpy);
            version();
            return 0;
        }
        if (STREQ("-l", *pargv) || STREQ("--list", *pargv))
        {
            get_wins(dpy, root, &all_wins, &n_wins);
            for (int i=0; i<n_wins; i++)
                printf("%lu\n", all_wins[i]);
            free(all_wins);
            XCloseDisplay(dpy);
            return 0;
        }
        if (consume_arg("-i", "--id", &pargc, &pargv, win == 0))
        {
            unsigned long wid = parse_number(*pargv);
            query_windows(dpy, root, (void *) &wid, &win, cmp_winid);
            continue;
        }
        if (consume_arg("-p", "-pid", &pargc, &pargv, win == 0))
        {
            pid_t pid = (pid_t)parse_number(*pargv);
            if (pid != 0)
                query_windows(dpy, root, (void *)&pid, &win, cmp_pid);
            continue;
        }
        if (consume_arg("-c", "-class", &pargc, &pargv, win == 0))
        {
            query_windows(dpy, root, *pargv, &win, cmp_class);
            continue;
        }
        if (consume_arg("-n", "-name", &pargc, &pargv, win == 0))
        {
            query_windows(dpy, root, *pargv, &win, cmp_name);
            continue;
        }
        fprintf(stderr, "Unknown option %s\n", *pargv);
        help(1);
    }
    if (win == 0 && all_wins == NULL)
    {
        char *window_id = getenv("DWB_WINID");
        if (window_id != NULL)
            win = parse_number(window_id);
        if (win == 0)
        {
            XCloseDisplay(dpy);
            fprintf(stderr, "Failed to get window id\n");
            return 1;
        }
    }

    if (STREQ(*pargv, "hook"))
    {
        type = GET_MULTIPLE;
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_HOOK, False);
    }
    else if (STREQ(*pargv, "bind"))
    {
        type = GET_MULTIPLE;
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_BIND, False);
    }
    else 
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_CLIENT_READ, False);

    dwbremote_set_property_list_by_name(dpy, win, DWB_ATOM_IPC_CLIENT_WRITE, pargv, pargc);
    XSelectInput(dpy, win, PropertyChangeMask | StructureNotifyMask);

    while (1)
    {
        XNextEvent(dpy, &e);
        if (e.type == DestroyNotify)
            break;
        if (e.type != PropertyNotify)
            continue;
        pe = &(e.xproperty);
        if (pe->atom == read_atom && dwbremote_get_property(dpy, win, read_atom, &list, &count))
        {
            if (count > 0)
            {
                if (type == GET_MULTIPLE)
                {
                    for (int i=pargc; i>0; i--)
                    {
                        if (!strcmp(list[0], pargv[i-1]))
                        {
                            printf("%s", pargv[i-1]);
                            if (count > 1)
                            {
                                printf(" %s", list[1]);
                            }
                            putchar('\n');
                        }
                    }
                    fflush(stdout);
                }
                else 
                {
                    printf("%s\n", list[0]);
                }
                XFreeStringList(list);
            }
            XDeleteProperty(dpy, win, read_atom);
        }
        if (type == GET_ONCE && pe->atom == XInternAtom(dpy, DWB_ATOM_IPC_SERVER_STATUS, False))
        {
            status = dwbremote_get_status(dpy, win, &atr);
            if (atr != None)
            {
                ret = status;
            }
            break;
        }

    }
    XCloseDisplay(dpy);
    return ret;
}
