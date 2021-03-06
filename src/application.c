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

#include <gtk/gtk.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include "dwb.h"
#include "view.h"
#include "session.h"
#include "util.h"
#include "scripts.h"
#include "application.h"
#include <gio-unix-2.0/gio/gdesktopappinfo.h>

static gboolean application_parse_option(const gchar *, const gchar *, gpointer , GError **);
static void application_execute_args(char **);
static GOptionContext * application_get_option_context(void);
static void application_start(GApplication *, char **);

/* Option parsing arguments  {{{ */
static gboolean s_opt_list_sessions = false;
static gboolean s_opt_single = false;
static gboolean s_opt_override_restore = false;
static gboolean s_opt_version = false;
static gboolean s_opt_version_full = false;
static gboolean s_opt_force = false;
static gboolean s_opt_fallback = false;
static gboolean s_opt_enable_scripts = false;
static gchar **s_opt_delete_profile = NULL;
static gchar *s_opt_restore = NULL;
static gchar **s_opt_exe = NULL;
static gchar **s_opt_temp_profile = false;
static gchar **s_scripts;
static gchar **s_run_scripts;
static GIOChannel *s_fallback_channel;
static GOptionEntry options[] = {
    { "check-script", 'c', 0, G_OPTION_ARG_FILENAME_ARRAY, &s_scripts, "Check script syntax of scripts", "script"},
    { "run-script", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &s_run_scripts, "Check script syntax of scripts", "script"},
    { "embed", 'e', 0, G_OPTION_ARG_INT64, &dwb.gui.wid, "Embed into window with window id wid", "wid"},
    { "force", 'f', 0, G_OPTION_ARG_NONE, &s_opt_force, "Force restoring a saved session, even if another process has restored the session", NULL },
    { "list-sessions", 'l', 0, G_OPTION_ARG_NONE, &s_opt_list_sessions, "List saved sessions and exit", NULL },
    { "fifo", 0, 0, G_OPTION_ARG_NONE, &s_opt_fallback, "Use a fifo for single-instance instead of dbus", NULL },
    { "new-instance", 'n', 0, G_OPTION_ARG_NONE, &s_opt_single, "Open a new instance, overrides 'single-instance'", NULL},
    { "restore", 'r', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &application_parse_option, "Restore session with name 'sessionname' or default if name is omitted", "sessionname"},
    { "override-restore", 'R', 0, G_OPTION_ARG_NONE, &s_opt_override_restore, "Don't restore last session even if 'save-session' is set", NULL},
    { "profile", 'p', 0, G_OPTION_ARG_STRING, &dwb.misc.profile, "Load configuration for 'profile'", "profile" },
    { "temp-profile", 't', 0, G_OPTION_ARG_NONE, &s_opt_temp_profile, "Use a fresh temporary profile", NULL },
    { "execute", 'x', 0, G_OPTION_ARG_STRING_ARRAY, &s_opt_exe, "Execute commands", NULL},
    { "version", 'v', 0, G_OPTION_ARG_NONE, &s_opt_version, "Show version information and exit", NULL},
    { "version-full", 'V', 0, G_OPTION_ARG_NONE, &s_opt_version_full, "Show detailed version information and exit", NULL},
    { "enable-scripts", 'S', 0, G_OPTION_ARG_NONE, &s_opt_enable_scripts, "Enable javascript api", NULL},
    { "delete-profile", 'd', 0, G_OPTION_ARG_STRING_ARRAY, &s_opt_delete_profile, "Deletes a profile", NULL},
    { "set-as-default", 0, G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, &application_parse_option, "Sets dwb as default browser", NULL},
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};
static GOptionContext *option_context;
/* }}} */

