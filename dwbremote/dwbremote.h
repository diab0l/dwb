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

#ifndef __DWB_DWB_RC_H__
#define __DWB_DWB_RC_H__

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define DWB_ATOM_IPC_SERVER_READ  "__DWB_IPC_SERVER_READ"
#define DWB_ATOM_IPC_SERVER_WRITE "__DWB_IPC_SERVER_WRITE"
#define DWB_ATOM_IPC_HOOK "__DWB_IPC_HOOK"
#define DWB_ATOM_IPC_BIND "__DWB_IPC_BIND"
#define DWB_ATOM_IPC_CLIENT_READ  DWB_ATOM_IPC_SERVER_WRITE
#define DWB_ATOM_IPC_CLIENT_WRITE DWB_ATOM_IPC_SERVER_READ
#define DWB_ATOM_IPC_SERVER_STATUS "__DWB_IPC_SERVER_STATUS"
#define DWB_ATOM_IPC_FOCUS_ID "__DWB_IPC_FOCUS_ID"


int 
dwbremote_get_property(Display *dpy, Window win, Atom atom, char ***list, int *count);

int 
dwbremote_set_property_value_by_name(Display *dpy, Window win, const char *name, char *value);

int 
dwbremote_set_property_value(Display *dpy, Window win, Atom atom, char *value);

int 
dwbremote_set_property_list_by_name(Display *dpy, Window win, const char *name, char **list, int count);

int 
dwbremote_set_property_list(Display *dpy, Window win, Atom atom, char **list, int count);

int 
dwbremote_set_formatted_property_value(Display *dpy, Window win, Atom atom, const char *format, ...);

int 
dwbremote_get_int_property(Display *dpy, Window win, Atom a, Atom *atr);

int 
dwbremote_set_int_property(Display *dpy, Window win, Atom atom, int status);

int 
dwbremote_get_last_focus_id(Display *dpy, Window win, Atom a, Window *win_ret);

#endif
