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

#include <glib.h>
#include <gio/gio.h>
#include <polkit/polkit.h>

#include "bus-utils.h"

struct check_polkit_data {
    const gchar *unique_name;
    const gchar *action_id;
    gboolean user_interaction;
    GAsyncReadyCallback callback;
    gpointer user_data;

    PolkitAuthority *authority;
    PolkitSubject *subject;
};

void
check_polkit_data_free (struct check_polkit_data *data)
{
    if (data == NULL)
        return;

    if (data->subject != NULL)
        g_object_unref (data->subject);
    if (data->authority != NULL)
        g_object_unref (data->authority);
    
    g_free (data);
}

gboolean
check_polkit_finish (GAsyncResult *res,
                     GError **error)
{
    GSimpleAsyncResult *simple;

    simple = G_SIMPLE_ASYNC_RESULT (res);
    if (g_simple_async_result_propagate_error (simple, error))
        return FALSE;

    return g_simple_async_result_get_op_res_gboolean (simple);
}

static void
check_polkit_authorization_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer _data)
{
    struct check_polkit_data *data;
    PolkitAuthorizationResult *result;
    GSimpleAsyncResult *simple;
    GError *err = NULL;

    data = (struct check_polkit_data *) _data;
    if ((result = polkit_authority_check_authorization_finish (data->authority, res, &err)) == NULL) {
        g_simple_async_report_take_gerror_in_idle (NULL, data->callback, data->user_data, err);
        goto out;
    }
 
    if (!polkit_authorization_result_get_is_authorized (result)) {
        g_simple_async_report_error_in_idle (NULL, data->callback, data->user_data, POLKIT_ERROR, POLKIT_ERROR_NOT_AUTHORIZED, "Authorizing for '%s': not authorized", data->action_id);
        goto out;
    }
    simple = g_simple_async_result_new (NULL, data->callback, data->user_data, check_polkit_async);
    g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);

  out:
    check_polkit_data_free (data);
    if (result != NULL)
        g_object_unref (result);
}

static void
check_polkit_authority_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer _data)
{
    struct check_polkit_data *data;
    GError *err = NULL;

    data = (struct check_polkit_data *) _data;
    if ((data->authority = polkit_authority_get_finish (res, &err)) == NULL) {
        g_simple_async_report_take_gerror_in_idle (NULL, data->callback, data->user_data, err);
        check_polkit_data_free (data);
        return;
    }
    if (data->unique_name == NULL || data->action_id == NULL || 
        (data->subject = polkit_system_bus_name_new (data->unique_name)) == NULL) {
        g_simple_async_report_error_in_idle (NULL, data->callback, data->user_data, POLKIT_ERROR, POLKIT_ERROR_FAILED, "Authorizing for '%s': failed sanity check", data->action_id);
        check_polkit_data_free (data);
        return;
    }
    polkit_authority_check_authorization (data->authority, data->subject, data->action_id, NULL, (PolkitCheckAuthorizationFlags) data->user_interaction, NULL, check_polkit_authorization_cb, data);
}

void
check_polkit_async (const gchar *unique_name,
                    const gchar *action_id,
                    const gboolean user_interaction,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    struct check_polkit_data *data;

    data = g_new0 (struct check_polkit_data, 1);
    data->unique_name = unique_name;
    data->action_id = action_id;
    data->user_interaction = user_interaction;
    data->callback = callback;
    data->user_data = user_data;

    polkit_authority_get_async (NULL, check_polkit_authority_cb, data);
}
