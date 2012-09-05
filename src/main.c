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

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "hostnamed.h"
#include "localed.h"
#include "timedated.h"
#include "utils.h"

#include "config.h"

#define DEFAULT_LEVELS (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE)

static gboolean debug = FALSE;
static gboolean read_only = FALSE;
static gchar *ntp_preferred_service = NULL;

static GOptionEntry option_entries[] =
{
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Enable debugging messages", NULL },
    { "read-only", 0, 0, G_OPTION_ARG_NONE, &read_only, "Run in read-only mode", NULL },
    { "ntp-service", 0, 0, G_OPTION_ARG_STRING, &ntp_preferred_service, "Preferred rc NTP service for timedated", NULL },
    { NULL }
};

/* Emulates the new behavior of g_log_default_handler introduced in glib-2.31.2 */
static void
log_handler (const gchar *log_domain,
             GLogLevelFlags log_level,
             const gchar *message,
             gpointer user_data)
{
    if (debug || log_level & DEFAULT_LEVELS)
        g_log_default_handler (log_domain, log_level, message, user_data);
}

gint
main (gint argc, gchar *argv[])
{
    GError *error = NULL;
    GOptionContext *option_context;
    GMainLoop *loop = NULL;

    g_type_init ();

    option_context = g_option_context_new ("- system settings D-Bus service for OpenRC");
    g_option_context_add_main_entries (option_context, option_entries, NULL);
    if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
        g_printerr ("Failed to parse options: %s\n", error->message);
        exit (1);
    }

    if (glib_check_version (2, 31, 2) == NULL) {
        if (debug)
            g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
    } else
        g_log_set_default_handler (log_handler, NULL);

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
    g_free (ntp_preferred_service);
    return 0;
}
