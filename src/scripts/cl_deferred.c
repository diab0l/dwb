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

/**
 * Constructs a new Deferred.
 * @memberOf Deferred.prototype
 * @constructor 
 */
static JSObjectRef 
deferred_constructor_cb(JSContextRef ctx, JSObjectRef constructor, size_t argc, const JSValueRef argv[], JSValueRef* exception) 
{
    return deferred_new(ctx);
}

static void 
deferred_destroy(JSContextRef ctx, JSObjectRef this, DeferredPriv *priv) 
{
    g_return_if_fail(this != NULL);

    if (priv == NULL)
        priv = JSObjectGetPrivate(this);

    if (priv->resolve)
        JSValueUnprotect(ctx, priv->resolve);
    if (priv->reject)
        JSValueUnprotect(ctx, priv->reject);
    if (priv->next)
        JSValueUnprotect(ctx, priv->next);

    JSObjectSetPrivate(this, NULL);

    g_free(priv);

    JSValueUnprotect(ctx, this);
}

static DeferredPriv * 
deferred_transition(JSContextRef ctx, JSObjectRef old, JSObjectRef new)
{
    DeferredPriv *opriv = JSObjectGetPrivate(old);
    DeferredPriv *npriv = JSObjectGetPrivate(new);

    npriv->resolve = opriv->resolve;
    if (npriv->resolve)
        JSValueProtect(ctx, npriv->resolve);
    npriv->reject = opriv->reject;
    if (npriv->reject)
        JSValueProtect(ctx, npriv->reject);
    npriv->next = opriv->next;
    if (npriv->next)
        JSValueProtect(ctx, npriv->next);

    deferred_destroy(ctx, old, opriv);
    return npriv;
}


JSObjectRef
deferred_new(JSContextRef ctx) 
{
    DeferredPriv *priv = g_malloc(sizeof(DeferredPriv));
    priv->resolve = priv->reject = priv->next = NULL;
    JSObjectRef ret = NULL;

    ScriptContext *sctx = scripts_get_context();
    if (sctx != NULL) {

        ret = JSObjectMake(ctx, sctx->classes[CLASS_DEFERRED], priv);
        JSValueProtect(ctx, ret);
        priv->is_fulfilled = false;
        scripts_release_context();
    }
    else {
        ret = JSValueToObject(ctx, NIL, NULL);
    }

    return ret;
}


gboolean 
deferred_fulfilled(JSObjectRef deferred) {
    DeferredPriv *priv = JSObjectGetPrivate(deferred); 
    if (priv != NULL) {
        return priv->is_fulfilled;
    }
    return true;
}

/** 
 * Registers functions for the done and fail chain
 *
 * @name then
 * @memberOf Deferred.prototype
 * @function
 *
 *
 * @param {Deferred~resolveCallback} ondone
 *      A callback function that will be called when the deferred is resolved.
 *      If the function returns a deferred the original deferred will be
 *      replaced with the new deferred.

 * @param {Deferred~rejectCallback} onfail
 *      A callback function that will be called when the deferred is rejected.
 *      If the function returns a deferred the original deferred will be
 *      replaced with the new deferred.
 * @returns {Deferred}
 *      A new deferred that can be used to chain callbacks.
 * */
/** 
 * Called when a Deferred is resolved
 * @callback Deferred~resolveCallback
 * @param {...Object} arguments 
 *      Variable number of arguments passed to Deferred.resolve
 * */
/** 
 * Called when a Deferred is rejected
 * @callback Deferred~rejectCallback
 * @param {...Object} arguments 
 *      Variable number of arguments passed to Deferred.reject
 * */
static JSValueRef 
deferred_then(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL) 
        return NIL;

    if (argc > 0)
    {
        priv->resolve = js_value_to_function(ctx, argv[0], NULL);
        JSValueProtect(ctx, priv->resolve);
    }
    if (argc > 1) 
    {
        priv->reject = js_value_to_function(ctx, argv[1], NULL);
        JSValueProtect(ctx, priv->reject);
    }

    priv->next = deferred_new(ctx);
    JSValueProtect(ctx, priv->next);

    return priv->next;
}
/**
 * Resolves a deferred, the done-chain is called when a deferred is resolved
 *
 * @name resolve
 * @memberOf Deferred.prototype
 * @function
 *
 * @param {...Object} arguments Arguments passed to the <i>done</i> callbacks
 */
