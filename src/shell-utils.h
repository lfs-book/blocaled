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

#ifndef _SHELL_UTILS_H_
#define _SHELL_UTILS_H_

#include <glib.h>
#include <gio/gio.h>

typedef struct _ShellUtilsTrivial ShellUtilsTrivial;

struct _ShellUtilsTrivial
{
  GFile *file;
  gchar *filename;
  GList *entry_list;
};

gchar *
shell_utils_source_var (GFile *file,
                        const gchar *variable,
                        GError **error);

void
shell_utils_init (void);

void
shell_utils_destroy (void);

ShellUtilsTrivial *
shell_utils_trivial_new (GFile *file,
                         GError **error);

ShellUtilsTrivial *
shell_utils_trivial_new_from_string (GFile *file,
                                     gchar *filebuf,
                                     GError **error);

void
shell_utils_trivial_free (ShellUtilsTrivial *trivial);

gboolean
shell_utils_trivial_is_empty (ShellUtilsTrivial *trivial);

gboolean
shell_utils_trivial_set_variable (ShellUtilsTrivial *trivial,
                                  const gchar *variable,
                                  const gchar *value,
                                  gboolean add_if_unset);

void
shell_utils_trivial_clear_variable (ShellUtilsTrivial *trivial,
                                    const gchar *variable);

gboolean
shell_utils_trivial_save (ShellUtilsTrivial *trivial,
                          GError **error);

gboolean
shell_utils_trivial_set_and_save (GFile *file,
                                  GError **error,
                                  const gchar *first_var_name,
                                  const gchar *first_alt_var_name,
                                  const gchar *first_value,
                                  ...);

gchar **
shell_utils_trivial_source_var_list (GFile *file,
                                     const gchar * const *var_names,
                                     GError **error);
#endif
