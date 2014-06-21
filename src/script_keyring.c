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

#ifdef WITH_LIBSECRET

#include "script_private.h"

void
on_keyring_no_val(int status, void *unused, JSObjectRef d) {
    (void) unused;
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSValueRef args[] = { JSValueMakeNumber(ctx, status) };
        if (status >= 0) {
            deferred_resolve(ctx, d, d, 1, args, NULL);
        }
        else {
            deferred_reject(ctx, d, d, 1, args, NULL);
        }

        scripts_release_global_context();
    }
}
void 
on_keyring_string(int status, const char *value, JSObjectRef dfd) {
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        if (status >= 0) {
            JSValueRef args[] = { value == NULL ? NIL : js_char_to_value(ctx, value) };
            deferred_resolve(ctx, dfd, dfd, 1, args, NULL);
        }
        else {
            JSValueRef args[] = { JSValueMakeNumber(ctx, status) };
            deferred_reject(ctx, dfd, dfd, 1, args, NULL);
        }

        scripts_release_global_context();
    }
}

static JSValueRef
keyring_call_str(JSContextRef ctx, void (*secret_func)(dwb_secret_cb, const char *, void *), size_t argc, const JSValueRef argv[], JSValueRef *exc) {
    if (argc > 0) {
        char *name = js_value_to_char(ctx, argv[0], -1, exc);
        if (name != NULL) {
            JSObjectRef d = deferred_new(ctx);
            secret_func((dwb_secret_cb)on_keyring_no_val, name, d);
            g_free(name);
            return d;
        }
    }
    return NIL;
}

/**
 * Checks if a service can be retrieved, i.e. gnome-keyring-daemon is installed and
 * running or could be started 
 *
 * @name checkService
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * keyring.checkService().then(
 *      function() {
 *          io.out("Everything's fine");
 *      }, 
 *      function() 
 *          io.out("No service");
 *      }
 * );
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if a service can be retrieved and
 *      rejected if an error occured.
 */
static JSValueRef 
keyring_check_service(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    JSObjectRef d = deferred_new(ctx);
    dwb_secret_check_service((dwb_secret_cb)on_keyring_no_val, d);
    return d;
}
/**
 * Creates a new keyring, if a keyring with the same name already exists no new keyring is created
 *
 * @name create
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * keyring.create("foo").then(function() {
 *     io.out("keyring created");
 * });
 *
 * @param {String} keyring The name of the keyring
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if the keyring was created or a keyring
 *      with matching name already exists and rejected if an error occured
 *
 */
static JSValueRef 
keyring_create(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    return keyring_call_str(ctx, dwb_secret_create_collection, argc, argv, exc);
}
/**
 * Unlocks a keyring
 *
 * @name unlock
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * keyring.unlock("foo").then(function() {
 *     io.out("keyring unlocked");
 * });
 *
 * @param {String} keyring The name of the keyring
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if the keyring was unlocked or rejected
 *      if no such keyring exists or an error occured
 *
 */
static JSValueRef 
keyring_unlock(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    return keyring_call_str(ctx, dwb_secret_unlock_collection, argc, argv, exc);
}
/**
 * Locks a keyring
 *
 * @name lock
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * keyring.lock("foo").then(function() {
 *     io.out("keyring locked");
 * });
 *
 * @param {String} keyring The name of the keyring
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if the keyring was locked or rejected
 *      if no such keyring exists or an error occured
 *
 */
static JSValueRef 
keyring_lock(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    return keyring_call_str(ctx, dwb_secret_lock_collection, argc, argv, exc);
}

/**
 * Stores a password in a keyring
 *
 * @name store
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * var id = script.generateId();
 * keyring.store("foo", "mypassword", id, "secretpassword").then(function() {
 *     io.out("keyring locked");
 * });
 *
 * @param {String} keyring  
 *      The name of the keyring, pass null to use the default keyring
 * @param {String} label    Label for the password
 * @param {String} id       Identifier for the password
 * @param {String} password The password
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if the password was saved or rejected
 *      if no such keyring exists or an error occured
 *
 */
