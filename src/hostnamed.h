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

#ifndef OPENRC_HOSTNAMED_H
#define OPENRC_HOSTNAMED_H

#include <glib.h>
#include <gio/gio.h>

void
hostnamed_on_bus_acquired (GDBusConnection *connection,
                 const gchar     *bus_name,
                 gpointer         user_data);

void
hostnamed_on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data);

void
hostnamed_on_name_lost (GDBusConnection *connection,
              const gchar     *bus_name,
              gpointer         user_data);

void
hostnamed_init (gboolean read_only);

void
hostnamed_destroy (void);

#endif
