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

#ifndef __DWB_PLUGINDB_H__
#define __DWB_PLUGINDB_H__

#define plugindb_free_list(list) (g_slist_free_full(list, (GDestroyNotify)g_object_unref))

GSList * 
plugindb_get_unique_plugin_list(void);

void 
plugindb_set_enabled(const char *name, gboolean enabled, gboolean write);

void 
plugindb_init(void);

#endif
