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

/*}}}*/

/* SOUP_MESSAGE {{{*/
/** 
 * A SoupUri
 *
 * @class
 * @name SoupUri
 * */
 /**
  * The scheme part of the uri
  * @name scheme 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The user part of the uri
  * @name user 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The password part of the uri
  * @name password 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The host part of the uri
  * @name host 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The port of the uri
  * @name port 
  * @memberOf SoupUri.prototype 
  * @type Number
  * @readonly
  * */
 /**
  * The path part of the uri
  * @name path
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The query part of the uri
  * @name query 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
 /**
  * The fragment part of the uri
  * @name fragment 
  * @memberOf SoupUri.prototype 
  * @type String
  * @readonly
  * */
static JSValueRef 
get_soup_uri(JSContextRef ctx, JSObjectRef object, SoupURI * (*func)(SoupMessage *), JSValueRef *exception)
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    if (msg == NULL)
        return NIL;

    SoupURI *uri = func(msg);
    if (uri == NULL)
        return NIL;
    return suri_to_object(ctx, uri, exception);
}

/* message_get_uri {{{*/
/**
 * The uri of a message
 *
 * @name uri
 * @memberOf SoupMessage.prototype
 * @type SoupUri
 * */
static JSValueRef 
message_get_uri(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return get_soup_uri(ctx, object, soup_message_get_uri, exception);
}/*}}}*/

/* message_get_first_party {{{*/
/**
 * The first party uri of a message
 *
 * @name firstParty
 * @memberOf SoupMessage.prototype
 * @type SoupUri
 * */
static JSValueRef 
message_get_first_party(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    return get_soup_uri(ctx, object, soup_message_get_first_party, exception);
}/*}}}*/
/**
 * The request headers of a message
 *
 * @name requestHeaders
 * @memberOf SoupMessage.prototype
 * @type SoupHeaders
 *
 * @since 1.1
 * */
static JSValueRef 
message_get_request_headers(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    JSValueRef ret = NIL;
    if (msg != NULL)
    {
        ScriptContext *sctx = scripts_get_context();
        if (sctx != NULL) {

            SoupMessageHeaders *headers;
            g_object_get(msg, "request-headers", &headers, NULL);
            ret = JSObjectMake(ctx, sctx->classes[CLASS_SOUP_HEADER], headers);
            scripts_release_context();
        }
    }
    return NIL;
}/*}}}*/
/**
 * The response headers of a message
 *
 * @name responseHeaders
 * @memberOf SoupMessage.prototype
 * @type SoupHeaders
 *
 * @since 1.1
 * */
static JSValueRef 
message_get_response_headers(JSContextRef ctx, JSObjectRef object, JSStringRef js_name, JSValueRef* exception) 
{
    SoupMessage *msg = JSObjectGetPrivate(object);
    JSValueRef ret = NIL;
    if (msg != NULL)
    {
        ScriptContext *sctx = scripts_get_context();
        if (sctx != NULL) {
            SoupMessageHeaders *headers;
            g_object_get(msg, "response-headers", &headers, NULL);
            ret = JSObjectMake(ctx, sctx->classes[CLASS_SOUP_HEADER], headers);
            scripts_release_context();
        }
    }
    return ret;
}/*}}}*/
// TODO: Documentation
static JSValueRef 
message_set_status(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return NULL;
    double status = JSValueToNumber(ctx, argv[0], exc);
    if (!isnan(status))
    {
        SoupMessage *msg = JSObjectGetPrivate(this);
        g_return_val_if_fail(msg != NULL, NULL);
        soup_message_set_status(msg, (int)status);
    }
    return NULL;
}
// TODO: Documentation
static JSValueRef 
message_set_response(JSContextRef ctx, JSObjectRef function, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    if (argc == 0)
        return NULL;
    char *content_type = NULL;
    char *response = js_value_to_char(ctx, argv[0], -1, exc);
    if (response != NULL)
    {
        SoupMessage *msg = JSObjectGetPrivate(this);
        g_return_val_if_fail(msg != NULL, NULL);
        if (argc > 1)
            content_type = js_value_to_char(ctx, argv[1], -1, exc);

        soup_message_set_response(msg, content_type ? content_type : "text/html", SOUP_MEMORY_TAKE, response, strlen(response));
        g_free(content_type);
    }
    return NULL;
}
void 
message_initialize(ScriptContext *sctx) {
    /**
     * Represents a SoupMessage 
     *
     * @name SoupMessage
     * @augments GObject 
     * @class 
     *      Represents a SoupMessage. Can be used to inspect information about a
     *      request
     *
     * @returns undefined
     * */
    JSStaticValue message_values[] = {
        { "uri",                message_get_uri, NULL, kJSDefaultAttributes }, 
        { "firstParty",         message_get_first_party, NULL, kJSDefaultAttributes }, 
        { "requestHeaders",     message_get_request_headers, NULL, kJSDefaultAttributes }, 
        { "responseHeaders",    message_get_response_headers, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSStaticFunction message_functions[] = {
        { "setStatus",       message_set_status, kJSDefaultAttributes }, 
        { "setResponse",     message_set_response, kJSDefaultAttributes }, 
        { 0, 0, 0 }, 
    };

    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "SoupMessage";
    cd.staticFunctions = message_functions;
    cd.staticValues = message_values;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_MESSAGE] = JSClassCreate(&cd);

    sctx->constructors[CONSTRUCTOR_SOUP_MESSAGE] = scripts_create_constructor(sctx->global_context, "SoupMessage", sctx->classes[CLASS_MESSAGE], NULL, NULL);
}
