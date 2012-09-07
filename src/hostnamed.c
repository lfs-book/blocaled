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

#include <dbus/dbus-protocol.h>
#include <glib.h>
#include <gio/gio.h>
#include <polkit/polkit.h>

#include <rc.h>

#include "hostnamed.h"
#include "hostname1-generated.h"
#include "main.h"
#include "utils.h"

#include "config.h"

#define QUOTE(macro) #macro
#define STR(macro) QUOTE(macro)

struct invoked_name {
    GDBusMethodInvocation *invocation;
    gchar *name; /* newly allocated */
};

guint bus_id = 0;
gboolean read_only = FALSE;

static OpenrcSettingsdHostnamedHostname1 *hostname1 = NULL;

static gchar *hostname = NULL;
G_LOCK_DEFINE_STATIC (hostname);
static gchar *static_hostname = NULL;
static GFile *static_hostname_file = NULL;
G_LOCK_DEFINE_STATIC (static_hostname);
static gchar *pretty_hostname = NULL;
static gchar *icon_name = NULL;
static GFile *machine_info_file = NULL;
G_LOCK_DEFINE_STATIC (machine_info);

static gboolean
hostname_is_valid (const gchar *name)
{
    if (name == NULL)
        return FALSE;

    return g_regex_match_simple ("^[a-zA-Z0-9_.-]{1," STR(HOST_NAME_MAX) "}$",
                                 name, G_REGEX_MULTILINE, 0);
}

static gchar *
guess_icon_name ()
{
    gchar *filebuf = NULL;
    gchar *ret = NULL;

    /* Note that rc_sys() leaks memory :( */
    if (rc_sys() != NULL) {
        ret = g_strdup ("computer-vm");
        goto out;
    }

#if defined(__i386__) || defined(__x86_64__)
    /* 
       Taken with a few minor changes from systemd's hostnamed

       See the SMBIOS Specification 2.7.1 section 7.4.1 for
       details about the values listed here:

       http://www.dmtf.org/sites/default/files/standards/documents/DSP0134_2.7.1.pdf
    */

    if (g_file_get_contents ("/sys/class/dmi/id/chassis_type", &filebuf, NULL, NULL)) {
        switch (g_ascii_strtoull (filebuf, NULL, 10)) {
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            ret = g_strdup ("computer-desktop");
            goto out;
        case 0x9:
        case 0xA:
        case 0xE:
            ret = g_strdup ("computer-laptop");
            goto out;
        case 0x11:
        case 0x17:
        case 0x1C:
        case 0x1D:
            ret = g_strdup ("computer-server");
            goto out;
        }
    }
#endif
    ret = g_strdup ("computer");
  out:
    g_free (filebuf);
    return ret;
}

