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

/* download_constructor_cb {{{*/
/**
 * Constructs a new download
 *
 * @memberOf WebKitDownload.prototype
 * @constructor
 * @param {String} uri The uri of the download
 * */
static JSObjectRef 
download_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    if (argc<1) 
        return JSValueToObject(ctx, NIL, NULL);

    char *uri = js_value_to_char(ctx, argv[0], -1, exception);
    if (uri == NULL) 
    {
        js_make_exception(ctx, exception, EXCEPTION("Download constructor: invalid argument"));
        return JSValueToObject(ctx, NIL, NULL);
    }

    WebKitNetworkRequest *request = webkit_network_request_new(uri);
    g_free(uri);
    if (request == NULL) 
    {
        js_make_exception(ctx, exception, EXCEPTION("Download constructor: invalid uri"));
        return JSValueToObject(ctx, NIL, NULL);
    }

    WebKitDownload *download = webkit_download_new(request);
    return make_object_for_class(ctx, CLASS_DOWNLOAD, G_OBJECT(download), true);
}/*}}}*/

/* stop_download_notify {{{*/
static gboolean 
stop_download_notify(CallbackData *c) 
{
    WebKitDownloadStatus status = webkit_download_get_status(WEBKIT_DOWNLOAD(c->gobject));
    if (status == WEBKIT_DOWNLOAD_STATUS_ERROR || status == WEBKIT_DOWNLOAD_STATUS_CANCELLED || status == WEBKIT_DOWNLOAD_STATUS_FINISHED) 
        return true;

    return false;
}/*}}}*/

/* download_start {{{*/
/** 
 * Starts a download
 *
 * @name start
 * @memberOf WebKitDownload.prototype
 * @function 
 *
 * @param {WebKitDownload~statusCallback} callback
 *      Callback function that will be executed whenever the 
 *      {@link Enums and Flags.DownloadStatus|DownloadStatus} changes
 *
 * @returns {Boolean}
 *      true if the download was started
 * */
static JSValueRef 
download_start(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitDownload *download = JSObjectGetPrivate(this);
    if (download == NULL)
        return JSValueMakeBoolean(ctx, false);

    if (webkit_download_get_destination_uri(download) == NULL) 
    {
        js_make_exception(ctx, exc, EXCEPTION("Download.start: destination == null"));
        return JSValueMakeBoolean(ctx, false);
    }

    if (argc > 0) 
        make_callback(ctx, this, G_OBJECT(download), "notify::status", argv[0], stop_download_notify, exc);

    webkit_download_start(download);
    return JSValueMakeBoolean(ctx, true);

}/*}}}*/

/** 
 * Cancels a download
 *
 * @name cancel
 * @memberOf WebKitDownload.prototype
 * @function 
 * */
/* download_cancel {{{*/
static JSValueRef 
download_cancel(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    WebKitDownload *download = JSObjectGetPrivate(this);
    g_return_val_if_fail(download != NULL, NULL);

    webkit_download_cancel(download);
    return NULL;
}/*}}}*/
void 
download_initialize(ScriptContext *sctx) {
    /** 
     * Constructs a new download
     *
     * @class 
     *    Object that can be used for downloads
     * @name WebKitDownload
     * @augments GObject
     *
     * @constructs WebKitDownload 
     *
     * @param {String} url The url of the download
     *
     * @returns WebKitDownLoad
     * */
    /* download */
    JSStaticFunction download_functions[] = { 
        { "start",          download_start,        kJSDefaultAttributes },
        { "cancel",         download_cancel,        kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };

    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "WebKitDownload";
    cd.staticFunctions = download_functions;
    cd.staticValues = NULL;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_DOWNLOAD] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_DOWNLOAD] = scripts_create_constructor(sctx->global_context, "WebKitDownload", sctx->classes[CLASS_DOWNLOAD], download_constructor_cb, NULL);
}