JSValueRef 
deferred_resolve(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;

    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL || priv->is_fulfilled)
        return NULL;
    ScriptContext *sctx = scripts_get_context();
    if (sctx == NULL) {
        return NULL;
    }

    if (priv->resolve) 
        ret = JSObjectCallAsFunction(ctx, priv->resolve, this, argc, argv, exc);

    priv->is_fulfilled = true;

    JSObjectRef next = priv->next;
    deferred_destroy(ctx, this, priv);

    if (next) 
    {
        if ( ret && JSValueIsObjectOfClass(ctx, ret, sctx->classes[CLASS_DEFERRED]) ) 
        {
            JSObjectRef o = JSValueToObject(ctx, ret, NULL);
            deferred_transition(ctx, next, o)->reject = NULL;
        }
        else 
        {
            if (ret && !JSValueIsNull(ctx, ret))
            {
                JSValueRef args[] = { ret };
                deferred_resolve(ctx, f, next, 1, args, exc);
            }
            else 
                deferred_resolve(ctx, f, next, argc, argv, exc);

        }
    }
    scripts_release_context();
    return NULL;
}
/**
 * Rejects a deferred, the fail-chain is called when a deferred is resolved
 *
 * @name reject
 * @memberOf Deferred.prototype
 * @function
 *
 * @param {...Object} arguments Arguments passed to the <i>fail</i> callbacks
 */
JSValueRef 
deferred_reject(JSContextRef ctx, JSObjectRef f, JSObjectRef this, size_t argc, const JSValueRef argv[], JSValueRef* exc) 
{
    JSValueRef ret = NULL;

    DeferredPriv *priv = JSObjectGetPrivate(this);
    if (priv == NULL || priv->is_fulfilled)
        return NULL;

    ScriptContext *sctx = scripts_get_context();
    if (sctx == NULL) 
        return NULL;

    if (priv->reject) 
        ret = JSObjectCallAsFunction(ctx, priv->reject, this, argc, argv, exc);

    priv->is_fulfilled = true;

    JSObjectRef next = priv->next;
    deferred_destroy(ctx, this, priv);

    if (next) 
    {
        if ( ret && JSValueIsObjectOfClass(ctx, ret, sctx->classes[CLASS_DEFERRED]) ) 
        {
            JSObjectRef o = JSValueToObject(ctx, ret, NULL);
            deferred_transition(ctx, next, o)->resolve = NULL;
        }
        else 
        {
            if (ret && !JSValueIsNull(ctx, ret))
            {
                JSValueRef args[] = { ret };
                deferred_reject(ctx, f, next, 1, args, exc);
            }
            else 
                deferred_reject(ctx, f, next, argc, argv, exc);
        }
    }
    scripts_release_context();
    return NULL;
}/*}}}*/

/** 
 * Wether this Deferred was resolved or rejected.
 *
 * @name isFulfilled
 * @memberOf Deferred.prototype
 * @type Boolean
 * @readonly
 * */
static JSValueRef 
deferred_is_fulfilled(JSContextRef ctx, JSObjectRef self, JSStringRef js_name, JSValueRef* exception) 
{
    return JSValueMakeBoolean(ctx, deferred_fulfilled(self));
}
void 
deferred_initialize(ScriptContext *sctx) {
    /**
     * Constructs a new Deferred
     *
     * @class 
     *      Deferred objects can be used to manage asynchronous operations. It
     *      can trigger a callback function when an asynchrounous operation has
     *      finished, and allows chaining of callbacks. Deferred basically has 2
     *      callback chains, a done-chain and a fail-chain. If a asynchronous
     *      operation is successful the deferred should be resolved and the done
     *      callback chain of the deferred is called. If a asynchronous
     *      operation fails the deferred should be rejected and the fail
     *      callback chain of the deferred is called.
     * @name Deferred
     * @constructs Deferred
     * @example
     * system.spawn("command").then(
     *      function() {
     *          // called when execution was  successful 
     *      },
     *      function(errorcode) {
     *          // called when execution wasn't successful 
     *      }
     * );
     *
     * function foo() {
     *     var d = new Deferred();
     *     timerStart(2000, function() {
     *         d.reject("rejected");
     *     });
     *     return d;
     * }
     * function onResponse(response) {
     *     io.out(response);
     * }
     *
     * // Will print "rejected" after 2 and 4 seconds
     * foo().fail(onResponse).fail(onResponse);
     *
     * @returns A Deferred
     *
     * */
    JSStaticFunction deferred_functions[] = { 
        { "then",             deferred_then,         kJSDefaultAttributes },
        { "resolve",          deferred_resolve,         kJSDefaultAttributes },
        { "reject",           deferred_reject,         kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSStaticValue deferred_values[] = {
        { "isFulfilled",     deferred_is_fulfilled, NULL, kJSDefaultAttributes }, 
        { 0, 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "Deferred"; 
    cd.staticFunctions = deferred_functions;
    cd.staticValues = deferred_values;
    sctx->classes[CLASS_DEFERRED] = JSClassCreate(&cd);
    sctx->constructors[CONSTRUCTOR_DEFERRED] = scripts_create_constructor(sctx->global_context, "Deferred", sctx->classes[CLASS_DEFERRED], deferred_constructor_cb, NULL);
}
