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
#include <time.h>

#include <dbus/dbus-protocol.h>
#include <glib.h>
#include <gio/gio.h>

#include <rc.h>

#include "copypaste/hwclock.h"
#include "timedated.h"
#include "timedate1-generated.h"
#include "bus-utils.h"
#include "shell-utils.h"

#include "config.h"

#define SERVICE_NAME "openrc-settingsd timedated"

static guint bus_id = 0;
static gboolean read_only = FALSE;

static OpenrcSettingsdTimedatedTimedate1 *timedate1 = NULL;

static GFile *hwclock_file = NULL;
static GFile *timezone_file = NULL;
static GFile *localtime_file = NULL;

gboolean local_rtc = FALSE;
gchar *timezone_name = NULL;
G_LOCK_DEFINE_STATIC (clock);

gboolean use_ntp = FALSE;
static const gchar *ntp_preferred_service = NULL;
static const gchar *ntp_default_services[4] = { "ntpd", "chronyd", "busybox-ntpd", NULL };
G_LOCK_DEFINE_STATIC (ntp);

static gboolean
get_local_rtc (GError **error)
{
    gchar *clock = NULL;
    gboolean ret = FALSE;

    clock = shell_utils_source_var (hwclock_file, "${clock}", error);
    if (!g_strcmp0 (clock, "local"))
        ret = TRUE;
    return ret;
}

