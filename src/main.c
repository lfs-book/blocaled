/*
  Copyright 2012 Alexandre Rostovtsev

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libdaemon/dfork.h>

#include <glib.h>
#include <gio/gio.h>

#include <rc.h>

#include "hostnamed.h"
#include "localed.h"
#include "timedated.h"
#include "utils.h"

#include "config.h"

#define DEFAULT_LEVELS (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE)

static gboolean debug = FALSE;
static gboolean foreground = FALSE;
static gboolean use_syslog = FALSE;
static gboolean read_only = FALSE;
static gboolean update_rc_status = FALSE;
static gchar *ntp_preferred_service = NULL;

static guint components_started = 0;
G_LOCK_DEFINE_STATIC (components_started);

static gboolean started = FALSE;

static GOptionEntry option_entries[] =
{
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Enable debugging messages", NULL },
    { "foreground", 0, 0, G_OPTION_ARG_NONE, &foreground, "Do not daemonize", NULL },
    { "read-only", 0, 0, G_OPTION_ARG_NONE, &read_only, "Run in read-only mode", NULL },
    { "ntp-service", 0, 0, G_OPTION_ARG_STRING, &ntp_preferred_service, "Preferred rc NTP service for timedated", NULL },
    { "update-rc-status", 0, 0, G_OPTION_ARG_NONE, &update_rc_status, "Force openrc-settingsd rc service to be marked as started", NULL },
    { NULL }
};

static int
log_level_to_syslog (GLogLevelFlags log_level)
{
    switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
    case G_LOG_LEVEL_CRITICAL:
        return LOG_ERR;
    case G_LOG_LEVEL_WARNING:
        return LOG_WARNING;
    case G_LOG_LEVEL_MESSAGE:
        return LOG_NOTICE;
    case G_LOG_LEVEL_INFO:
        return LOG_INFO;
    case G_LOG_LEVEL_DEBUG:
        return LOG_DEBUG;
    }
    return LOG_NOTICE;
}

static void
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
    const gchar *debug_domains = NULL;
    GString *result = NULL;
    gchar *result_data = NULL;

    debug_domains = g_getenv ("G_MESSAGES_DEBUG");
    if (!debug && !(log_level & DEFAULT_LEVELS) && g_strcmp0 (debug_domains, "all") && strstr0 (debug_domains, log_domain) == NULL)
        return;

    result = g_string_new (NULL);
    if (!use_syslog)
        g_string_append_printf (result, "openrc-settingsd[%lu]: ", (gulong)getpid ());
    if (log_domain != NULL)
        g_string_append_printf (result, "%s: ", log_domain);

    if (!use_syslog)
        switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_LEVEL_ERROR:
        case G_LOG_LEVEL_CRITICAL:
            g_string_append (result, "ERROR: ");
            break;
        case G_LOG_LEVEL_WARNING:
            g_string_append (result, "WARNING: ");
            break;
        case G_LOG_LEVEL_MESSAGE:
            g_string_append (result, "Notice: ");
            break;
        case G_LOG_LEVEL_INFO:
            g_string_append (result, "Info: ");
            break;
        case G_LOG_LEVEL_DEBUG:
            g_string_append (result, "Debug: ");
            break;
        }

    if (message != NULL)
        g_string_append (result, message);
    else
        g_string_append (result, "(NULL)");

    result_data = g_string_free (result, FALSE);

    if (use_syslog) {
        openlog ("openrc-settingsd", LOG_PID, LOG_DAEMON);
        syslog (log_level_to_syslog (log_level), "%s", result_data);
    } else
        g_printerr ("%s\n", result_data);

    g_free (result_data);
}

void
openrc_settingsd_exit (int status)
{
    GFile *pidfile = NULL;

    if (!foreground)
        daemon_retval_send (status);

    pidfile = g_file_new_for_path (PIDFILE);
    g_file_delete (pidfile, NULL, NULL);

    if (update_rc_status && started) {
        if (status)
            rc_service_mark ("openrc-settingsd", RC_SERVICE_FAILED);
        else
            rc_service_mark ("openrc-settingsd", RC_SERVICE_STOPPED);
    }

    g_clear_object (&pidfile);
    exit (status);
}

/* This is called each time we successfully grab a bus name when starting up */
void
openrc_settingsd_component_started ()
{
    gchar *pidstring = NULL;
    GError *err = NULL;
    GFile *pidfile = NULL;

    G_LOCK (components_started);

    components_started++;
    /* We want all 3 names (hostnamed, localed, timedated) to be grabbed */
    if (components_started < 3)
        goto out;

    pidfile = g_file_new_for_path (PIDFILE);
    pidstring = g_strdup_printf ("%lu", (gulong)getpid ());
    if (!g_file_replace_contents (pidfile, pidstring, strlen(pidstring), NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, &err)) {
        g_critical ("Failed to write " PIDFILE ": %s", err->message);
        openrc_settingsd_exit (1);
    }

    if (!foreground)
        daemon_retval_send (0);

    if (update_rc_status)
        rc_service_mark ("openrc-settingsd", RC_SERVICE_STARTED);
    started = TRUE;

  out:
    G_UNLOCK (components_started);
    g_clear_object (&pidfile);
    g_free (pidstring);
}

gint
main (gint argc, gchar *argv[])
{
    GError *error = NULL;
    GOptionContext *option_context;
    GMainLoop *loop = NULL;
    pid_t pid;

    g_type_init ();
    g_log_set_default_handler (log_handler, NULL);

    option_context = g_option_context_new ("- system settings D-Bus service for OpenRC");
    g_option_context_add_main_entries (option_context, option_entries, NULL);
    if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
        g_critical ("Failed to parse options: %s", error->message);
        return 1;
    }

    if (!foreground) {
        if (daemon_retval_init () < 0) {
            g_critical ("Failed to create pipe");
            return 1;
        }
        if ((pid = daemon_fork ()) < 0) {
            /* Fork failed */
            daemon_retval_done ();
            return 1;
        } else if (pid) {
            /* Parent */
            int ret;

            /* Wait 20 seconds for daemon_retval_send() in the daemon process */
            if ((ret = daemon_retval_wait (20)) < 0) {
                g_critical ("Timed out waiting for daemon process: %s", strerror(errno));
                return 255;
            } else if (ret > 0) {
                g_critical ("Daemon process returned error code %d", ret);
                return ret;
            }
            return 0;
        }
        /* Daemon */
        use_syslog = TRUE;
        daemon_close_all (-1);
    }

    utils_init ();
    hostnamed_init (read_only);
    localed_init (read_only);
    timedated_init (read_only, ntp_preferred_service);
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    g_main_loop_unref (loop);

    timedated_destroy ();
    localed_destroy ();
    hostnamed_destroy ();
    utils_destroy ();

    g_clear_error (&error);
    g_free (ntp_preferred_service);
    openrc_settingsd_exit (0);
}