static JSValueRef 
keyring_store(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    JSValueRef ret = NIL;
    JSObjectRef dfd;
    char *collection = NULL, *label = NULL, *id = NULL, *pwd = NULL;

    if (argc < 4) 
        return NIL;

    collection = js_value_to_char(ctx, argv[0], -1, exc);

    label = js_value_to_char(ctx, argv[1], -1, exc);
    if (label == NULL) 
        goto error_out;
    id = js_value_to_char(ctx, argv[2], -1, exc);
    if (id == NULL) 
        goto error_out;
    pwd = js_value_to_char(ctx, argv[3], -1, exc);
    if (pwd == NULL) 
        goto error_out;

    dfd = deferred_new(ctx);
    dwb_secret_store_pwd((dwb_secret_cb)on_keyring_no_val, collection, label, id, pwd, dfd);

    sec_memset(pwd, 0, strlen(pwd));

    ret = dfd;
error_out: 
    g_free(collection);
    g_free(label);
    g_free(id);
    g_free(pwd);
    return ret;
}

/**
 * Get a password from a keyring
 *
 * @name lookup
 * @memberOf keyring
 * @function
 * @since 1.11
 * @example 
 * keyring.lookup("foo", "myid").then(function(password) {
 *     io.out("password: " + password);
 * });
 *
 * @param {String} keyring  
 *      The name of the keyring, pass null to use the default keyring
 * @param {String} id       
 *      Identifier of the password
 *
 * @returns {Deferred} 
 *      A deferred that will be resolved if the password was found or rejected
 *      if no such keyring exists, the password wasn't found or an error occured
 *
 */
static JSValueRef 
keyring_lookup(JSContextRef ctx, JSObjectRef f, JSObjectRef thisObject, size_t argc, const JSValueRef argv[], JSValueRef* exc)  {
    JSValueRef ret = NIL;
    JSObjectRef dfd;
    char *collection = NULL, *id = NULL;

    if (argc < 2) 
        return NIL;

    collection = js_value_to_char(ctx, argv[0], -1, exc);

    id = js_value_to_char(ctx, argv[1], -1, exc);
    if (id == NULL) 
        goto error_out;

    dfd = deferred_new(ctx);
    dwb_secret_lookup_pwd((dwb_secret_cb)on_keyring_string, collection, id, dfd);
    ret = dfd;
error_out: 
    g_free(collection);
    g_free(id);
    return ret;
}
JSObjectRef 
keyring_initialize(JSContextRef ctx) {
    /**
     * Namespace for managing passwords, needs gnome-keyring
     * @namespace 
     *      Access to gnome-keyring
     * @name keyring 
     * @static 
     * @since 1.11
     * @example
     *
     * //!javascript
     * 
     * var keyring = namespace("keyring");
     * 
     * var collection = "foo";
     * var id = script.generateId();
     * 
     * // creates a new keyring if it doesn't exist and saves a new password
     * keyring.create(collection).then(function(status) {
     *     return keyring.store(collection, "foobar", id, "secret");
     * }).then(function(status) {
     *     io.notify("Password saved");
     * });
     *
     * */
    JSStaticFunction keyring_functions[] = { 
        { "checkService",            keyring_check_service,       kJSDefaultAttributes },
        { "create",                  keyring_create,       kJSDefaultAttributes },
        { "lock",                    keyring_lock,       kJSDefaultAttributes },
        { "unlock",                  keyring_unlock,       kJSDefaultAttributes },
        { "store",                   keyring_store,       kJSDefaultAttributes },
        { "lookup",                  keyring_lookup,      kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };
    JSClassRef klass = scripts_create_class("keyring", keyring_functions, NULL, NULL);
    JSObjectRef ret = JSObjectMake(ctx, klass, NULL);
    JSClassRelease(klass);
    return ret;
}
#endif // WITH_LIBSECRET
