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

#include <stdarg.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "shell-utils.h"

#include "config.h"

GRegex *indent_regex = NULL;
GRegex *comment_regex = NULL;
GRegex *separator_regex = NULL;
GRegex *var_equals_regex = NULL;
GRegex *single_quoted_regex = NULL;
GRegex *double_quoted_regex = NULL;
GRegex *unquoted_regex = NULL;

enum ShellEntryType {
    SHELL_ENTRY_TYPE_INDENT,
    SHELL_ENTRY_TYPE_COMMENT,
    SHELL_ENTRY_TYPE_SEPARATOR,
    SHELL_ENTRY_TYPE_ASSIGNMENT,
};

struct ShellEntry {
    enum ShellEntryType type;
    gchar *string;
    gchar *variable; /* only relevant for assignments */
};

gchar *
shell_utils_source_var (GFile *file,
                        const gchar *variable,
                        GError **error)
{
    gchar *argv[4] = { "sh", "-c", NULL, NULL };
    gchar *filename, *quoted_filename;
    gchar *output = NULL;
    GFileInfo *info;
    const GFileAttributeInfo *attribute_info;

    filename = g_file_get_path (file);
    if ((info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_ACCESS_CAN_READ, G_FILE_QUERY_INFO_NONE, NULL, error)) == NULL) {
        g_prefix_error (error, "Unable to source '%s': ", filename);
        goto out;
    }

    if (g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR &&
        g_file_info_get_file_type (info) != G_FILE_TYPE_SYMBOLIC_LINK) {
        g_propagate_error (error,
                           g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                        "Unable to source '%s': not a regular file", filename));
        goto out;
    }

    if (!g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
        g_propagate_error (error,
                           g_error_new (G_FILE_ERROR, G_FILE_ERROR_ACCES,
                                        "Unable to source '%s': permission denied", filename));
        goto out;
    }

    quoted_filename = g_shell_quote (filename);
    argv[2] = g_strdup_printf (". %s; echo -n %s", quoted_filename, variable);

    if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &output, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to source '%s': ", filename);
    }

  out:
    if (filename != NULL)
        g_free (filename);
    if (quoted_filename != NULL)
        g_free (quoted_filename);
    if (info != NULL)
        g_object_unref (info);
    if (argv[2] != NULL)
        g_free (argv[2]);
    return output;
}

static void
shell_entry_free (struct ShellEntry *entry)
{
    if (entry == NULL)
        return;

    if (entry->string != NULL)
        g_free (entry->string);
    if (entry->variable != NULL)
        g_free (entry->variable);
    g_free (entry);
}

void
shell_utils_trivial_free (ShellUtilsTrivial *trivial)
{
    if (trivial == NULL)
        return;

    if (trivial->file != NULL)
        g_object_unref (trivial->file);
    if (trivial->filename != NULL)
        g_free (trivial->filename);
    if (trivial->entry_list != NULL)
        g_list_free_full (trivial->entry_list, (GDestroyNotify)shell_entry_free);
    g_free (trivial);
}

