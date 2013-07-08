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
int 
main(int argc, char **argv)
{
    Display *dpy; 
    Window win;

    char *end; 
    unsigned long wid;
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
    int arg_start = 1;
    char *window_id;

    int type = GET_ONCE;

    if (argc < 3)
        help(1);
    if (STREQ(argv[1], "-i"))
    {
        if (argc < 5)
            help(1);
        window_id = argv[2];
        arg_start = 3;
    }
    else 
    {
        window_id = getenv("DWB_WINID");
    }
    if (window_id == NULL)
        help(1);
    if (STRNEQ(window_id, "0x", 2))
        wid = strtoul(window_id + 2, &end, 16);
    else 
        wid = strtoul(window_id, &end, 10);
    if (*end != '\0')
    {
        fprintf(stderr, "Parsing window id failed!\n");
        help(1);
        return -1;
    }
    win = wid;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "Cannot open display!\n");
        return -1;
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
                                printf(" %s\n", list[1]);
                            }
                        }
                    }
                }
                else 
                {
                    printf("%s\n", list[0]);
                }
                if (type != GET_ONCE)
                    fflush(stdout);
                XFreeStringList(list);
            }
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
    XCloseDisplay(dpy);
    return ret;
}
