/*
  Copyright 2012 Alexandre Rostovtsev

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  Extracted from src/utils.c. Modified in 2019 by Pierre Labastie.
  See git log
*/

/*
  Pierre Labastie 2019: I'm discovering the PolKit interface, and also
  the glib, gio, etc API's, so it is slightly too steep a learning curve.
  I'll make detailed comments to tell what I understand, and help me
  remember it. Sorry for experimented devs.
*/

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gio.h>
#include <polkit/polkit.h>

#include "polkitasync.h"

#include "config.h"

/*
  We need to check whether a user is authorized to change the config files.
  For that, we ask the PolKit "Authority". There are two steps:
  - get the "ref" to the authority. Note that if the ref is null, it
    means polkitd is not running.
  - check the authorization.

  For checking the authorization, we need to pass:
  - the ref to the authority (PolkitAuthority *)
  - the subject (PolkitSubject *): a type describing what is asking
                                   the authorization
  - the action id (gchar *): the action for which the authorization is sought
  - some flags telling whether the user should interact with the system (enter
    a password for example)
  - a callback (which is called when the authorization check is complete)
  - user data (to pass to the callback).

  All the above needs to be retrieved after getting the authority, in the
  callback which is called at the end of the authority seek. So we need to
  pack it into a struct:
*/

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

/*
  We are called through the function "check_polkit_async", which
  just packs the needed data, and call the get_authority function. This
  function is passed a callback, which will get the result and the data,
  then call the polkit_check function. This function is itself passed
  a callback, which has two things to do:
  - get the result from the check (using polkit_check_finish)
  - propagate back the result
  The calling program will get the result through the function
  check_polkit_finish.
  To follow a clear workflow, we need to make forward references to the
  callbacks.
*/

static void
check_polkit_authority_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer _data);

static void
check_polkit_authorization_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer _data);

/**
 * check_polkit_async:
 * @unique_name: the connection who invoked the method for which an
 * authorization is sought
 * @action_id: what action (for us, either set-keyboard or set-locale)
 * @user_interaction: whether the user is allowed to interact for
 * getting the authorization
 * @callback: function to call when done
 * @user_data: a struct passed to the callback
 *
 * Check that the user associated with @unique_name is authorized
 * to perform @action_id. When the result is known, calls @callback
 * passing @user_data
 */
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

/* Note: the first parameter is a GCancellable. Passing NULL means the
         action cannot be cancelled (Hmmm, am I sure?). */
    polkit_authority_get_async (NULL, check_polkit_authority_cb, data);
}

/*
  Now the first callback: The authority_get action is complete, and we
  need to test it, and use it to get the authorization if available
*/

static void
check_polkit_authority_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer _data)
{
    struct check_polkit_data *data;
    GError *err = NULL;

    data = (struct check_polkit_data *) _data;
    if ((data->authority = polkit_authority_get_finish (res, &err)) == NULL) {
// I'm not sure about the second NULL...
        g_task_report_error (NULL, data->callback, data->user_data, NULL, err);
        check_polkit_data_free (data);
        return;
    }
    if (data->unique_name == NULL || data->action_id == NULL || 
        (data->subject = polkit_system_bus_name_new (data->unique_name)) == NULL) {
        g_task_report_new_error (NULL, data->callback, data->user_data, NULL, POLKIT_ERROR, POLKIT_ERROR_FAILED, "Authorizing for '%s': failed sanity check", data->action_id);
        check_polkit_data_free (data);
        return;
    }
    polkit_authority_check_authorization (data->authority, data->subject, data->action_id, NULL, (PolkitCheckAuthorizationFlags) data->user_interaction, NULL, check_polkit_authorization_cb, data);
}

/*
  Now the second callback: The check action is complete, and we
  need to test it, and Make it available to the _finish function
*/

static void
check_polkit_authorization_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer _data)
{
    struct check_polkit_data *data;
    PolkitAuthorizationResult *result;
    GTask *task;
    GError *err = NULL;

    data = (struct check_polkit_data *) _data;
    if ((result = polkit_authority_check_authorization_finish (data->authority, res, &err)) == NULL) {
        g_task_report_error (NULL, data->callback, data->user_data, NULL, err);
        goto out;
    }
 
    if (!polkit_authorization_result_get_is_authorized (result)) {
        g_task_report_new_error (NULL, data->callback, data->user_data, NULL, POLKIT_ERROR, POLKIT_ERROR_NOT_AUTHORIZED, "Authorizing for '%s': not authorized", data->action_id);
        goto out;
    }
    task = g_task_new (NULL, NULL, data->callback, data->user_data);
    g_task_return_boolean (task, TRUE);
//    g_simple_async_result_complete_in_idle (simple); Apparently this step
//    not needed with GTask
    g_object_unref (task);

  out:
    check_polkit_data_free (data);
    if (result != NULL)
        g_object_unref (result);
}

/**
 * check_polkit_finish:
 * @res: what has been received by the callback
 * @error: set if an error occurs
 *
 * extract the results of the authorization contained in @res
 *
 * returns: %TRUE if authorized, %FALSE otherwise
 */

gboolean
check_polkit_finish (GAsyncResult *res,
                     GError      **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

