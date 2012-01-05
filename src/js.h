/*
 * Copyright (c) 2010-2012 Stefan Bolte <portix@gmx.net>
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

#ifndef JS_H
#define JS_H

char * js_string_to_char(JSContextRef ctx, JSStringRef jsstring);
char * js_value_to_char(JSContextRef ctx, JSValueRef value);
JSObjectRef js_get_object_property(JSContextRef ctx, JSObjectRef arg, const char *name);
JSObjectRef js_get_object_property(JSContextRef ctx, JSObjectRef arg, const char *name);
char * js_get_string_property(JSContextRef ctx, JSObjectRef arg, const char *name);
double  js_get_double_property(JSContextRef ctx, JSObjectRef arg, const char *name);

#endif