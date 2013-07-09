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
#include "dwbrc.h"
#define STREQ(x, y) (strcmp(x, y) == 0)
#define STRNEQ(x, y, n) (strncmp((x), (y), (n)) == 0)

enum {
    GET_ONCE = 0,
    GET_MULTIPLE, 
};

static void 
help(int ret)
{
    printf( "USAGE: \n" 
            "   dwbrc [-i winid] <command> <arguments>\n");
    exit(ret);
}

Bool 
is_toplevel(Display *dpy, Window win)
{
    Bool ret = False;
    XWMHints *hints = XGetWMHints(dpy, win);
    if (hints)
    {
        ret = True;
        XFree(hints);
    }
    return ret;
}

Bool 
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

    return g_pid == orig_pid && is_toplevel(dpy, win);
}
Bool 
cmp_class(Display *dpy, Window win, void *data)
{
    char *orig_class = (char *) data;
    XClassHint hint;
    int status = XGetClassHint(dpy, win, &hint);
    return status != 0 && hint.res_class && STREQ(hint.res_class, orig_class) && is_toplevel(dpy, win);
}

Bool 
cmp_name(Display *dpy, Window win, void *data)
{
    char *orig_name = (char *) data;
    XClassHint hint;
    int status = XGetClassHint(dpy, win, &hint);
    return status != 0 && hint.res_class && STREQ(hint.res_name, orig_name) && is_toplevel(dpy, win);
}
void 
query_windows(Display *dpy, Window win, void *data, Window *winret, Bool (*cmp)(Display *, Window win, void *data))
{
    Window unused_win; 
    Window *children; 
    unsigned int n_children;

    if (XQueryTree(dpy, win, &unused_win, &unused_win, &children, &n_children) == 0)
        return;
    for (unsigned int i=0; i<n_children; i++)
    {
        if (cmp(dpy, children[i], data))
        {
            *winret = children[i];
            break;
        }
        else 
            query_windows(dpy, children[i], data, winret, cmp);
    }
}
int 
main(int argc, char **argv)
{
    Display *dpy; 
    Window win = 0;

    char *end; 
    unsigned long wid = 0;;
    char **multiple_list = NULL;
    int ret = 0;
    Atom read_atom;

    XEvent e;
    Atom atr; 
    int afr;
    unsigned long nr, bar; 
    int *status;
    char **list; 
    int count;
    int arg_start = 3;
    char *window_id;
    int parse_wid = 0;

    int type = GET_ONCE;
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "Cannot open display!\n");
        return -1;
    }

    if (argc < 3)
        help(1);
    if (STREQ(argv[1], "-id"))
    {
        if (argc < 5)
            help(1);
        window_id = argv[2];
        parse_wid = 1;
    }
    else if (STREQ(argv[1], "-pid"))
    {
        pid_t pid = (pid_t)strtoul(argv[2], &end, 10);
        if (*end != '\0')
        {
            fprintf(stderr, "Failed to parse pid!\n");
            ret = 1; 
            goto finish;
        }
        query_windows(dpy, RootWindow(dpy, DefaultScreen(dpy)), (void *)&pid, &win, cmp_pid);
    }
    else if (STREQ(argv[1], "-class"))
    {
        query_windows(dpy, RootWindow(dpy, DefaultScreen(dpy)), argv[2], &win, cmp_class);
    }
    else if (STREQ(argv[1], "-name"))
    {
        query_windows(dpy, RootWindow(dpy, DefaultScreen(dpy)), argv[2], &win, cmp_name);
    }
    else 
    {
        window_id = getenv("DWB_WINID");
        parse_wid = 1;
        arg_start = 1;
    }
    if (parse_wid)
    {
        if (window_id == NULL)
            help(1);
        if (STRNEQ(window_id, "0x", 2))
            wid = strtoul(window_id + 2, &end, 16);
        else 
            wid = strtoul(window_id, &end, 10);
        if (*end != '\0')
        {
            fprintf(stderr, "Failed to parse window id!\n");
            ret = 1;
            goto finish;
        }
        win = wid;
    }
    else if (win == 0)
    {
        fprintf(stderr, "Failed to get window id!\n");
        ret = 1; 
        goto finish;
    }

    if (STREQ(argv[arg_start], "hook"))
    {
        type = GET_MULTIPLE;
        multiple_list = &argv[arg_start];
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_HOOK, False);
    }
    else if (STREQ(argv[arg_start], "bind"))
    {
        type = GET_MULTIPLE;
        multiple_list = &argv[arg_start];
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_BIND, False);
    }
    else 
        read_atom = XInternAtom(dpy, DWB_ATOM_IPC_CLIENT_READ, False);

    dwbrc_set_property_list_by_name(dpy, win, DWB_ATOM_IPC_CLIENT_WRITE, &argv[arg_start], argc-arg_start);

    XSelectInput(dpy, win, PropertyChangeMask | StructureNotifyMask);
    while (1)
    {
        XNextEvent(dpy, &e);
        if (e.type == DestroyNotify)
            break;
        if (e.type != PropertyNotify)
            continue;
        XPropertyEvent *pe = &(e.xproperty);
        if (pe->atom == read_atom && dwbrc_get_property(dpy, win, read_atom, &list, &count))
        {
            if (count > 0)
            {
                if (type == GET_MULTIPLE)
                {
                    for (int i=0; i<argc-arg_start; i++)
                    {
                        if (!strcmp(list[0], multiple_list[i]))
                        {
                            printf("%s", multiple_list[i]);
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
            XGetWindowProperty(dpy, win, 
                    XInternAtom(dpy, DWB_ATOM_IPC_SERVER_STATUS, False), 
                    0L, 1, False, XA_INTEGER, 
                    &atr, &afr, &nr, &bar, (unsigned char **)&status);
            ret = *status;
            break;
        }

    }
finish:
    XCloseDisplay(dpy);
    return ret;
}