/* DwbApplication derived from GApplication {{{*/
#define DWB_TYPE_APPLICATION            (dwb_application_get_type ())
#define DWB_APPLICATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DWB_TYPE_APPLICATION, DwbApplication))
#define DWB_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DWB_TYPE_APPLICATION, DwbApplicationClass))
#define DWB_IS_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DWB_TYPE_APPLICATION))
#define DWB_IS_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DWB_TYPE_APPLICATION))
#define DWB_APPLICATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DWB_TYPE_APPLICATION, DwbApplicationClass))

typedef struct _DwbApplication        DwbApplication;
typedef struct _DwbApplicationClass   DwbApplicationClass;
typedef struct _DwbApplicationPrivate DwbApplicationPrivate;

struct _DwbApplication
{
    GApplication parent;
};

struct _DwbApplicationClass
{
    GApplicationClass parent_class;
};
G_DEFINE_TYPE(DwbApplication, dwb_application, G_TYPE_APPLICATION);

static DwbApplication *s_app;

static void
dwb_application_main(GApplication *app) 
{
    gtk_main();
}
static void
dwb_application_main_quit(GApplication *app) 
{
    gtk_main_quit();
}
static gint 
dwb_application_command_line(GApplication *app, GApplicationCommandLine *cl) 
{
    gint argc;
    gchar **argv = g_application_command_line_get_arguments(cl, &argc);

    GOptionContext *c = application_get_option_context();
    if (!g_option_context_parse(c, &argc, &argv, NULL)) 
        return 1;
    
    application_execute_args(argv);
    return 0;
}


/* dwb_handle_channel {{{*/
static gboolean
application_handle_channel(GIOChannel *c, GIOCondition condition) 
{
    char *line = NULL;

    g_io_channel_read_line(c, &line, NULL, NULL, NULL);
    if (line) 
    {
        g_strstrip(line);
        dwb_parse_commands(line);
        g_io_channel_flush(c, NULL);
        g_free(line);
    }
    return true;
}/*}}}*/

static char *
application_local_path(const char *file) 
{
    char *path = NULL;
    if ( g_file_test(file, G_FILE_TEST_EXISTS) && !g_path_is_absolute(file)) 
    {
        char *curr_dir = g_get_current_dir();
        path = g_build_filename(curr_dir, file, NULL);
        g_free (curr_dir);
    }
    return path;

}

