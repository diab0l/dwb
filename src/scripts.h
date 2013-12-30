/*
 * Copyright (c) 2010-2013 Stefan Bolte <portix@gmx.net>
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

#ifndef __DWB_SCRIPTS_H__
#define __DWB_SCRIPTS_H__

#include <JavaScriptCore/JavaScript.h>

#define SCRIPTS_SIG_FIRST               0
#define SCRIPTS_SIG_NAVIGATION          0
#define SCRIPTS_SIG_LOAD_STATUS         1
#define SCRIPTS_SIG_MIME_TYPE           2
#define SCRIPTS_SIG_DOWNLOAD            3
#define SCRIPTS_SIG_DOWNLOAD_START      4
#define SCRIPTS_SIG_DOWNLOAD_STATUS     5
#define SCRIPTS_SIG_RESOURCE            6
#define SCRIPTS_SIG_KEY_PRESS           7
#define SCRIPTS_SIG_KEY_RELEASE         8
#define SCRIPTS_SIG_BUTTON_PRESS        9
#define SCRIPTS_SIG_BUTTON_RELEASE      10
#define SCRIPTS_SIG_TAB_FOCUS           11
#define SCRIPTS_SIG_FRAME_STATUS        12
#define SCRIPTS_SIG_LOAD_FINISHED       13
#define SCRIPTS_SIG_LOAD_COMMITTED      14
#define SCRIPTS_SIG_CLOSE_TAB           15
#define SCRIPTS_SIG_CREATE_TAB          16
#define SCRIPTS_SIG_FRAME_CREATED       17
#define SCRIPTS_SIG_CLOSE               18
#define SCRIPTS_SIG_DOCUMENT_LOADED     19
#define SCRIPTS_SIG_MOUSE_MOVE          20
#define SCRIPTS_SIG_STATUS_BAR          21
#define SCRIPTS_SIG_TAB_BUTTON_PRESS    22
#define SCRIPTS_SIG_CHANGE_MODE         23
#define SCRIPTS_SIG_EXECUTE_COMMAND     24
#define SCRIPTS_SIG_CONTEXT_MENU        25
#define SCRIPTS_SIG_ERROR               26
#define SCRIPTS_SIG_SCROLL              27
#define SCRIPTS_SIG_FOLLOW              28
#define SCRIPTS_SIG_ADD_COOKIE          29
#define SCRIPTS_SIG_SERVER_RUN          30
#define SCRIPTS_SIG_SERVER_STOP         31
#define SCRIPTS_SIG_LAST                32


#define SCRIPT_MAX_SIG_OBJECTS 8

typedef struct _ScriptSignal {
  JSObjectRef jsobj;
  GObject *objects[SCRIPT_MAX_SIG_OBJECTS]; 
  char *json;
  uint64_t signal;
  int numobj;
  Arg *arg;
} ScriptSignal;

gboolean scripts_emit(ScriptSignal *);
void scripts_create_tab(GList *gl);
void scripts_remove_tab(JSObjectRef );
void scripts_end(gboolean);
void scripts_init_script(const char *, const char *);
void scripts_init_archive(const char *, const char *);
gboolean scripts_init(gboolean);
void scripts_unprotect(JSObjectRef);
DwbStatus scripts_eval_key(KeyMap *m, Arg *arg);
gboolean scripts_execute_one(const char *script);
void scripts_completion_activate(void);
void scripts_reapply(void);
void scripts_check_syntax(char **scripts);
JSObjectRef scripts_make_cookie(SoupCookie *cookie);
gboolean scripts_load_chrome(JSObjectRef,  const char *);
void scripts_load_extension(const char *);

#define EMIT_SCRIPT(sig)  ((dwb.misc.script_signals & (1ULL<<SCRIPTS_SIG_##sig)))
#define SCRIPTS_EMIT_RETURN(signal, json, val) G_STMT_START  \
  if (scripts_emit(&signal)) { \
    g_free(json); \
    return val; \
  } else g_free(json); \
G_STMT_END
#define SCRIPTS_EMIT(signal, json) G_STMT_START  \
  if (scripts_emit(&signal)) { \
    g_free(json); \
    return; \
  } else g_free(json); \
G_STMT_END

#define SCRIPTS_WV(gl) .jsobj = (VIEW(gl)->script_wv)
#define SCRIPTS_SIG_META(js, sig, num) .json = js, .signal = SCRIPTS_SIG_##sig, .numobj = num, .arg = NULL
#define SCRIPTS_SIG_ARG(js, sig, num) .json = js, .signal = SCRIPTS_SIG_##sig, .numobj = num 
#endif
