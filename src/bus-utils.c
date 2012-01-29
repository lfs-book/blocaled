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

gboolean
check_polkit (const gchar *unique_name,
              const gchar *action_id,
              const gboolean user_interaction,
              GError **error)
{
    gboolean ret = FALSE;
    GDBusConnection *connection = NULL;
    PolkitAuthority *authority = NULL;
    PolkitSubject *subject = NULL;
    PolkitAuthorizationResult *result = NULL;

    if ((authority = polkit_authority_get_sync (NULL, error)) == NULL)
        goto end;

    if (unique_name == NULL || action_id == NULL || 
        (subject = polkit_system_bus_name_new (unique_name)) == NULL) {
        g_propagate_error (error,
                    g_error_new (POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                "Authorizing for '%s': failed sanity check", action_id));
        goto end;
    }

    if ((result = polkit_authority_check_authorization_sync (authority, subject, action_id, NULL, (PolkitCheckAuthorizationFlags) user_interaction, NULL, error)) == NULL)
        goto end;
 
    if ((ret = polkit_authorization_result_get_is_authorized (result)) == FALSE) {
        g_propagate_error (error,
                    g_error_new (POLKIT_ERROR, POLKIT_ERROR_NOT_AUTHORIZED,
                                "Authorizing for '%s': not authorized", action_id));
    }
                                                             
  end:
    if (result != NULL)
        g_object_unref (result);
    if (subject != NULL)
        g_object_unref (subject);
    if (authority != NULL)
        g_object_unref (authority);
    return ret;
}