static gboolean
dwb_application_local_command_line(GApplication *app, gchar ***argv, gint *exit_status) 
{
    GError *error = NULL;
    *exit_status = 0;
    gboolean remote = false, single_instance;
    gint argc_remain, argc_exe = 0;
    gint argc = g_strv_length(*argv);
    gint i, count;
    gchar **restore_args;
    gint fd = -1;
    FILE *ff = NULL; 
    gchar *path, *unififo = NULL;
    char *appid;
    GDBusConnection *bus = NULL;

    GOptionContext *c = application_get_option_context();
    if (!g_option_context_parse(c, &argc, argv, &error)) 
    {
        fprintf(stderr, "Error parsing command line options: %s\n", error->message);
        *exit_status = 1;
        return true;
    }
    if (!s_opt_fallback) {
        appid = g_strconcat("org.bitbucket.dwb.", dwb.misc.profile, NULL);
        g_application_set_application_id(app, appid);
        g_free(appid);
    }

    argc_remain = g_strv_length(*argv);

    if (s_opt_exe != NULL)
        argc_exe = g_strv_length(s_opt_exe);

    if (s_opt_delete_profile != NULL)
    {
        for (int i=0; s_opt_delete_profile[i]; i++)
        {
            if (!dwb_delete_profile(s_opt_delete_profile[i]))
            {
                fprintf(stderr, "Deleting profile \"%s\" failed\n", s_opt_delete_profile[i]);
            }
        }
        return true;
    }
    if (s_opt_temp_profile)
    {
        dwb.misc.profile = g_strdup_printf("%"PRId64, g_get_monotonic_time());
    }
    if (s_opt_list_sessions) 
    {
        session_list();
        return true;
    }
    if (s_opt_version_full)
    {
        dwb_version();
        dwb_version_libs();
        return true;
    }
    if (s_opt_version) 
    {
        dwb_version();
        return true;
    }
    if (s_scripts)
    {
        scripts_check_syntax(s_scripts);
        return true;
    }
    if (s_run_scripts)
    {
        scripts_run_scripts(s_run_scripts);
        return true;
    }
    dwb_init_vars();
    dwb_init_files();
    dwb_init_settings();

    single_instance = GET_BOOL("single-instance");
    if (!s_opt_fallback) {
        if (s_opt_single || !single_instance) 
            g_application_set_flags(app, G_APPLICATION_NON_UNIQUE);
#if GLIB_CHECK_VERSION(2, 40, 0)
        else {
            int flags = g_application_get_flags(app);
            g_application_set_flags(app, flags | G_APPLICATION_HANDLES_COMMAND_LINE);
        }
#endif

        bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    }
    if (bus != NULL && g_application_register(app, NULL, &error)) 
    { 
        g_object_unref(bus);
        remote = g_application_get_is_remote(app);
        if (remote) 
        {
            /* Restore executable args */
            if (argc_exe > 0 || argc_remain > 1) 
            {
                restore_args = g_malloc0_n(argc_exe*2 + argc_remain + 1, sizeof(char*));
                if (restore_args == NULL)
                    return false;

                count = i = 0;
                restore_args[count++] = g_strdup((*argv)[0]);
                if (s_opt_exe != NULL) 
                {
                    for (; s_opt_exe[i] != NULL; i++) 
                    {
                        restore_args[2*i + count] = g_strdup("-x");
                        restore_args[2*i + 1 + count] = s_opt_exe[i];
                    }
                }
                for (; (*argv)[count]; count++) 
                {
                    if ( (path = application_local_path((*argv)[count]))) 
                        restore_args[2*i+count] = path;
                    else 
                        restore_args[2*i+count] = g_strdup((*argv)[count]);
                }
                restore_args[2*i+count] = NULL;
                g_strfreev(*argv);
                *argv = restore_args;
            }
            else if (argc_remain == 1) 
            {
                application_start(app, *argv);
                return true;
            }
            return false;
        }
    }
    /* Fallback */
    else 
    {
        if (!s_opt_fallback)
        {
            fprintf(stderr, "D-Bus-registration failed, using fallback mode\n");
        }
        if (!single_instance) 
        {
            application_start(app, *argv);
            return true;
        }
        else 
        {
            path = util_build_path();
            unififo = g_build_filename(path, dwb.misc.profile, "dwb-uni.fifo", NULL);
            g_free(path);

            if (! g_file_test(unififo, G_FILE_TEST_EXISTS)) 
                mkfifo(unififo, 0600);

            fd = open(unififo, O_WRONLY | O_NONBLOCK);

            if ( (ff = fdopen(fd, "w")) )
                remote = true;

            if ( remote ) 
            {
                if (argc_remain > 1 || s_opt_exe != NULL) 
                {
                    for (int i=1; (*argv)[i]; i++) 
                    {
                        if ( (path = application_local_path((*argv)[i])) ) 
                        {
                            fprintf(ff, "tabopen %s\n", path);
                            g_free (path);
                        }
                        else 
                            fprintf(ff, "tabopen %s\n", (*argv)[i]);
                    }
                    if (s_opt_exe != NULL) 
                    {
                        for (int i = 0; s_opt_exe[i]; i++) 
                            fprintf(ff, "%s\n", s_opt_exe[i]);
                    }
                    goto clean;
                }
            }
            else 
            {
                GIOChannel *s_fallback_channel = g_io_channel_new_file(unififo, "r+", NULL);
                g_io_add_watch(s_fallback_channel, G_IO_IN, (GIOFunc)application_handle_channel, NULL);
            }
        }
    }
    if (GET_BOOL("save-session") && !remote && !s_opt_single)
        s_opt_force = true;

    application_start(app, *argv);
clean:
    if (ff != NULL)
        fclose(ff);
    if(fd > 0)
        close(fd);
    g_free(unififo);
    return true;
}

