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

#ifndef __DWB_SCRIPT_DEFERRED_H__
#define __DWB_SCRIPT_DEFERRED_H__

typedef struct DeferredPriv_s 
{
    JSObjectRef reject;
    JSObjectRef resolve;
    JSObjectRef next;
    gboolean is_fulfilled;
} DeferredPriv;

JSObjectRef
deferred_new(JSContextRef ctx);

void 
deferred_destroy(JSContextRef , JSObjectRef , DeferredPriv *);

gboolean 
deferred_fulfilled(JSObjectRef deferred);

JSValueRef 
deferred_then(JSContextRef, JSObjectRef f, JSObjectRef , size_t argc, const JSValueRef argv[], JSValueRef* exc);

JSValueRef 
deferred_resolve(JSContextRef, JSObjectRef f, JSObjectRef , size_t argc, const JSValueRef argv[], JSValueRef* exc);

JSValueRef 
deferred_reject(JSContextRef, JSObjectRef f, JSObjectRef, size_t argc, const JSValueRef argv[], JSValueRef* exc);

JSValueRef 
deferred_is_fulfilled(JSContextRef ctx, JSObjectRef self, JSStringRef js_name, JSValueRef* exception);

#endif