static void
on_handle_set_hostname_authorized_cb (GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
    GError *err = NULL;
    struct invoked_name *data;
    
    data = (struct invoked_name *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (hostname);
    /* Don't allow an empty or invalid hostname */
    if (!hostname_is_valid (data->name)) {
        if (data->name != NULL)
            g_free (data->name);

        if (hostname_is_valid (static_hostname))
            data->name = g_strdup (static_hostname);
        else
            data->name = g_strdup ("localhost");
    }
    if (sethostname (data->name, strlen(data->name))) {
        int errsv = errno;
        g_dbus_method_invocation_return_dbus_error (data->invocation,
                                                    DBUS_ERROR_FAILED,
                                                    strerror (errsv));
        G_UNLOCK (hostname);
        goto out;
    }
    g_free (hostname);
    hostname = data->name; /* data->name is g_strdup-ed already */;
    openrc_settingsd_hostnamed_hostname1_complete_set_hostname (hostname1, data->invocation);
    openrc_settingsd_hostnamed_hostname1_set_hostname (hostname1, hostname);
    G_UNLOCK (hostname);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_hostname (OpenrcSettingsdHostnamedHostname1 *hostname1,
                        GDBusMethodInvocation *invocation,
                        const gchar *name,
                        const gboolean user_interaction,
                        gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
    else {
        struct invoked_name *data;
        data = g_new0 (struct invoked_name, 1);
        data->invocation = invocation;
        data->name = g_strdup (name);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-hostname", user_interaction, on_handle_set_hostname_authorized_cb, data);
    }

    return TRUE;
}

static void
on_handle_set_static_hostname_authorized_cb (GObject *source_object,
                                             GAsyncResult *res,
                                             gpointer user_data)
{
    GError *err = NULL;
    struct invoked_name *data;
    
    data = (struct invoked_name *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (static_hostname);
    /* Don't allow an empty or invalid hostname */
    if (!hostname_is_valid (data->name)) {
        if (data->name != NULL)
            g_free (data->name);

        data->name = g_strdup ("localhost");
    }

    if (!shell_parser_set_and_save (static_hostname_file, &err, "hostname", "HOSTNAME", data->name, NULL)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        G_UNLOCK (static_hostname);
        goto out;
    }

    g_free (static_hostname);
    static_hostname = data->name; /* data->name is g_strdup-ed already */;
    openrc_settingsd_hostnamed_hostname1_complete_set_static_hostname (hostname1, data->invocation);
    openrc_settingsd_hostnamed_hostname1_set_static_hostname (hostname1, static_hostname);
    G_UNLOCK (static_hostname);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_static_hostname (OpenrcSettingsdHostnamedHostname1 *hostname1,
                               GDBusMethodInvocation *invocation,
                               const gchar *name,
                               const gboolean user_interaction,
                               gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
    else {
        struct invoked_name *data;
        data = g_new0 (struct invoked_name, 1);
        data->invocation = invocation;
        data->name = g_strdup (name);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-static-hostname", user_interaction, on_handle_set_static_hostname_authorized_cb, data);
    }

    return TRUE; /* Always return TRUE to indicate signal has been handled */
}

static void
on_handle_set_pretty_hostname_authorized_cb (GObject *source_object,
                                             GAsyncResult *res,
                                             gpointer user_data)
{
    GError *err = NULL;
    struct invoked_name *data;
    
    data = (struct invoked_name *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (machine_info);
    /* Don't allow a null pretty hostname */
    if (data->name == NULL)
        data->name = g_strdup ("");

    if (!shell_parser_set_and_save (machine_info_file, &err, "PRETTY_HOSTNAME", NULL, data->name, NULL)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        G_UNLOCK (machine_info);
        goto out;
    }

    g_free (pretty_hostname);
    pretty_hostname = data->name; /* data->name is g_strdup-ed already */
    openrc_settingsd_hostnamed_hostname1_complete_set_pretty_hostname (hostname1, data->invocation);
    openrc_settingsd_hostnamed_hostname1_set_pretty_hostname (hostname1, pretty_hostname);
    G_UNLOCK (machine_info);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_pretty_hostname (OpenrcSettingsdHostnamedHostname1 *hostname1,
                               GDBusMethodInvocation *invocation,
                               const gchar *name,
                               const gboolean user_interaction,
                               gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
    else {
        struct invoked_name *data;
        data = g_new0 (struct invoked_name, 1);
        data->invocation = invocation;
        data->name = g_strdup (name);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-machine-info", user_interaction, on_handle_set_pretty_hostname_authorized_cb, data);
    }

    return TRUE; /* Always return TRUE to indicate signal has been handled */
}

static void
on_handle_set_icon_name_authorized_cb (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
    GError *err = NULL;
    struct invoked_name *data;
    
    data = (struct invoked_name *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (machine_info);
    /* Don't allow a null pretty hostname */
    if (data->name == NULL)
        data->name = g_strdup ("");

    if (!shell_parser_set_and_save (machine_info_file, &err, "ICON_NAME", NULL, data->name, NULL)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        G_UNLOCK (machine_info);
        goto out;
    }

    g_free (icon_name);
    icon_name = data->name; /* data->name is g_strdup-ed already */
    openrc_settingsd_hostnamed_hostname1_complete_set_icon_name (hostname1, data->invocation);
    openrc_settingsd_hostnamed_hostname1_set_icon_name (hostname1, icon_name);
    G_UNLOCK (machine_info);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_icon_name (OpenrcSettingsdHostnamedHostname1 *hostname1,
                         GDBusMethodInvocation *invocation,
                         const gchar *name,
                         const gboolean user_interaction,
                         gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    "openrc-settingsd hostnamed is in read-only mode");
    else {
        struct invoked_name *data;
        data = g_new0 (struct invoked_name, 1);
        data->invocation = invocation;
        data->name = g_strdup (name);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.hostname1.set-machine-info", user_interaction, on_handle_set_icon_name_authorized_cb, data);
    }

    return TRUE; /* Always return TRUE to indicate signal has been handled */
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *bus_name,
                 gpointer         user_data)
{
    gchar *name;
    GError *err = NULL;

    g_debug ("Acquired a message bus connection");

    hostname1 = openrc_settingsd_hostnamed_hostname1_skeleton_new ();

    openrc_settingsd_hostnamed_hostname1_set_hostname (hostname1, hostname);
    openrc_settingsd_hostnamed_hostname1_set_static_hostname (hostname1, static_hostname);
    openrc_settingsd_hostnamed_hostname1_set_pretty_hostname (hostname1, pretty_hostname);
    openrc_settingsd_hostnamed_hostname1_set_icon_name (hostname1, icon_name);

    g_signal_connect (hostname1, "handle-set-hostname", G_CALLBACK (on_handle_set_hostname), NULL);
    g_signal_connect (hostname1, "handle-set-static-hostname", G_CALLBACK (on_handle_set_static_hostname), NULL);
    g_signal_connect (hostname1, "handle-set-pretty-hostname", G_CALLBACK (on_handle_set_pretty_hostname), NULL);
    g_signal_connect (hostname1, "handle-set-icon-name", G_CALLBACK (on_handle_set_icon_name), NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (hostname1),
                                           connection,
                                           "/org/freedesktop/hostname1",
                                           &err)) {
        if (err != NULL) {
            g_critical ("Failed to export interface on /org/freedesktop/hostname1: %s", err->message);
            openrc_settingsd_exit (1);
        }
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data)
{
    g_debug ("Acquired the name %s", bus_name);
    openrc_settingsd_component_started ();
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *bus_name,
              gpointer         user_data)
{
    if (connection == NULL)
        g_critical ("Failed to acquire a dbus connection");
    else
        g_critical ("Failed to acquire dbus name %s", bus_name);
    openrc_settingsd_exit (1);
}

/* Public functions */

void
hostnamed_init (gboolean _read_only)
{
    GError *err = NULL;

    hostname = g_malloc0 (HOST_NAME_MAX + 1);
    if (gethostname (hostname, HOST_NAME_MAX)) {
        perror (NULL);
        g_strlcpy (hostname, "localhost", HOST_NAME_MAX + 1);
    }

    static_hostname_file = g_file_new_for_path (SYSCONFDIR "/conf.d/hostname");
    machine_info_file = g_file_new_for_path (SYSCONFDIR "/machine-info");

    static_hostname = shell_source_var (static_hostname_file, "${hostname-${HOSTNAME-localhost}}", &err);
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }
    pretty_hostname = shell_source_var (machine_info_file, "${PRETTY_HOSTNAME}", &err);
    if (pretty_hostname == NULL)
        pretty_hostname = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }
    icon_name = shell_source_var (machine_info_file, "${ICON_NAME}", &err);
    if (icon_name == NULL)
        icon_name = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_error_free (err);
        err = NULL;
    }

    if (icon_name == NULL || *icon_name == 0) {
        g_free (icon_name);
        icon_name = guess_icon_name ();
    }

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
    g_free (hostname);
    g_free (static_hostname);
    g_free (pretty_hostname);
    g_free (icon_name);

    g_object_unref (static_hostname_file);
    g_object_unref (machine_info_file);
}