static void
dwb_application_class_init (DwbApplicationClass *class)
{
    GApplicationClass *app_class = G_APPLICATION_CLASS (class);

    app_class->run_mainloop = dwb_application_main;
    app_class->quit_mainloop = dwb_application_main_quit;
    app_class->local_command_line = dwb_application_local_command_line;
    app_class->command_line = dwb_application_command_line;
}
static void
dwb_application_init(DwbApplication *app) 
{
    (void)app;
}

static DwbApplication *
dwb_application_new (const gchar *id, GApplicationFlags flags)
{
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init ();
#endif
    return g_object_new (DWB_TYPE_APPLICATION, "application-id", id, "flags", flags, NULL);
}/*}}}*/

static void /* application_execute_args(char **argv) {{{*/
application_execute_args(char **argv) 
{
    static int offset = 0;
    if (argv != NULL && argv[1] != NULL) 
    {
        for (int i=1; argv[i] != NULL; i++) 
            view_add(argv[i], false);
    }
    if (s_opt_exe != NULL) 
    {
        int length = g_strv_length(s_opt_exe);
        for (int i=offset; i<length; i++) 
            dwb_parse_commands(s_opt_exe[i]);

        offset = length;
    }
}/*}}}*/

static void /* application_start(GApplication *app, char **argv) {{{*/
application_start(GApplication *app, char **argv) 
{
    gboolean restored = false;
    int session_flags = 0;
    if (argv == NULL)
        return;

    gtk_init(NULL, NULL);
    dwb_init();

    dwb_pack(GET_CHAR("widget-packing"), false);
    scripts_init(s_opt_enable_scripts);

    if (s_opt_force) 
        session_flags |= SESSION_FORCE;

    /* restore session */ 
    if (! s_opt_override_restore) 
    {
        if (GET_BOOL("save-session") || s_opt_restore != NULL) 
            restored = session_restore(s_opt_restore, session_flags);
    }
    else 
        session_restore(s_opt_restore, session_flags | SESSION_ONLY_MARK);

    if ((! restored && g_strv_length(argv) == 1)) 
    {
        view_add(NULL, false);
        dwb_open_startpage(dwb.state.fview);
    }
    dwb_init_auto_started_files();

    application_execute_args(argv);
    /*  Compute bar height */
    gint bar_height;
    gint w = 0, h = 0;
    PangoContext *pctx = gtk_widget_get_pango_context(VIEW(dwb.state.fview)->tablabel);
    PangoLayout *layout = pango_layout_new(pctx);
    pango_layout_set_text(layout, "a", -1);
    pango_layout_set_font_description(layout, dwb.font.fd_active);
    pango_layout_get_size(layout, &w, &h);
    bar_height = h/PANGO_SCALE;
    g_object_unref(layout);
    if (dwb.misc.statusbar_height <= 0) {
        dwb.misc.statusbar_height = bar_height;
    }
    if (dwb.misc.tabbar_height <= 0) {
        dwb.misc.tabbar_height = bar_height;
    }
    else if (dwb.misc.favicon_size > 0) {
        dwb.misc.tabbar_height = MAX(dwb.misc.tabbar_height, dwb.misc.favicon_size);
    }
    gtk_widget_set_size_request(dwb.gui.entry, -1, bar_height);

    if (dwb.misc.favicon_size == 0) {
        dwb.misc.favicon_size = dwb.misc.tabbar_height;
    }

    dwb_init_signals();

    /**
     * Emitted once after dwb has been fully initialized. some functions can only be
     * called after this signal has been emitted, e.g. execute.
     * @event ready
     * @memberOf signals
     * @param {signals~onReady} callback
     *      Callback function that will be called when the signal is emitted
     * */
    /**
     * Callback called when dwb has been fully initialized
     * @callback signals~onReady
     * */

    if (EMIT_SCRIPT(READY)) {
        ScriptSignal signal = { NULL, SCRIPTS_SIG_META(NULL, READY, 0) };
        scripts_emit(&signal);
    }

    
    g_application_hold(app);
}/*}}}*/

