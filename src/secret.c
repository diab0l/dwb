/*
 * Copyright (c) 2014 Stefan Bolte <portix@gmx.net>
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "secret.h"

static const SecretSchema s_schema = {
    "org.bitbucket.dwb.secrets", SECRET_SCHEMA_NONE, {
        { "id", SECRET_SCHEMA_ATTRIBUTE_STRING }, 
        { NULL, 0 },
    }
};

enum {
    DWB_SECRET_ACTION_NONE, 
    DWB_SECRET_ACTION_STORE, 
    DWB_SECRET_ACTION_LOOKUP, 
    DWB_SECRET_ACTION_LOCK, 
    DWB_SECRET_ACTION_UNLOCK, 
};

typedef struct dwb_secret_s dwb_secret_t;
typedef struct dwb_pwd_s dwb_pwd_t;

struct dwb_secret_s {
    void *data;
    void *user_data;
    void (*free_func)(void*);
    dwb_secret_cb cb;
    int action;
};

struct dwb_pwd_s {
    char *collection;
    char *label;
    char *id;
    char *pwd; 
};
#define get_service_init(cb, s) secret_service_get(SECRET_SERVICE_LOAD_COLLECTIONS | SECRET_SERVICE_OPEN_SESSION, \
        NULL, (GAsyncReadyCallback)cb, s)


static dwb_pwd_t * 
dwb_pwd_new(const char *collection, const char *label, const char *id, const char *password) {
    dwb_pwd_t *pwd = g_malloc0(sizeof(dwb_pwd_t));
    if (collection != NULL) 
        pwd->collection = g_strdup(collection);
    if (label != NULL) 
        pwd->label = g_strdup(label);
    if (id != NULL) 
        pwd->id = g_strdup(id);
    if (pwd != NULL) 
        pwd->pwd = g_strdup(password);
    return pwd;
}

static void
dwb_pwd_free(dwb_pwd_t *pwd) {
    g_return_if_fail(pwd != NULL);
    g_free(pwd->collection);
    g_free(pwd->label);
    g_free(pwd->id);
    if (pwd->pwd != NULL) {
        secret_password_free(pwd->pwd);
    }
    g_free(pwd);
}

static dwb_secret_t *
dwb_secret_new(dwb_secret_cb cb, void *data, void *user_data, void (*free_func)(void *), int action) {

    dwb_secret_t *secret = g_malloc0(sizeof(dwb_secret_t));

    secret->data = data;
    secret->user_data = user_data;
    secret->free_func = free_func;
    secret->cb = cb;
    secret->action = action;
    return secret;
}

static void 
dwb_secret_free(dwb_secret_t *s) {
    g_return_if_fail(s != NULL);
    if (s->data != NULL && s->free_func != NULL) {
        s->free_func(s->data);
    }
    g_free(s);

}

static void
invoke(dwb_secret_t *s, int state, const void *data) {
    g_return_if_fail(s != NULL);
    if (s->cb != NULL) {
        s->cb(state, data, s->user_data);
    }
    dwb_secret_free(s);
}

static SecretService *
get_service(dwb_secret_t *secret, GAsyncResult *result)  {
    GError *e = NULL;
    SecretService *service = secret_service_get_finish(result, &e);
    if (e != NULL) {
        invoke(secret, DWB_SECRET_SERVICE_ERROR, NULL);
        g_error_free(e);
    }
    return service;
}

static SecretCollection *
collection_first_matching(SecretService *service, const char *name) {
    SecretCollection *ret = NULL;
    GList *collections = secret_service_get_collections(service);
    for (GList *l = collections; l != NULL && ret == NULL; l=l->next) {
        SecretCollection *collection = l->data;
        char *label = secret_collection_get_label(collection);
        if (!g_strcmp0(name, label)) {
            ret = g_object_ref(collection);
        }
    }
    g_list_free_full(collections, g_object_unref);
    return ret;
}

static const char *
collection_lookup_path(SecretService *service, const char *name) {
    const char *ret = NULL;
    SecretCollection *collection = collection_first_matching(service, name);
    if (collection != NULL) {
        ret = g_dbus_proxy_get_object_path((GDBusProxy *) collection);
        g_object_unref(collection);
    }
    return ret;
}

/*
 * create collection
 * */
static void 
on_created_collection(GObject *o, GAsyncResult *result, dwb_secret_t *secret) {
    GError *e = NULL;
    SecretCollection *collection = secret_collection_create_finish(result, &e);
    if (e == NULL) {
        invoke(secret, DWB_SECRET_OK, NULL);
        g_object_unref(collection);
    }
    else {
        invoke(secret, DWB_SECRET_ERROR, NULL);
        g_clear_error(&e);
    }
}
static void 
on_service_create_collection(GObject *source, GAsyncResult *result, dwb_secret_t *secret) {
    SecretService *service = get_service(secret, result);
    if (service != NULL) {
        SecretCollection *collection = collection_first_matching(service, secret->data);
        if (collection == NULL) {
            secret_collection_create(service, secret->data, NULL, SECRET_COLLECTION_CREATE_NONE, NULL, 
                    (GAsyncReadyCallback)on_created_collection, secret);
        }
        else {
            invoke(secret, DWB_SECRET_COLLECTION_EXISTS, NULL);
            g_object_unref(collection);
        }
        g_object_unref(service);
    }
}
void 
dwb_secret_create_collection(dwb_secret_cb cb, const char *name, void *user_data) {
    dwb_secret_t *s = dwb_secret_new(cb, g_strdup(name), user_data, g_free, DWB_SECRET_ACTION_NONE);
    get_service_init(on_service_create_collection, s);
}
/*
 * lock
 */

