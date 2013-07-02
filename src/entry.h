/*
 * Copyright (c) 2010-2013 Stefan Bolte <portix@gmx.net>
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

#ifndef __DWB_ENTRY_H__
#define __DWB_ENTRY_H__
DwbStatus entry_history_forward(GList **last);
DwbStatus entry_history_back(GList **list, GList **last);
void entry_focus();
void entry_hide();
void entry_clear_history();
void entry_set_text(const char *text);
void entry_insert_text(const char *text);
void entry_move_cursor_step(GtkMovementStep step, int stepcount, gboolean del);
void entry_snoop(GCallback callback, gpointer data);
void entry_snoop_end(GCallback callback, gpointer data);
void entry_clear(gboolean visibility);
gboolean entry_snooping();
#endif
