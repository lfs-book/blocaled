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

typedef struct _ShellParser ShellParser;

struct _ShellParser
{
  GFile *file;
  gchar *filename;
  GList *entry_list;
};

/* Always return TRUE */
gboolean
_g_match_info_clear (GMatchInfo **match_info);

void
check_polkit_async (const gchar *unique_name,
                    const gchar *action_id,
                    const gboolean user_interaction,
                    GAsyncReadyCallback callback,
                    gpointer user_data);

gboolean
check_polkit_finish (GAsyncResult *res,
                     GError **error);

gchar *
shell_source_var (GFile *file,
                  const gchar *variable,
                  GError **error);

ShellParser *
shell_parser_new (GFile *file,
                  GError **error);

ShellParser *
shell_parser_new_from_string (GFile *file,
                              gchar *filebuf,
                              GError **error);

void
shell_parser_free (ShellParser *parser);

gboolean
shell_parser_is_empty (ShellParser *parser);

gboolean
shell_parser_set_variable (ShellParser *parser,
                                  const gchar *variable,
                                  const gchar *value,
                                  gboolean add_if_unset);

void
shell_parser_clear_variable (ShellParser *parser,
                             const gchar *variable);

gboolean
shell_parser_save (ShellParser *parser,
                   GError **error);

gboolean
shell_parser_set_and_save (GFile *file,
                           GError **error,
                           const gchar *first_var_name,
                           const gchar *first_alt_var_name,
                           const gchar *first_value,
                           ...);

gchar **
shell_parser_source_var_list (GFile *file,
                              const gchar * const *var_names,
                              GError **error);

void
utils_init (void);

void
utils_destroy (void);

#endif