static void 
on_lock_unlock_collection(SecretService *service, GAsyncResult *result, dwb_secret_t *secret) {
    GError *e = NULL;
    int status = DWB_SECRET_OK;

    if (secret->action == DWB_SECRET_ACTION_LOCK) {
        secret_service_lock_finish(service, result, NULL, &e);
    }
    else {
        secret_service_unlock_finish(service, result, NULL, &e);
    }
    if (e != NULL) {
        status = DWB_SECRET_ERROR;
        g_error_free(e);
    }

    invoke(secret, status, NULL);
}

static void 
on_service_lock_unlock_collection(GObject *source, GAsyncResult *result, dwb_secret_t *secret) {
    SecretService *service = get_service(secret, result);
    if (service == NULL) 
        return;

    GList *cols = NULL;
    GList *collections = secret_service_get_collections(service);
    gboolean lock = secret->action == DWB_SECRET_ACTION_LOCK;

    for (GList *l = collections; l != NULL; l=l->next) {
        SecretCollection *collection = l->data;
        gboolean locked = secret_collection_get_locked(collection);
        if ((lock && !locked) || (!lock && locked)) {
            char *label = secret_collection_get_label(collection);
            if (!g_strcmp0(secret->data, label)) {
                cols = g_list_append(cols, collection);
            }
            g_free(label);
        }
    }
    if (cols != NULL) {
        if (!lock) 
            secret_service_unlock(service, cols, NULL, (GAsyncReadyCallback)on_lock_unlock_collection, secret);
        else 
            secret_service_lock(service, cols, NULL, (GAsyncReadyCallback)on_lock_unlock_collection, secret);
    }
    else {
        invoke(secret, DWB_SECRET_NO_SUCH_COLLECTION, NULL);
    }
    if (collections != NULL) {
        g_list_free_full(collections, g_object_unref);
    }
    g_object_unref(service);
}

void 
dwb_secret_lock_collection(dwb_secret_cb cb, const char *name, void *user_data) {
    dwb_secret_t *s = dwb_secret_new(cb, g_strdup(name), user_data, g_free, DWB_SECRET_ACTION_LOCK);
    get_service_init(on_service_lock_unlock_collection, s);
}
void 
dwb_secret_unlock_collection(dwb_secret_cb cb, const char *name, void *user_data) {
    dwb_secret_t *s = dwb_secret_new(cb, g_strdup(name), user_data, g_free, DWB_SECRET_ACTION_UNLOCK);
    get_service_init(on_service_lock_unlock_collection, s);
}

/*  
 *  store
 * */

static void 
on_store_pwd(GObject *o, GAsyncResult *result, dwb_secret_t *secret) {
    gboolean success = secret_password_store_finish(result, NULL);
    invoke(secret, success ? DWB_SECRET_OK : DWB_SECRET_ERROR, NULL);
}
static void 
on_lookup_pwd(GObject *o, GAsyncResult *result, dwb_secret_t *secret) {
    GError *e = NULL;
    char *pwd = secret_password_lookup_finish(result, &e);
    if (e == NULL) {
        invoke(secret, DWB_SECRET_OK, pwd);
        secret_password_free(pwd);
    }
    else {
        invoke(secret, DWB_SECRET_ERROR, NULL);
        g_error_free(e);
    }
}

static void 
on_service_pwd(GObject *source, GAsyncResult *result, dwb_secret_t *secret) {
    SecretService *service = get_service(secret, result);
    if (service != NULL) {
        dwb_pwd_t *pwd = secret->data;
        const char *path = collection_lookup_path(service, pwd->collection);
        if (path != NULL) {
            if (secret->action == DWB_SECRET_ACTION_STORE) {
                secret_password_store(&s_schema, path, pwd->label, pwd->pwd, NULL,
                        (GAsyncReadyCallback)on_store_pwd, secret, "id", pwd->id, NULL);
            }
            else {
                secret_password_lookup(&s_schema, NULL, (GAsyncReadyCallback)on_lookup_pwd, secret, "id", pwd->id, NULL);
            }
        }
        else {
            invoke(secret, DWB_SECRET_NO_SUCH_COLLECTION, NULL);
        }
        g_object_unref(service);
    }
}

void 
dwb_secret_store_pwd(dwb_secret_cb cb, const char *collection, 
        const char *label, const char *id, const char *password, void *user_data) {
    dwb_pwd_t *pwd = dwb_pwd_new(collection, label, id, password);
    dwb_secret_t *s = dwb_secret_new(cb, pwd, user_data, (void (*)(void *))dwb_pwd_free, DWB_SECRET_ACTION_STORE);
    get_service_init(on_service_pwd, s);
}

void 
dwb_secret_lookup_pwd(dwb_secret_cb cb, const char *collection, const char *id, void *user_data) {
    dwb_pwd_t *pwd = dwb_pwd_new(collection, NULL, id, NULL);
    dwb_secret_t *s = dwb_secret_new(cb, pwd, user_data, (void (*)(void *))dwb_pwd_free, DWB_SECRET_ACTION_LOOKUP);
    get_service_init(on_service_pwd, s);
}

void 
on_service_check_service(GObject *o, GAsyncResult *result, dwb_secret_t *secret) {
    SecretService *service = get_service(secret, result);
    if (service != NULL) {
        invoke(secret, DWB_SECRET_OK, NULL);
        g_object_unref(service);
    }
}
void 
dwb_secret_check_service(dwb_secret_cb cb, void *user_data) {
    dwb_secret_t *s = dwb_secret_new(cb, NULL, user_data, NULL, DWB_SECRET_ACTION_NONE);
    get_service_init(on_service_check_service, s);
}
#endif
