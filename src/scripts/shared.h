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

#ifndef __DWB_SCRIPT_SHARED_H__
#define __DWB_SCRIPT_SHARED_H__

JSObjectRef 
suri_to_object(JSContextRef ctx, SoupURI *uri, JSValueRef *exception);

JSObjectRef 
make_object_for_class(JSContextRef ctx, int iclass, GObject *o, gboolean protect);

bool
set_property_cb(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception);

JSValueRef 
scripts_call_as_function(JSContextRef ctx, JSObjectRef func, JSObjectRef this, size_t argc, const JSValueRef argv[]);

#endif
