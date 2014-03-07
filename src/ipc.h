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

#ifndef __DWB_IPC_H__
#define __DWB_IPC_H__

#include <dwbremote.h>

enum {
    IPC_HOOK_hook = 1<<0, 
    IPC_HOOK_navigation = 1<<1, 
    IPC_HOOK_load_finished = 1<<2, 
    IPC_HOOK_load_committed = 1<<3, 
    IPC_HOOK_close_tab = 1<<4, 
    IPC_HOOK_new_tab = 1<<5, 
    IPC_HOOK_focus_tab = 1<<6, 
    IPC_HOOK_execute = 1<<7, 
    IPC_HOOK_change_mode = 1<<8, 
    IPC_HOOK_download_finished = 1<<9, 
    IPC_HOOK_document_finished = 1<<10, 
};

void ipc_start(GtkWidget *);
void ipc_end(GtkWidget *);
void ipc_send_hook(char *hook, const char *format, ...);
void ipc_send_end_win(void);

#define IPC_SEND_HOOK(hook, ...); do { \
    if (dwb.state.ipc_hooks & IPC_HOOK_##hook) ipc_send_hook(#hook, __VA_ARGS__); \
    } while(0)
#endif