static gchar *
get_timezone_name (GError **error)
{
    gchar *filebuf = NULL, *filebuf2 = NULL, *ret = NULL, *newline = NULL, *filename = NULL;
    gchar *timezone_filename = NULL, *localtime_filename = NULL, *localtime2_filename = NULL;
    GFile *localtime2_file = NULL;

    timezone_filename = g_file_get_path (timezone_file);
    if (!g_file_load_contents (timezone_file, NULL, &filebuf, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", timezone_filename);
        ret = g_strdup ("");
        goto out;
    }
    if ((newline = strstr (filebuf, "\n")) != NULL)
        *newline = 0;
    ret = g_strdup (g_strstrip (filebuf));
    g_free (filebuf);
    filebuf = NULL;

    /* Log if /etc/localtime is not up to date */
    localtime_filename = g_file_get_path (localtime_file);
    localtime2_filename = g_strdup_printf (DATADIR "/zoneinfo/%s", ret);
    localtime2_file = g_file_new_for_path (localtime2_filename);

    if (!g_file_load_contents (localtime_file, NULL, &filebuf, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", localtime_filename);
        goto out;
    }
    if (!g_file_load_contents (localtime2_file, NULL, &filebuf2, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", localtime2_filename);
        goto out;
    }
    if (g_strcmp0 (filebuf, filebuf2))
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s and %s differ; %s may be outdated or out of sync with %s", localtime_filename, localtime2_filename, localtime_filename, timezone_filename);

  out:
    g_free (filebuf);
    g_free (filebuf2);
    g_free (timezone_filename);
    g_free (localtime_filename);
    g_free (localtime2_filename);
    if (localtime_file != NULL)
        g_object_unref (localtime2_file);
    return ret;
}

static gboolean
set_timezone (const gchar *_timezone_name,
              GError **error)
{
    gchar *filebuf = NULL;
    gchar *timezone_filename = NULL, *localtime_filename = NULL, *localtime2_filename = NULL;
    GFile *localtime2_file = NULL;
    gboolean ret = FALSE;
    gsize length = 0;

    timezone_filename = g_file_get_path (timezone_file);
    if (!g_file_replace_contents (timezone_file, _timezone_name, strlen (_timezone_name), NULL, FALSE, 0, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to write '%s':", timezone_filename);
        goto out;
    }

    localtime_filename = g_file_get_path (localtime_file);
    localtime2_filename = g_strdup_printf (DATADIR "/zoneinfo/%s", _timezone_name);
    localtime2_file = g_file_new_for_path (localtime2_filename);
    if (!g_file_load_contents (localtime2_file, NULL, &filebuf, &length, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", localtime2_filename);
        goto out;
    }
    if (!g_file_replace_contents (localtime_file, filebuf, length, NULL, FALSE, 0, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to write '%s':", localtime_filename);
        goto out;
    }
    ret = TRUE;

  out:
    g_free (filebuf);
    g_free (timezone_filename);
    g_free (localtime_filename);
    g_free (localtime2_filename);
    if (localtime_file != NULL)
        g_object_unref (localtime2_file);
    return ret;
}

/* Return the ntp rc service we will use; return value should NOT be freed */
static const gchar *
ntp_service ()
{
    const gchar * const *s = NULL;
    const gchar *service = NULL;
    gchar *runlevel = NULL;

    if (ntp_preferred_service != NULL)
        return ntp_preferred_service;

    runlevel = rc_runlevel_get();
    for (s = ntp_default_services; *s != NULL; s++) {
        if (!rc_service_exists (*s))
            continue;
        if (service == NULL)
            service = *s;
        if (rc_service_in_runlevel (*s, runlevel)) {
            service = *s;
            break;
        }
    }
    free (runlevel);

    if (service == NULL)
        service = ntp_default_services[0];
    return service;
}

static gboolean
service_started (const gchar *service,
                 GError **error)
{
    RC_SERVICE state;

    if (!rc_service_exists (service)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s rc service not found", service);
        return FALSE;
    }

    state = rc_service_state (service);
    return state == RC_SERVICE_STARTED || state == RC_SERVICE_STARTING || state == RC_SERVICE_INACTIVE;
}

static gboolean
service_disable (const gchar *service,
                 GError **error)
{
    gchar *runlevel = NULL;
    gchar *service_script = NULL;
    const gchar *argv[3] = { NULL, "stop", NULL };
    gboolean ret = FALSE;
    gint exit_status = 0;

    if (!rc_service_exists (service)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s rc service not found", service);
        goto out;
    }

    runlevel = rc_runlevel_get();
    if (rc_service_in_runlevel (service, runlevel)) {
        g_debug ("Removing %s rc service from %s runlevel", service, runlevel);
        if (!rc_service_delete (runlevel, service))
            g_warning ("Failed to remove %s rc service from %s runlevel", service, runlevel);
    }

    if ((service_script = rc_service_resolve (service)) == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s rc service does not resolve", service);
        goto out;
    }

    g_debug ("Stopping %s rc service", service);
    argv[0] = service;
    if (!g_spawn_sync (NULL, (gchar **)argv, NULL, 0, NULL, NULL, NULL, NULL, &exit_status, error)) {
        g_prefix_error (error, "Failed to spawn %s rc service:", service);
        goto out;
    }
    if (exit_status) {
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "%s rc service failed to stop with exit status %d", service, exit_status);
        goto out;
    }
    ret = TRUE;
    
  out:
    if (runlevel != NULL)
        free (runlevel);
    if (service_script != NULL)
        free (service_script);
    return ret;
}

static gboolean
service_enable (const gchar *service,
                GError **error)
{
    gchar *runlevel = NULL;
    gchar *service_script = NULL;
    const gchar *argv[3] = { NULL, "start", NULL };
    gboolean ret = FALSE;
    gint exit_status = 0;

    if (!rc_service_exists (service)) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s rc service not found", service);
        goto out;
    }

    runlevel = rc_runlevel_get();
    if (!rc_service_in_runlevel (service, runlevel)) {
        g_debug ("Adding %s rc service to %s runlevel", service, runlevel);
        if (!rc_service_add (runlevel, service))
            g_warning ("Failed to add %s rc service to %s runlevel", service, runlevel);
    }

    if ((service_script = rc_service_resolve (service)) == NULL) {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s rc service does not resolve", service);
        goto out;
    }

    g_debug ("Starting %s rc service", service);
    argv[0] = service;
    if (!g_spawn_sync (NULL, (gchar **)argv, NULL, 0, NULL, NULL, NULL, NULL, &exit_status, error)) {
        g_prefix_error (error, "Failed to spawn %s rc service:", service);
        goto out;
    }
    if (exit_status) {
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "%s rc service failed to start with exit status %d", service, exit_status);
        goto out;
    }
    ret = TRUE;
    
  out:
    if (runlevel != NULL)
        free (runlevel);
    if (service_script != NULL)
        free (service_script);
    return ret;
}

struct invoked_set_time {
    GDBusMethodInvocation *invocation;
    gint64 usec_utc;
    gboolean relative;
};

static void
on_handle_set_time_authorized_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
    GError *err = NULL;
    struct invoked_set_time *data;
    struct timespec ts = { 0, 0 };
    struct tm *tm = NULL;

    data = (struct invoked_set_time *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (clock);
    if (!data->relative && data->usec_utc < 0) {
        g_dbus_method_invocation_return_dbus_error (data->invocation, DBUS_ERROR_INVALID_ARGS, "Attempt to set time before epoch");
        goto unlock;
    }

    if (data->relative)
        if (clock_gettime (CLOCK_REALTIME, &ts)) {
            int errsv = errno;
            g_dbus_method_invocation_return_dbus_error (data->invocation, DBUS_ERROR_FAILED, strerror (errsv));
            goto unlock;
        }
    ts.tv_sec += data->usec_utc / 1000000;
    ts.tv_nsec += (data->usec_utc % 1000000) * 1000;
    if (clock_settime (CLOCK_REALTIME, &ts)) {
        int errsv = errno;
        g_dbus_method_invocation_return_dbus_error (data->invocation, DBUS_ERROR_FAILED, strerror (errsv));
        goto unlock;
    }

    if (local_rtc)
        tm = localtime(&ts.tv_sec);
    else
        tm = gmtime(&ts.tv_sec);
    hwclock_set_time(tm);

    openrc_settingsd_timedated_timedate1_complete_set_time (timedate1, data->invocation);

  unlock:
    G_UNLOCK (clock);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_time (OpenrcSettingsdTimedatedTimedate1 *timedate1,
                    GDBusMethodInvocation *invocation,
                    const gint64 usec_utc,
                    const gboolean relative,
                    const gboolean user_interaction,
                    gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_set_time *data;
        data = g_new0 (struct invoked_set_time, 1);
        data->invocation = invocation;
        data->usec_utc = usec_utc;
        data->relative = relative;
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.timedate1.set-time", user_interaction, on_handle_set_time_authorized_cb, data);
    }

    return TRUE;
}

struct invoked_set_timezone {
    GDBusMethodInvocation *invocation;
    gchar *timezone; /* newly allocated */
};

static void
on_handle_set_timezone_authorized_cb (GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
    GError *err = NULL;
    struct invoked_set_timezone *data;

    data = (struct invoked_set_timezone *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (clock);
    if (!set_timezone(data->timezone, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }

    if (local_rtc) {
        struct timespec ts;
        struct tm *tm;
 
        /* Update kernel's view of the rtc timezone */
        hwclock_apply_localtime_delta (NULL);
        clock_gettime (CLOCK_REALTIME, &ts);
        tm = localtime (&ts.tv_sec);
        hwclock_set_time (tm);
    }

    openrc_settingsd_timedated_timedate1_complete_set_timezone (timedate1, data->invocation);
    g_free (timezone_name);
    timezone_name = data->timezone;
    openrc_settingsd_timedated_timedate1_set_timezone (timedate1, timezone_name);

  unlock:
    G_UNLOCK (clock);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_timezone (OpenrcSettingsdTimedatedTimedate1 *timedate1,
                        GDBusMethodInvocation *invocation,
                        const gchar *timezone,
                        const gboolean user_interaction,
                        gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_set_timezone *data;
        data = g_new0 (struct invoked_set_timezone, 1);
        data->invocation = invocation;
        data->timezone = g_strdup (timezone);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.timedate1.set-timezone", user_interaction, on_handle_set_timezone_authorized_cb, data);
    }

    return TRUE;
}

struct invoked_set_local_rtc {
    GDBusMethodInvocation *invocation;
    gboolean local_rtc;
    gboolean fix_system;
};

static void
on_handle_set_local_rtc_authorized_cb (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
    GError *err = NULL;
    struct invoked_set_local_rtc *data;
    gchar *clock = NULL;
    const gchar *clock_types[2] = { "UTC", "local" };

    data = (struct invoked_set_local_rtc *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (clock);
    clock = shell_utils_source_var (hwclock_file, "${clock}", NULL);
    if (clock != NULL || data->local_rtc)
        if (!shell_utils_trivial_set_and_save (hwclock_file, &err, "clock", NULL, clock_types[data->local_rtc], NULL)) {
            g_dbus_method_invocation_return_gerror (data->invocation, err);
            goto unlock;
        }

    if (data->local_rtc != local_rtc) {
        /* The clock sync code below taken almost verbatim from systemd's timedated.c, and is
         * copyright 2011 Lennart Poettering */
        struct timespec ts;

        /* Update kernel's view of the rtc timezone */
        if (data->local_rtc) 
            hwclock_apply_localtime_delta (NULL);
        else
            hwclock_reset_localtime_delta ();

        clock_gettime (CLOCK_REALTIME, &ts);
        if (data->fix_system) {
            struct tm tm;

            /* Sync system clock from RTC; first,
             * initialize the timezone fields of
             * struct tm. */
            if (data->local_rtc)
                tm = *localtime(&ts.tv_sec);
            else
                tm = *gmtime(&ts.tv_sec);

            /* Override the main fields of
             * struct tm, but not the timezone
             * fields */
            if (hwclock_get_time(&tm) >= 0) {
                /* And set the system clock
                 * with this */
                if (data->local_rtc)
                    ts.tv_sec = mktime(&tm);
                else
                    ts.tv_sec = timegm(&tm);

                clock_settime(CLOCK_REALTIME, &ts);
            }

        } else {
            struct tm *tm;

            /* Sync RTC from system clock */
            if (data->local_rtc)
                tm = localtime(&ts.tv_sec);
            else
                tm = gmtime(&ts.tv_sec);

            hwclock_set_time(tm);
        }
    }

    openrc_settingsd_timedated_timedate1_complete_set_timezone (timedate1, data->invocation);
    g_free (timezone_name);
    local_rtc = data->local_rtc;
    openrc_settingsd_timedated_timedate1_set_local_rtc (timedate1, local_rtc);

  unlock:
    G_UNLOCK (clock);

  out:
    g_free (clock);
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_local_rtc (OpenrcSettingsdTimedatedTimedate1 *timedate1,
                         GDBusMethodInvocation *invocation,
                         const gboolean _local_rtc,
                         const gboolean fix_system,
                         const gboolean user_interaction,
                         gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_set_local_rtc *data;
        data = g_new0 (struct invoked_set_local_rtc, 1);
        data->invocation = invocation;
        data->local_rtc = _local_rtc;
        data->fix_system = fix_system;
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.timedate1.set-local-rtc", user_interaction, on_handle_set_local_rtc_authorized_cb, data);
    }

    return TRUE;
}

struct invoked_set_ntp {
    GDBusMethodInvocation *invocation;
    gboolean use_ntp;
};

static void
on_handle_set_ntp_authorized_cb (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
    GError *err = NULL;
    struct invoked_set_ntp *data;

    data = (struct invoked_set_ntp *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (ntp);
    if ((data->use_ntp && !service_enable (ntp_service(), &err)) ||
        (!data->use_ntp && !service_disable (ntp_service(), &err)))
    {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }

    openrc_settingsd_timedated_timedate1_complete_set_ntp (timedate1, data->invocation);
    use_ntp = data->use_ntp;
    openrc_settingsd_timedated_timedate1_set_ntp (timedate1, use_ntp);

  unlock:
    G_UNLOCK (ntp);

  out:
    g_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_ntp (OpenrcSettingsdTimedatedTimedate1 *timedate1,
                   GDBusMethodInvocation *invocation,
                   const gboolean _use_ntp,
                   const gboolean user_interaction,
                   gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_set_ntp *data;
        data = g_new0 (struct invoked_set_ntp, 1);
        data->invocation = invocation;
        data->use_ntp = _use_ntp;
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.timedate1.set-ntp", user_interaction, on_handle_set_ntp_authorized_cb, data);
    }

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

    timedate1 = openrc_settingsd_timedated_timedate1_skeleton_new ();

    openrc_settingsd_timedated_timedate1_set_timezone (timedate1, timezone_name);
    openrc_settingsd_timedated_timedate1_set_local_rtc (timedate1, local_rtc);
    openrc_settingsd_timedated_timedate1_set_ntp (timedate1, use_ntp);

    g_signal_connect (timedate1, "handle-set-time", G_CALLBACK (on_handle_set_time), NULL);
    g_signal_connect (timedate1, "handle-set-timezone", G_CALLBACK (on_handle_set_timezone), NULL);
    g_signal_connect (timedate1, "handle-set-local-rtc", G_CALLBACK (on_handle_set_local_rtc), NULL);
    g_signal_connect (timedate1, "handle-set-ntp", G_CALLBACK (on_handle_set_ntp), NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (timedate1),
                                           connection,
                                           "/org/freedesktop/timedate1",
                                           &err)) {
        if (err != NULL) {
            g_printerr ("Failed to export interface on /org/freedesktop/timedate1: %s\n", err->message);
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
timedated_init (gboolean _read_only,
                const gchar *_ntp_preferred_service)
{
    GError *err = NULL;

    read_only = _read_only;
    ntp_preferred_service = _ntp_preferred_service;

    hwclock_file = g_file_new_for_path (SYSCONFDIR "/conf.d/hwclock");
    timezone_file = g_file_new_for_path (SYSCONFDIR "/timezone");
    localtime_file = g_file_new_for_path (SYSCONFDIR "/localtime");

    local_rtc = get_local_rtc (&err);
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }
    timezone_name = get_timezone_name (&err);
    if (err != NULL) {
        g_warning ("%s", err->message);
        g_clear_error (&err);
    }
    use_ntp = service_started (ntp_service (), &err);
    if (err != NULL) {
        g_warning ("%s", err->message);
        g_clear_error (&err);
    }

    bus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.freedesktop.timedate1",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);
}

void
timedated_destroy (void)
{
    g_bus_unown_name (bus_id);
    bus_id = 0;
    read_only = FALSE;
    ntp_preferred_service = NULL;

    g_object_unref (hwclock_file);
    g_object_unref (timezone_file);
    g_object_unref (localtime_file);
}