static gboolean 
application_set_default(const char *text)
{
    gboolean ret = false;
    GDesktopAppInfo *info = g_desktop_app_info_new("dwb.desktop");
    char **token = NULL;
    if (info == NULL)
    {
        dwb_set_error_message(dwb.state.fview, "No desktop file found");
        return ret;
    }
    if (text == NULL || *text == '\0') 
    {
        g_app_info_set_as_default_for_type(G_APP_INFO(info), "text/html", NULL);
        g_app_info_set_as_default_for_type(G_APP_INFO(info), "text/xml", NULL);
        g_app_info_set_as_default_for_type(G_APP_INFO(info), "application/xhtml+xml", NULL);
        g_app_info_set_as_default_for_type(G_APP_INFO(info), "x-scheme-handler/http", NULL);
        g_app_info_set_as_default_for_type(G_APP_INFO(info), "x-scheme-handler/https", NULL);
        ret = true;
    }
    else 
    {
        token = g_strsplit(text, " ", -1);
        if (token[0] != NULL && token[1] != NULL) 
        {
            if (!g_strcmp0(token[0], "mimetype"))
            {
                for (int i=1; token[i]; i++)
                    g_app_info_set_as_default_for_type(G_APP_INFO(info), token[i], NULL);
                ret = true;
            }
            else if (!g_strcmp0(token[0], "extension"))
            {
                for (int i=1; token[i]; i++)
                    g_app_info_set_as_default_for_extension(G_APP_INFO(info), token[i], NULL);
                ret = true;
            }
        }
        g_strfreev(token);
    }
    g_object_unref(info);
    return ret;
}

static GOptionContext * /* application_get_option_context(void) {{{*/
application_get_option_context(void) 
{
    if (option_context == NULL) 
    {
        option_context = g_option_context_new("[url]");
        g_option_context_add_main_entries(option_context, options, NULL);
        g_option_context_add_group(option_context, gtk_get_option_group(true));
    }
    return option_context;
}/*}}}*/

static gboolean /* application_parse_option(const gchar *key, const gchar *value, gpointer data, GError **error) {{{*/
application_parse_option(const gchar *key, const gchar *value, gpointer data, GError **error) 
{
    if (!g_strcmp0(key, "--set-as-default"))
    {
        if (!application_set_default(value))
        {
            fprintf(stderr, "Invalid option for --set-as-default\n");
            exit(EXIT_FAILURE);
        }
        else 
        {
            exit(EXIT_SUCCESS);
        }

    }
    if (!g_strcmp0(key, "-r") || !g_strcmp0(key, "--restore")) 
    {
        if (value != NULL) 
            s_opt_restore = g_strdup(value);
        else 
            s_opt_restore = g_strdup("default");
        return true;
    }
    else 
        g_set_error(error, 0, 1, "Invalid option : %s", key);

    return false;
}/*}}}*/

void /* application_stop() {{{*/
application_stop(void) 
{
    if (s_fallback_channel != NULL) 
    {
        g_io_channel_shutdown(s_fallback_channel, true, NULL);
        g_io_channel_unref(s_fallback_channel);
    }
    if (s_opt_temp_profile)
    {
        dwb_delete_profile(dwb.misc.profile);
    }
    g_application_release(G_APPLICATION(s_app));
}/*}}}*/

gint /* application_run(gint, char **) {{{*/
application_run(gint argc, gchar **argv) 
{
    s_app = dwb_application_new("org.bitbucket.dwb", 0);
    gint ret = g_application_run(G_APPLICATION(s_app), argc, argv);
    g_object_unref(s_app);

    return ret;
}/*}}}*/
