/*
 * Copyright (c) 2010-2014 Stefan Bolte <portix@gmx.net>
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

#include "private.h"

#if WEBKIT_CHECK_VERSION(1, 10, 0)
static JSValueRef 
file_chooser_select(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    if (argc == 0) {
        return NULL;
    }
    WebKitFileChooserRequest *request = JSObjectGetPrivate(this);
    if (request == NULL) {
        return NULL;
    }
    char **files = g_malloc0_n(argc, sizeof(char *));

    for (size_t i=0; i<argc; i++) {
        files[i] = js_value_to_char(ctx, argv[i], -1, exc);
        if (files[i] == NULL) {
            goto error_out;
        }
    }
    webkit_file_chooser_request_select_files(request, (const gchar * const *)files);

error_out:
    if (request != NULL) {
        g_object_unref(request);
    }
    g_strfreev(files);

    return NULL;
}
void 
filechooser_initialize(ScriptContext *sctx) {
    JSStaticFunction file_chooser_functions[] = { 
        { "selectFiles",          file_chooser_select,        kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitFileChooserRequest";
    cd.staticFunctions = file_chooser_functions;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_FILE_CHOOSER] = JSClassCreate(&cd);
}
#endif
