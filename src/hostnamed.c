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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus-protocol.h>
#include <glib.h>
#include <gio/gio.h>
#include <polkit/polkit.h>

#include "hostnamed.h"
#include "hostnamed-generated.h"
#include "bus-utils.h"
#include "shell-utils.h"

#include "config.h"

#define QUOTE(macro) #macro
#define STR(macro) QUOTE(macro)

guint bus_id = 0;
gboolean read_only = FALSE;

static OpenrcSettingsdHostnamedHostname1 *hostname1 = NULL;

static gchar hostname[HOST_NAME_MAX + 1];
static GStaticMutex hostname_mutex = G_STATIC_MUTEX_INIT;
static gchar *static_hostname = NULL;
static GStaticMutex static_hostname_mutex = G_STATIC_MUTEX_INIT;
static gchar *pretty_hostname = NULL;
static gchar *icon_name = NULL;
static GStaticMutex machine_info_mutex = G_STATIC_MUTEX_INIT;

static gboolean
hostname_is_valid (const gchar *name)
{
    if (name == NULL)
        return FALSE;

    return g_regex_match_simple ("^[a-zA-Z0-9_.-]{1," STR(HOST_NAME_MAX) "}$",
                                 name, G_REGEX_MULTILINE, 0);
}

static void
guess_icon_name ()
{
    /* TODO: detect virtualization by reading rc_sys */

#if defined(__i386__) || defined(__x86_64__)
    /* 
       Taken with a few minor changes from systemd's hostnamed

       See the SMBIOS Specification 2.7.1 section 7.4.1 for
       details about the values listed here:

       http://www.dmtf.org/sites/default/files/standards/documents/DSP0134_2.7.1.pdf
    */
    gchar *contents;

    if (g_file_get_contents ("/sys/class/dmi/id/chassis_type", &contents, NULL, NULL)) {
        switch (g_ascii_strtoull (contents, NULL, 10)) {
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            icon_name = g_strdup ("computer-desktop");
            return;
        case 0x9:
        case 0xA:
        case 0xE:
            icon_name = g_strdup ("computer-laptop");
            return;
        case 0x11:
        case 0x17:
        case 0x1C:
        case 0x1D:
            icon_name = g_strdup ("computer-server");
            return;
        }
    }
#endif
    icon_name = g_strdup ("computer");
}

static gboolean
on_handle_set_hostname (OpenrcSettingsdHostnamedHostname1 *hostname1,
                        GDBusMethodInvocation *invocation,
                        const gchar *name,
                        const gboolean user_interaction,
                        gpointer user_data)
{
    GError *err = NULL;

    if (read_only) {
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
        goto end;
    }

    if (!check_polkit (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-hostname", user_interaction, &err)) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        goto end;
    }

    g_static_mutex_lock (&hostname_mutex);
    /* Don't allow an empty or invalid hostname */
    if (!hostname_is_valid (name)) {
        name = hostname;
        if (!hostname_is_valid (name))
            name = "localhost";
    }
    if (sethostname (name, strlen(name))) {
        int errsv = errno;
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_FAILED,
                                                    strerror (errsv));
        g_static_mutex_unlock (&hostname_mutex);
        goto end;
    }
    g_strlcpy (hostname, name, HOST_NAME_MAX + 1);
    openrc_settingsd_hostnamed_hostname1_complete_set_hostname (hostname1, invocation);
    openrc_settingsd_hostnamed_hostname1_set_hostname (hostname1, hostname);
    g_static_mutex_unlock (&hostname_mutex);

  end:
    return TRUE;
}

static gboolean
on_handle_set_static_hostname (OpenrcSettingsdHostnamedHostname1 *hostname1,
                               GDBusMethodInvocation *invocation,
                               const gchar *name,
                               const gboolean user_interaction,
                               gpointer user_data)
{
    ShellUtilsTrivial *confd_file = NULL;
    GError *err = NULL;

    if (read_only) {
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
        goto end;
    }

    if (!check_polkit (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-static-hostname", user_interaction, &err)) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        goto end;
    }

    g_static_mutex_lock (&static_hostname_mutex);
    /* Don't allow an empty or invalid hostname */
    if (!hostname_is_valid (name))
        name = "localhost";

    confd_file = shell_utils_trivial_new (SYSCONFDIR "/conf.d/hostname", &err);
    if (confd_file == NULL) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        g_static_mutex_unlock (&static_hostname_mutex);
        goto end;
    }

    if (!shell_utils_trivial_set_variable (confd_file, "hostname", name, FALSE) &&
        !shell_utils_trivial_set_variable (confd_file, "HOSTNAME", name, FALSE) &&
        !shell_utils_trivial_set_variable (confd_file, "hostname", name, TRUE))
    {
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_FAILED,
                                                    "Failed to set static hostname in " SYSCONFDIR "/conf.d/hostname");
        g_static_mutex_unlock (&static_hostname_mutex);
        goto end;
    }

    if (!shell_utils_trivial_save (confd_file, &err)) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        g_static_mutex_unlock (&static_hostname_mutex);
        goto end;
    }

    g_free (static_hostname);
    static_hostname = g_strdup (name);
    openrc_settingsd_hostnamed_hostname1_complete_set_static_hostname (hostname1, invocation);
    openrc_settingsd_hostnamed_hostname1_set_static_hostname (hostname1, static_hostname);
    g_static_mutex_unlock (&static_hostname_mutex);

  end:
    shell_utils_trivial_free (confd_file);
    if (err != NULL)
        g_error_free (err);

    return TRUE; /* Always return TRUE to indicate signal has been handled */
}

