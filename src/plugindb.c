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
#include "plugindb.h"
#include "util.h"

static int
cmp_plugin_name(WebKitWebPlugin *a, WebKitWebPlugin *b)
{
    if (a == NULL || b == NULL)
        return 1;
    return g_strcmp0(webkit_web_plugin_get_name(a), webkit_web_plugin_get_name(b));
}

GSList *
plugindb_get_plugin_list()
{
    WebKitWebPluginDatabase *db = webkit_get_web_plugin_database();
    GSList *plugins = webkit_web_plugin_database_get_plugins(db);

    plugins = g_slist_sort(plugins, (GCompareFunc)cmp_plugin_name);

    return plugins;
}

void 
plugindb_set_enabled(const char *path, gboolean enabled, gboolean write)
{
    WebKitWebPluginDatabase *db = webkit_get_web_plugin_database();
    GSList *plugins = webkit_web_plugin_database_get_plugins(db);
    GString *buf = NULL; 
    if (write)
        buf = g_string_new(NULL);
    for (GSList *l = plugins; l; l=l->next)
    {
        const char *ppath = webkit_web_plugin_get_path(l->data);
        gboolean currently_enabled = webkit_web_plugin_get_enabled(l->data);
        if (!g_strcmp0(path, ppath))
        {
            webkit_web_plugin_set_enabled(l->data, enabled);
            currently_enabled = enabled;
        }
        if (write && !currently_enabled)
        {
            if (!currently_enabled)
            {
                g_string_append_printf(buf, "%s\n", ppath);
            }
        }
    }
    if (write)
    {
        util_set_file_content(dwb.files[FILES_PLUGINDB], buf->str);
        g_string_free(buf, true);
    }
    webkit_web_plugin_database_plugins_list_free(plugins);
}

void
plugindb_init()
{
    char **lines = util_get_lines(dwb.files[FILES_PLUGINDB]);
    for (int i=0; lines[i]; i++)
    {
        if (*lines[i])
            plugindb_set_enabled(lines[i], false, false);
    }
    g_strfreev(lines);
}
