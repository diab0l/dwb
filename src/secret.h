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

#ifndef __DWB_SECRET_H__
#define __DWB_SECRET_H__

#include <libsecret/secret.h>
#include <glib-2.0/glib.h>


typedef void (* dwb_secret_cb)(int, const void *, void *);

enum {
    DWB_SECRET_OK,
    DWB_SECRET_COLLECTION_EXISTS,
    DWB_SECRET_NO_SUCH_COLLECTION,
    DWB_SECRET_SERVICE_ERROR,
    DWB_SECRET_ERROR,
};

void 
dwb_secret_create_collection(dwb_secret_cb cb, const char *name, void *user_data);

void 
dwb_secret_lock_collection(dwb_secret_cb cb, const char *name, void *user_data);

void 
dwb_secret_unlock_collection(dwb_secret_cb cb, const char *name, void *user_data);

void 
dwb_secret_store_pwd(dwb_secret_cb cb, const char *collection, const char *label, 
        const char *id, const char *password, void *user_data);

void 
dwb_secret_lookup_pwd(dwb_secret_cb cb, const char *collection, const char *id, void *user_data);

void 
dwb_secret_check_service(dwb_secret_cb cb, void *user_data);

#endif

#endif
