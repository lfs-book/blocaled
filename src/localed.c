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

#include <dbus/dbus-protocol.h>
#include <glib.h>
#include <gio/gio.h>

#include "localed.h"
#include "locale1-generated.h"
#include "bus-utils.h"
#include "shell-utils.h"

#include "config.h"

#define SERVICE_NAME "openrc-settingsd localed"

static guint bus_id = 0;
static gboolean read_only = FALSE;

static OpenrcSettingsdLocaledLocale1 *locale1 = NULL;

static gchar *locale_variables[] = {
    "LANG", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION", NULL
};

static gchar **locale = NULL; /* Expected format is { "LANG=foo", "LC_TIME=bar", NULL } */
static GFile *locale_file = NULL;
G_LOCK_DEFINE_STATIC (locale);

static gchar *vconsole_keymap = NULL;
static gchar *vconsole_keymap_toggle = NULL;
static GFile *keymaps_file = NULL;
G_LOCK_DEFINE_STATIC (keymaps);

static gchar *x11_layout = NULL;
static gchar *x11_model = NULL;
static gchar *x11_variant = NULL;
static gchar *x11_options = NULL;
G_LOCK_DEFINE_STATIC (xorg_conf);

static gboolean
on_handle_set_locale (OpenrcSettingsdLocaledLocale1 *locale1,
                      GDBusMethodInvocation *invocation,
                      const gchar * const *_locale,
                      const gboolean user_interaction,
                      gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static gboolean
on_handle_set_vconsole_keyboard (OpenrcSettingsdLocaledLocale1 *locale1,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *keymap,
                                 const gchar *keymap_toggle,
                                 const gboolean convert,
                                 const gboolean user_interaction,
                                 gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static gboolean
on_handle_set_x11_keyboard (OpenrcSettingsdLocaledLocale1 *locale1,
                            GDBusMethodInvocation *invocation,
                            const gchar *layout,
                            const gchar *model,
                            const gchar *variant,
                            const gchar *options,
                            const gboolean convert,
                            const gboolean user_interaction,
                            gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *bus_name,
                 gpointer         user_data)
{
    gchar *name;
    GError *err = NULL;

    g_debug ("Acquired a message bus connection");

    locale1 = openrc_settingsd_localed_locale1_skeleton_new ();

    openrc_settingsd_localed_locale1_set_locale (locale1, (const gchar * const *) locale);
    openrc_settingsd_localed_locale1_set_vconsole_keymap (locale1, vconsole_keymap);
    openrc_settingsd_localed_locale1_set_vconsole_keymap_toggle (locale1, vconsole_keymap_toggle);
    openrc_settingsd_localed_locale1_set_x11_layout (locale1, x11_layout);
    openrc_settingsd_localed_locale1_set_x11_model (locale1, x11_model);
    openrc_settingsd_localed_locale1_set_x11_variant (locale1, x11_variant);
    openrc_settingsd_localed_locale1_set_x11_options (locale1, x11_options);

    g_signal_connect (locale1, "handle-set-locale", G_CALLBACK (on_handle_set_locale), NULL);
    g_signal_connect (locale1, "handle-set-vconsole-keyboard", G_CALLBACK (on_handle_set_vconsole_keyboard), NULL);
    g_signal_connect (locale1, "handle-set-x11-keyboard", G_CALLBACK (on_handle_set_x11_keyboard), NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (locale1),
                                           connection,
                                           "/org/freedesktop/locale1",
                                           &err)) {
        if (err != NULL) {
            g_printerr ("Failed to export interface on /org/freedesktop/locale1: %s\n", err->message);
            g_error_free (err);
        }
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data)
{
    g_debug ("Acquired the name %s", bus_name);
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

void
localed_init (gboolean _read_only)
{
    GError *err = NULL;
    gchar **locale_values = NULL;

    read_only = _read_only;
    locale_file = g_file_new_for_path (SYSCONFDIR "/env.d/02locale");
    keymaps_file = g_file_new_for_path (SYSCONFDIR "/conf.d/keymaps");

    locale = g_new0 (gchar *, g_strv_length (locale_variables) + 1);
    locale_values = shell_utils_trivial_source_var_list (locale_file, (const gchar * const *)locale_variables, &err);
    if (locale_values != NULL) {
        gchar **variable, **value, **loc;
        loc = locale;
        for (variable = locale_variables, value = locale_values; *variable != NULL; variable++, value++) {
            if (*value != NULL) {
                *loc = g_strdup_printf ("%s=%s", *variable, *value);
                loc++;
            }
        }
            
        g_strfreev (locale_values);
    }
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    vconsole_keymap = shell_utils_source_var (keymaps_file, "${keymap}", &err);
    if (vconsole_keymap == NULL)
        vconsole_keymap = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    /* We don't have a good equivalent for this in openrc at the moment */
    vconsole_keymap_toggle = g_strdup ("");

    bus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.freedesktop.locale1",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);
}

void
localed_destroy (void)
{
    g_bus_unown_name (bus_id);
    bus_id = 0;
    read_only = FALSE;
    g_strfreev (locale);
    g_free (vconsole_keymap);
    g_free (vconsole_keymap_toggle);
    g_free (x11_layout);
    g_free (x11_model);
    g_free (x11_variant);
    g_free (x11_options);

    g_object_unref (locale_file);
    g_object_unref (keymaps_file);
}
