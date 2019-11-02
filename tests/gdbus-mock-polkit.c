#include <gio/gio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------------------------------- */

static GDBusNodeInfo *introspection_data = NULL;

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.PolicyKit1.Authority'>"
  "    <method name='CheckAuthorization'>"
  "      <arg type='(sa{sv})' name='subject' direction='in'/>"
  "      <arg type='s' name='action_id' direction='in'/>"
  "      <arg type='a{ss}' name='details' direction='in'/>"
  "      <arg type='u' name='flags' direction='in'/>"
  "      <arg type='s' name='cancellation_id' direction='in'/>"
  "      <arg type='(bba{ss})' name='result' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

typedef enum
{
  POLKIT_ERROR_FAILED = 0,
  POLKIT_ERROR_CANCELLED = 1,
  POLKIT_ERROR_NOT_SUPPORTED = 2,
  POLKIT_ERROR_NOT_AUTHORIZED = 3
} PolkitError;

#define POLKIT_ERROR (polkit_error_quark())

static const GDBusErrorEntry polkit_error_entries[] =
{
  {POLKIT_ERROR_FAILED,         "org.freedesktop.PolicyKit1.Error.Failed"},
  {POLKIT_ERROR_CANCELLED,      "org.freedesktop.PolicyKit1.Error.Cancelled"},
  {POLKIT_ERROR_NOT_SUPPORTED,  "org.freedesktop.PolicyKit1.Error.NotSupported"},
  {POLKIT_ERROR_NOT_AUTHORIZED, "org.freedesktop.PolicyKit1.Error.NotAuthorized"},
};

GQuark
polkit_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("polkit-error-quark",
                                      &quark_volatile,
                                      polkit_error_entries,
                                      G_N_ELEMENTS (polkit_error_entries));
  G_STATIC_ASSERT (G_N_ELEMENTS (polkit_error_entries) - 1 == POLKIT_ERROR_NOT_AUTHORIZED);
  return (GQuark) quark_volatile;
}


/* ---------------------------------------------------------------------------------------------------- */

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  if (g_strcmp0 (method_name, "CheckAuthorization") == 0)
    {
      const gchar *action_id;

      g_variant_get (parameters, "((sa{sv})&sa{ss}us)", NULL, NULL, &action_id, NULL, NULL, NULL);

      if ((g_strcmp0 (action_id, "org.freedesktop.locale1.set-locale") != 0) &&
          (g_strcmp0 (action_id, "org.freedesktop.locale1.set-keyboard") != 0))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 POLKIT_ERROR,
                                                 POLKIT_ERROR_NOT_SUPPORTED,
                                                 "Mock Polkit only supports locale1 actions");
        }
      else
        {
          GVariantBuilder *builder;
	  builder = g_variant_builder_new (G_VARIANT_TYPE("a{ss}"));
	  g_variant_builder_add (builder, "{ss}",
		                 "polkit.retains_authorization_after_challenge",
				 "true");
	  g_variant_builder_add (builder, "{ss}",
		                 "polkit.temporary_authorization_id",
				 "tmpauthz1");
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("((bba{ss}))",
					         TRUE,
						 FALSE,
						 builder));
          g_variant_builder_unref (builder);
        }
    }
}

/* for now */
static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  NULL,
  NULL
};

/* ---------------------------------------------------------------------------------------------------- */

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  guint registration_id;

  registration_id = g_dbus_connection_register_object (connection,
                                                       "/org/freedesktop/PolicyKit1/Authority",
                                                       introspection_data->interfaces[0],
                                                       &interface_vtable,
                                                       NULL,  /* user_data */
                                                       NULL,  /* user_data_free_func */
                                                       NULL); /* GError** */
  g_assert (registration_id > 0);

}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  exit (1);
}

int
main (int argc, char *argv[])
{
  guint owner_id;
  GMainLoop *loop;

  /* We are lazy here - we don't want to manually provide
   * the introspection data structures - so we just build
   * them from XML.
   */
  introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
  g_assert (introspection_data != NULL);

  owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.freedesktop.PolicyKit1",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_bus_unown_name (owner_id);

  g_dbus_node_info_unref (introspection_data);

  return 0;
}
