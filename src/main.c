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

#include "hostnamed.h"
#include "shell-utils.h"

#include "config.h"

gint
main (gint argc, gchar *argv[])
{
    GMainLoop *loop = NULL;

    g_type_init ();

    shell_utils_init ();
    hostnamed_init (FALSE);

    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);

    g_main_loop_unref (loop);
    hostnamed_destroy ();
    shell_utils_destroy ();
    return 0;
}