static gboolean
on_handle_set_machine_info (OpenrcSettingsdHostnamedHostname1 *hostname1,
                            GDBusMethodInvocation *invocation,
                            const gchar *name,
                            const gboolean user_interaction,
                            gpointer user_data)
{
    ShellUtilsTrivial *confd_file = NULL;
    GError *err = NULL;
    gboolean is_pretty_hostname = GPOINTER_TO_INT(user_data);

    if (read_only) {
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
        goto end;
    }

    if (!check_polkit (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-machine-info", user_interaction, &err)) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        goto end;
    }

    g_static_mutex_lock (&machine_info_mutex);
    /* Don't allow a null pretty hostname */
    if (name == NULL)
        name = "";

    confd_file = shell_utils_trivial_new (SYSCONFDIR "/machine-info", &err);
    if (confd_file == NULL) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        g_static_mutex_unlock (&machine_info_mutex);
        goto end;
    }

    if ((is_pretty_hostname && !shell_utils_trivial_set_variable (confd_file, "PRETTY_HOSTNAME", name, TRUE)) ||
        (!is_pretty_hostname && !shell_utils_trivial_set_variable (confd_file, "ICON_NAME", name, TRUE)))
    {
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_FAILED,
                                                    "Failed to value in " SYSCONFDIR "/machine-info");
        g_static_mutex_unlock (&machine_info_mutex);
        goto end;
    }

    if (!shell_utils_trivial_save (confd_file, &err)) {
        g_dbus_method_invocation_return_gerror (invocation, err);
        g_static_mutex_unlock (&machine_info_mutex);
        goto end;
    }

    if (is_pretty_hostname) {
        g_free (pretty_hostname);
        pretty_hostname = g_strdup (name);
        openrc_settingsd_hostnamed_hostname1_complete_set_pretty_hostname (hostname1, invocation);
        openrc_settingsd_hostnamed_hostname1_set_pretty_hostname (hostname1, pretty_hostname);
    } else {
        g_free (icon_name);
        icon_name = g_strdup (name);
        openrc_settingsd_hostnamed_hostname1_complete_set_icon_name (hostname1, invocation);
        openrc_settingsd_hostnamed_hostname1_set_icon_name (hostname1, icon_name);
    }
    g_static_mutex_unlock (&machine_info_mutex);

  end:
    shell_utils_trivial_free (confd_file);
    if (err != NULL)
        g_error_free (err);

    return TRUE; /* Always return TRUE to indicate signal has been handled */
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *bus_name,
                 gpointer         user_data)
{
    gchar *name;
    GError *err = NULL;

#ifdef OPENRC_SETTINGSD_DEBUG
    g_debug ("Acquired a message bus connection");
#endif

    hostname1 = openrc_settingsd_hostnamed_hostname1_skeleton_new ();

    openrc_settingsd_hostnamed_hostname1_set_hostname (hostname1, hostname);
    openrc_settingsd_hostnamed_hostname1_set_static_hostname (hostname1, static_hostname);
    openrc_settingsd_hostnamed_hostname1_set_pretty_hostname (hostname1, pretty_hostname);
    openrc_settingsd_hostnamed_hostname1_set_icon_name (hostname1, icon_name);

    g_signal_connect (hostname1, "handle-set-hostname", G_CALLBACK (on_handle_set_hostname), NULL);
    g_signal_connect (hostname1, "handle-set-static-hostname", G_CALLBACK (on_handle_set_static_hostname), NULL);
    g_signal_connect (hostname1, "handle-set-pretty-hostname", G_CALLBACK (on_handle_set_machine_info), GINT_TO_POINTER(TRUE));
    g_signal_connect (hostname1, "handle-set-icon-name", G_CALLBACK (on_handle_set_machine_info), GINT_TO_POINTER(FALSE));

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (hostname1),
                                           connection,
                                           "/org/freedesktop/hostname1",
                                           &err)) {
        if (err != NULL) {
            g_printerr ("Failed to export interface on /org/freedesktop/hostname1: %s\n", err->message);
            g_error_free (err);
        }
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data)
{
#ifdef OPENRC_SETTINGSD_DEBUG
    g_debug ("Acquired the name %s", bus_name);
#endif
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *bus_name,
              gpointer         user_data)
{
    if (connection == NULL)
        g_printerr ("Failed to acquire a dbus connection\n");
    else
        g_printerr ("Failed to acquire dbus name %s\n", bus_name);
    exit(-1);
}

/* Public functions */

void
hostnamed_init (gboolean _read_only)
{
    GError *err = NULL;

    memset (hostname, 0, HOST_NAME_MAX + 1);
    if (gethostname (hostname, HOST_NAME_MAX)) {
        perror (NULL);
        hostname[0] = 0;
    }

    static_hostname = shell_utils_source_var (SYSCONFDIR "/conf.d/hostname", "${hostname-${HOSTNAME-localhost}}", &err);
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }
    pretty_hostname = shell_utils_source_var (SYSCONFDIR "/machine-info", "${PRETTY_HOSTNAME}", &err);
    if (pretty_hostname == NULL)
        pretty_hostname = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }
    icon_name = shell_utils_source_var (SYSCONFDIR "/machine-info", "${ICON_NAME}", &err);
    if (icon_name == NULL)
        icon_name = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }

    if (icon_name == NULL || strlen (icon_name) == 0)
        guess_icon_name ();

    read_only = _read_only;

    bus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.freedesktop.hostname1",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);
}

void
hostnamed_destroy (void)
{
    g_bus_unown_name (bus_id);
    bus_id = 0;
    read_only = FALSE;
    memset (hostname, 0, HOST_NAME_MAX + 1);
    g_free (static_hostname);
    g_free (pretty_hostname);
    g_free (icon_name);
}