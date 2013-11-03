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

#include "dwbremote.h"
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xatom.h>
#ifndef MAX 
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif

int 
dwbremote_get_property(Display *dpy, Window win, Atom atom, char ***list, int *count)
{
    int result = True;
    XTextProperty prop;
    if (!XGetTextProperty(dpy, win, &prop, atom))
    {
        return False;
    }
    if (Xutf8TextPropertyToTextList(dpy, &prop, list, count) != Success)
    {
        result = False;
    }
    XFree(prop.value);

    return result;
}

int 
dwbremote_set_property_list(Display *dpy, Window win, Atom atom, char **list, int count)
{
    XTextProperty prop;
    Xutf8TextListToTextProperty(dpy, list, count, XUTF8StringStyle, &prop);
    XSetTextProperty(dpy, win, &prop, atom);
    XFree(prop.value);
    return 0;
}

int 
dwbremote_set_property_list_by_name(Display *dpy, Window win, const char *name, char **list, int count)
{
    return dwbremote_set_property_list(dpy, win, XInternAtom(dpy, name, False), list, count);
}

int 
dwbremote_set_property_value(Display *dpy, Window win, Atom atom, char *value)
{
    char *argv[] = { value };
    return dwbremote_set_property_list(dpy, win, atom, argv, 1);
}
int 
dwbremote_set_property_value_by_name(Display *dpy, Window win, const char *name, char *value)
{
    return dwbremote_set_property_value(dpy, win, XInternAtom(dpy, name, False), value);
}

int 
dwbremote_set_formatted_property_value(Display *dpy, Window win, Atom atom, const char *format, ...)
{
    va_list arg_list; 
    char buffer[2048];

    va_start(arg_list, format);
    vsnprintf(buffer, sizeof(buffer), format, arg_list);
    va_end(arg_list);
    return dwbremote_set_property_value(dpy, win, atom, buffer);
}

int 
dwbremote_get_int_property(Display *dpy, Window win, Atom a, Atom *atr)
{
    int *status;
    int afr;
    unsigned long nr, bar; 
    XGetWindowProperty(dpy, win, 
            a, 0L, 1, False, XA_INTEGER, 
            atr, &afr, &nr, &bar, (unsigned char **)&status);
    if (*atr != None)
    {
        return *(int *)status;
    }
    else
    {
        return -1;
    }
}
int 
dwbremote_set_int_property(Display *dpy, Window win, Atom atom, int status)
{
    return XChangeProperty(dpy, win, atom, XA_INTEGER, 32, PropModeReplace, (unsigned char*)&status, 1);
}

int 
dwbremote_get_last_focus_id(Display *dpy, Window win, Atom a, Window *win_ret)
{
    Window *children = NULL; 
    Atom atr;
    int max = 0, old_max = -1;
    int ret = 0;
    unsigned int n_children;
    Window root, parent;

    if (XQueryTree(dpy, win, &root, &parent, &children, &n_children) == 0)
        goto error_out;

    for (unsigned int i=0; i<n_children; i++)
    {
        ret = dwbremote_get_int_property(dpy, children[i], a, &atr);
        old_max = max;
        if (atr != None)
        {
            max = MAX(ret, max);
            if (max > old_max)
                *win_ret = children[i];
        }
        else 
            max = MAX(dwbremote_get_last_focus_id(dpy, children[i], a, win_ret), max);

    }
error_out:
    if (children != NULL)
        XFree((char *)children);
    return max;
}