ShellUtilsTrivial *
shell_utils_trivial_new (GFile *file,
                         GError **error)
{
    gchar *filebuf = NULL;
    ShellUtilsTrivial *ret = NULL;
    GError *local_err;
    gchar *s;

    if (file == NULL)
        return NULL;

    ret = g_new0 (ShellUtilsTrivial, 1);
    g_object_ref (file);
    ret->file = file;
    ret->filename = g_file_get_path (file);

    if (!g_file_load_contents (file, NULL, &filebuf, NULL, NULL, &local_err)) {
        if (local_err != NULL) {
            /* Inability to parse or open is a failure; file not existing at all is *not* a failure */
            if (local_err->code == G_IO_ERROR_NOT_FOUND) {
                g_error_free (local_err);
                return ret;
            }

            g_propagate_prefixed_error (error, local_err, "Unable to read '%s':", ret->filename);
        }
        shell_utils_trivial_free (ret);
        return NULL;
    }

    gboolean want_separator = FALSE; /* Do we expect the next entry to be a separator or comment? */
    s = filebuf;
    while (*s != 0) {
        g_debug ("Scanning string: ``%s''", s);
        gboolean matched = FALSE;
        GMatchInfo *match_info = NULL;
        struct ShellEntry *entry = NULL;
 
        matched = g_regex_match (comment_regex, s, 0, &match_info);
        if (matched) {
            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_COMMENT;
            entry->string = g_match_info_fetch (match_info, 0);
            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            s += strlen (entry->string);
            g_debug ("Scanned comment: ``%s''", entry->string);
            g_match_info_free (match_info);
            match_info = NULL;
            want_separator = FALSE;
            continue;
        }
        g_match_info_free (match_info);
        match_info = NULL;

        matched = g_regex_match (separator_regex, s, 0, &match_info);
        if (matched) {
            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_SEPARATOR;
            entry->string = g_match_info_fetch (match_info, 0);
            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            s += strlen (entry->string);
            g_debug ("Scanned separator: ``%s''", entry->string);
            g_match_info_free (match_info);
            match_info = NULL;
            want_separator = FALSE;
            continue;
        }
        g_match_info_free (match_info);
        match_info = NULL;

        matched = g_regex_match (indent_regex, s, 0, &match_info);
        if (matched) {
            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_INDENT;
            entry->string = g_match_info_fetch (match_info, 0);
            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            s += strlen (entry->string);
            g_debug ("Scanned indent: ``%s''", entry->string);
            g_match_info_free (match_info);
            match_info = NULL;
            continue;
        }
        g_match_info_free (match_info);
        match_info = NULL;

        matched = g_regex_match (var_equals_regex, s, 0, &match_info);
        if (matched) {
            /* If we expect a separator and get an assignment instead, fail */
            if (want_separator)
                goto no_match;

            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_ASSIGNMENT;
            entry->string = g_match_info_fetch (match_info, 0);
            entry->variable = g_match_info_fetch (match_info, 1);
            s += strlen (entry->string);
            g_debug ("Scanned variable: ``%s''", entry->string);
            g_match_info_free (match_info);
            match_info = NULL;
            want_separator = TRUE;

            while (*s != 0) {
                g_debug ("Scanning string for values: ``%s''", s);
                gboolean matched2 = FALSE;
                gchar *temp1 = NULL, *temp2 = NULL;

                matched2 = g_regex_match (single_quoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                g_match_info_free (match_info);
                match_info = NULL;

                matched2 = g_regex_match (double_quoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                g_match_info_free (match_info);
                match_info = NULL;

                matched2 = g_regex_match (unquoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                g_match_info_free (match_info);
                match_info = NULL;

                break;
append_value:
                temp1 = entry->string;
                temp2 = g_match_info_fetch (match_info, 0);
                entry->string = g_strconcat (temp1, temp2, NULL);
                s += strlen (temp2);
                g_debug ("Scanned value: ``%s''", temp2);
                g_free (temp1);
                g_free (temp2);
                g_match_info_free (match_info);
                match_info = NULL;
            }

            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            continue;
        }

no_match:
        /* Nothing matches, parsing has failed! */
        g_match_info_free (match_info);
        match_info = NULL;
        g_propagate_error (error,
                           g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                        "Unable to parse '%s'", ret->filename));
        shell_utils_trivial_free (ret);
        return NULL;
    }

    ret->entry_list = g_list_reverse (ret->entry_list);
    return ret;
}

gboolean
shell_utils_trivial_set_variable (ShellUtilsTrivial *trivial,
                                  const gchar *variable,
                                  const gchar *value,
                                  gboolean add_if_unset)
{
    GList *curr = NULL;
    struct ShellEntry *found_entry = NULL;
    gchar *quoted_value = NULL;
    gboolean ret = FALSE;

    g_assert (trivial != NULL);
    g_assert (variable != NULL);

    for (curr = trivial->entry_list; curr != NULL; curr = curr->next) {
        struct ShellEntry *entry;

        entry = (struct ShellEntry *)(curr->data);
        if (entry->type == SHELL_ENTRY_TYPE_ASSIGNMENT && g_strcmp0 (variable, entry->variable) == 0)
            found_entry = entry;
    }

    quoted_value = g_shell_quote (value);

    if (found_entry != NULL) {
        g_free (found_entry->string);
        found_entry->string = g_strdup_printf ("%s=%s", variable, quoted_value);
        ret = TRUE;
    } else {
        if (add_if_unset) {
            found_entry = g_new0 (struct ShellEntry, 1);
            found_entry->type = SHELL_ENTRY_TYPE_ASSIGNMENT;
            found_entry->variable = g_strdup (variable);
            found_entry->string = g_strdup_printf ("%s=%s", variable, quoted_value);
            trivial->entry_list = g_list_append (trivial->entry_list, found_entry);
            ret = TRUE;
        }
    }

    g_free (quoted_value);
    return ret;
}

gboolean
shell_utils_trivial_save (ShellUtilsTrivial *trivial,
                          GError **error)
{
    gboolean ret = FALSE;
    GList *curr = NULL;
    GFileOutputStream *os;

    g_assert (trivial != NULL && trivial->file != NULL && trivial->filename != NULL);
    if ((os = g_file_replace (trivial->file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error)) == NULL) {
        g_prefix_error (error, "Unable to save '%s': ", trivial->filename);
        goto out;
    }

    for (curr = trivial->entry_list; curr != NULL; curr = curr->next) {
        struct ShellEntry *entry;
        gsize written;

        entry = (struct ShellEntry *)(curr->data);
        if (!g_output_stream_write_all (G_OUTPUT_STREAM (os), entry->string, strlen (entry->string), &written, NULL, error)) {
            g_prefix_error (error, "Unable to save '%s': ", trivial->filename);
            goto out;
        }
    }
    
    if (!g_output_stream_close (G_OUTPUT_STREAM (os), NULL, error)) {
        g_prefix_error (error, "Unable to save '%s': ", trivial->filename);
        g_output_stream_close (G_OUTPUT_STREAM (os), NULL, NULL);
        goto out;
    }
    ret = TRUE;

  out:
    if (os)
        g_object_unref (os);
    return ret;
}

gboolean
shell_utils_trivial_set_and_save (GFile *file,
                                  GError **error,
                                  const gchar *first_var_name,
                                  const gchar *first_alt_var_name,
                                  const gchar *first_value,
                                  ...)
{
    va_list ap;
    ShellUtilsTrivial *trivial;
    gboolean ret = FALSE;
    const gchar *var_name, *alt_var_name, *value;

    va_start (ap, first_value);
    if ((trivial = shell_utils_trivial_new (file, error)) == NULL)
        goto out;

    var_name = first_var_name;
    alt_var_name = first_alt_var_name;
    value = first_value;
    do {
        if (alt_var_name == NULL) {
            if (!shell_utils_trivial_set_variable (trivial, var_name, value, TRUE)) {
                g_propagate_error (error,
                        g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                    "Unable to set %s in '%s'", var_name, trivial->filename));
                goto out;
            }
        } else {
            if (!shell_utils_trivial_set_variable (trivial, var_name, value, FALSE) &&
                !shell_utils_trivial_set_variable (trivial, alt_var_name, value, FALSE) &&
                !shell_utils_trivial_set_variable (trivial, var_name, value, TRUE)) {
                    g_propagate_error (error,
                            g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                        "Unable to set %s or %s in '%s'", var_name, alt_var_name, trivial->filename));
                    goto out;
            }
        }
    } while ((var_name = va_arg (ap, const gchar*)) != NULL ?
                 alt_var_name = va_arg (ap, const gchar*), value = va_arg (ap, const gchar*), 1 : 0);

    if (!shell_utils_trivial_save (trivial, error))
        goto out;

    ret = TRUE;

  out:
    va_end (ap);
    if (trivial != NULL)
        shell_utils_trivial_free (trivial);
    return ret;
}

void
shell_utils_destroy (void)
{
    if (indent_regex != NULL) {
        g_regex_unref (indent_regex);
        indent_regex = NULL;
    }
    if (comment_regex != NULL) {
        g_regex_unref (comment_regex);
        comment_regex = NULL;
    }
    if (separator_regex != NULL) {
        g_regex_unref (separator_regex);
        separator_regex = NULL;
    }
    if (var_equals_regex != NULL) {
        g_regex_unref (var_equals_regex);
        var_equals_regex = NULL;
    }
    if (single_quoted_regex != NULL) {
        g_regex_unref (single_quoted_regex);
        single_quoted_regex = NULL;
    }
    if (double_quoted_regex != NULL) {
        g_regex_unref (double_quoted_regex);
        double_quoted_regex = NULL;
    }
    if (unquoted_regex != NULL) {
        g_regex_unref (unquoted_regex);
        unquoted_regex = NULL;
    }
}

void
shell_utils_init (void)
{
    if (indent_regex == NULL) {
        indent_regex = g_regex_new ("^[ \\t]+", G_REGEX_ANCHORED, 0, NULL);
        g_assert (indent_regex != NULL);
    }
    if (comment_regex == NULL) {
        comment_regex = g_regex_new ("^#[^\\n]*\\n", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (comment_regex != NULL);
    }
    if (separator_regex == NULL) {
        separator_regex = g_regex_new ("^[ \\t;\\n\\r]*[;\\n][ \\t;\\n\\r]*", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (separator_regex != NULL);
    }
    if (var_equals_regex == NULL) {
        var_equals_regex = g_regex_new ("^([a-zA-Z_][a-zA-Z0-9_]*)(?:(?:\\\\\\n)*)=(?:(?:\\\\\\n)*)", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (var_equals_regex != NULL);
    }
    if (single_quoted_regex == NULL) {
        single_quoted_regex = g_regex_new ("^'[^']*'", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (single_quoted_regex != NULL);
    }
    /* We do not want to allow $(...) or `...` constructs in double-quoted
     * strings because they might have side effects, but ${...} is OK */
    if (double_quoted_regex == NULL) {
        double_quoted_regex = g_regex_new ("^\"(?:[^\"`\\$]|\\\\[\"`\\$]|\\$\\{)*\"", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (double_quoted_regex != NULL);
    }
    if (unquoted_regex == NULL) {
        unquoted_regex = g_regex_new ("^(?:[^\\s\"'`\\$\\|&<>;]|\\\\[\\s\"'`\\$\\|&<>;]|\\$\\{)+", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
        g_assert (unquoted_regex != NULL);
    }
}