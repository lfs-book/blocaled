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
*/

/* DEBUG begin: comment out when debugged
#include <stdio.h>
DEBUG end */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gio.h>

#include "shellparser.h"

#include "config.h"

static GRegex *indent_regex = NULL;
static GRegex *comment_regex = NULL;
static GRegex *separator_regex = NULL;
static GRegex *var_equals_regex = NULL;
static GRegex *single_quoted_regex = NULL;
static GRegex *double_quoted_regex = NULL;
static GRegex *unquoted_regex = NULL;

/* Always returns TRUE */
gboolean
_g_match_info_clear (GMatchInfo **match_info)
{
    if (match_info == NULL || *match_info == NULL)
        return TRUE;
    g_match_info_free (*match_info);
    *match_info = NULL;
    return TRUE;
}

/**
 * strstr0:
 * @haystack: (nullable) string to be searched in
 * @needle: (nullable) string to search
 *
 * Look for the string @needle inside the string @haystack. 
 *
 * Returns: a pointer to @needle inside @haystack if found, or %NULL
 * otherwise; also returns %NULL if any of the pointer to @haystack or
 * @needle is %NULL.
 */

gchar *
strstr0 (const gchar *haystack, const gchar *needle)
{
    if (haystack == NULL || needle == NULL)
        return NULL;
    return strstr (haystack, needle);
}

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
    gchar *unquoted_value; /* only relevant for assignments */
};

/**
 * shell_source_var:
 * @file: the file where the variable assignment is sought
 * @variable: the variable name: must be preceded by a `$' character
 * @error: set if an error occurs
 *
 * Have the shell source the file and diplay the value of @variable
 *
 * Returns: the value of @variable, or %NULL if not found in the file
 */

gchar *
shell_source_var (GFile *file,
                  const gchar *variable,
                  GError **error)
{
    gchar *argv[4] = { "sh", "-c", NULL, NULL };
    gchar *filename = NULL, *quoted_filename = NULL;
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
    g_free (filename);
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

    g_free (entry->string);
    g_free (entry->variable);
    g_free (entry->unquoted_value);
    g_free (entry);
}

/**
 * shell_parser_free:
 * @parser: a pointer to the ShellParser to free
 *
 * Remove a ShellParser from memory and free the memory
 */

void
shell_parser_free (ShellParser *parser)
{
    if (parser == NULL)
        return;

    if (parser->file != NULL)
        g_object_unref (parser->file);
    if (parser->filename != NULL)
        g_free (parser->filename);
    if (parser->entry_list != NULL)
        g_list_free_full (parser->entry_list, (GDestroyNotify)shell_entry_free);
    g_free (parser);
}

/**
 * shell_parser_new:
 * @file: the file associated to the new ShellParser
 * @error: allows returning an error
 *
 * Allocate memory for a new ShellParser associated to @file, and
 * parse the file, or return an empty ShellParser if the file
 * does not exist.
 *
 * Returns: a ShellParser. Free with #shell_parser_free
 */

ShellParser *
shell_parser_new (GFile *file,
                  GError **error)
{
    gchar *filebuf = NULL;
    GError *local_err = NULL;
    ShellParser *ret = NULL;

    if (file == NULL)
        return NULL;

    if (!g_file_load_contents (file, NULL, &filebuf, NULL, NULL, &local_err)) {
        if (local_err != NULL) {
            /* Inability to parse or open is a failure; file not existing at all is *not* a failure */
            if (local_err->code == G_IO_ERROR_NOT_FOUND) {
                g_error_free (local_err);
                ret = g_new0 (ShellParser, 1);
                g_object_ref (file);
                ret->file = file;
                ret->filename = g_file_get_path (file);
                return ret;
            } else {
                gchar *filename;
                filename = g_file_get_path (file);
                g_propagate_prefixed_error (error, local_err, "Unable to read '%s':", filename);
                g_free (filename);
            }
        }
        return NULL;
    }
    ret = shell_parser_new_from_string (file, filebuf, error);
    g_free (filebuf);
    return ret;
}

/**
 * shell_parser_new_from_string:
 * @file: the file being parsed
 * @filebuf: the raw content of the file as a string
 * @error: set if an error is encountered
 *
 * Allocate a new parser, and parse the content of @filebuf to it.
 * the following type of records are recognized:
 * - comment: from `#' to end of line
 * - indent: space at the beginning of a line
 * - separator: `;' or end of line (possibly surronded by space or
 *   blank line(s)
 * - assignments: form variable=value (there may be \`\'end-of-line between
 *   variable and \`=' and between \`=' and value. Values may be the
 *   concatenation of single quoted, double quoted, and unquoted strings,
 *   ending at the first unquoted space, \`|', and other characters. Values
 *   are stored unquoted, but may contain ${...} constructs (should we
 *   test?)
 *
 * Returns: (nullable) a ShellParser or %NULL in case of error.
 * Free with #shell_parser_free
 */

ShellParser *
shell_parser_new_from_string (GFile *file,
                              gchar *filebuf,
                              GError **error)
{
    ShellParser *ret = NULL;
    GError *local_err = NULL;
    gchar *s;

    if (file == NULL || filebuf == NULL)
        return NULL;

    ret = g_new0 (ShellParser, 1);
    g_object_ref (file);
    ret->file = file;
    ret->filename = g_file_get_path (file);

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
            _g_match_info_clear (&match_info);
            want_separator = FALSE;
            continue;
        }
        _g_match_info_clear (&match_info);

        matched = g_regex_match (separator_regex, s, 0, &match_info);
        if (matched) {
            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_SEPARATOR;
            entry->string = g_match_info_fetch (match_info, 0);
            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            s += strlen (entry->string);
            g_debug ("Scanned separator: ``%s''", entry->string);
            _g_match_info_clear (&match_info);
            want_separator = FALSE;
            continue;
        }
        _g_match_info_clear (&match_info);

        matched = g_regex_match (indent_regex, s, 0, &match_info);
        if (matched) {
            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_INDENT;
            entry->string = g_match_info_fetch (match_info, 0);
            ret->entry_list = g_list_prepend (ret->entry_list, entry);
            s += strlen (entry->string);
            g_debug ("Scanned indent: ``%s''", entry->string);
            _g_match_info_clear (&match_info);
            continue;
        }
        _g_match_info_clear (&match_info);

        matched = g_regex_match (var_equals_regex, s, 0, &match_info);
        if (matched) {
            gchar *raw_value = NULL, *temp1 = NULL, *temp2 = NULL;
            /* If we expect a separator and get an assignment instead, fail */
            if (want_separator)
                goto no_match;

            entry = g_new0 (struct ShellEntry, 1);
            entry->type = SHELL_ENTRY_TYPE_ASSIGNMENT;
            entry->string = g_match_info_fetch (match_info, 0);
            entry->variable = g_match_info_fetch (match_info, 1);
            s += strlen (entry->string);
            g_debug ("Scanned variable: ``%s''", entry->string);
            _g_match_info_clear (&match_info);
            want_separator = TRUE;

            while (*s != 0) {
                g_debug ("Scanning string for values: ``%s''", s);
                gboolean matched2 = FALSE;

                matched2 = g_regex_match (single_quoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                _g_match_info_clear (&match_info);

                matched2 = g_regex_match (double_quoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                _g_match_info_clear (&match_info);

                matched2 = g_regex_match (unquoted_regex, s, 0, &match_info);
                if (matched2)
                    goto append_value;

                _g_match_info_clear (&match_info);

                break;

  append_value:
                if (raw_value == NULL) {
                    raw_value = g_match_info_fetch (match_info, 0);
                    s += strlen (raw_value);
                    g_debug ("Scanned value: ``%s''", raw_value);
                } else {
                    temp1 = raw_value;
                    temp2 = g_match_info_fetch (match_info, 0);
                    raw_value = g_strconcat (temp1, temp2, NULL);
                    s += strlen (temp2);
                    g_debug ("Scanned value: ``%s''", temp2);
                    g_free (temp1);
                    g_free (temp2);
                }
                _g_match_info_clear (&match_info);
            }

            if (raw_value != NULL) {
                entry->unquoted_value = g_shell_unquote (raw_value, &local_err);
                g_debug  ("Unquoted value: ``%s''", entry->unquoted_value);
                temp1 = entry->string;
                temp2 = raw_value;
                entry->string = g_strconcat (temp1, temp2, NULL);
                g_free (temp1);
                g_free (temp2);
                ret->entry_list = g_list_prepend (ret->entry_list, entry);
                if (local_err != NULL)
                    goto no_match;
            }
            continue;
        }

  no_match:
        /* Nothing matches, parsing has failed! */
        _g_match_info_clear (&match_info);
        if (local_err != NULL)
            g_propagate_prefixed_error (error, local_err, "Unable to parse '%s':", ret->filename);
        else
            g_propagate_error (error,
                               g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                            "Unable to parse '%s'", ret->filename));
        shell_parser_free (ret);
        return NULL;
    }

    ret->entry_list = g_list_reverse (ret->entry_list);
    return ret;
}

/**
 * shell_parser_is_empty:
 * @parser: a ShellParser
 *
 * Test if a ShellParser is empty (possibly as the result of parsing a
 * non existent file)
 *
 * Returns: %TRUE if the parser is empty, %FALSE otherwise
 */

gboolean
shell_parser_is_empty (ShellParser *parser)
{
    if (parser == NULL || parser->entry_list == NULL)
        return TRUE;
    return FALSE;
}

/* DEBUG begin: comment out when debugged
void
print_parser (ShellParser *parser)
{
    GList *curr;
    int i;

    g_assert (parser != NULL);

    printf ("\nParser associated to %s:\n", parser->filename);
    printf ("List Pointer: %x\n", parser->entry_list);
    if (parser->entry_list == NULL)
        return;
    printf ("\nEntry List:\n");
    for (i = 1, curr = parser->entry_list; curr != NULL; curr = curr->next, i++) {
        struct ShellEntry *curr_entry = (struct ShellEntry *)curr->data;
        printf ("Entry #%d:\n", i);
        printf (" -- next: %x\n", curr->next);
        printf (" -- prev: %x\n", curr->prev);
        printf (" -- entry: %x\n", curr_entry);
        if (curr_entry != NULL) {
            printf (" --    -- type:      %d\n", curr_entry->type);
            printf (" --    -- string:    %s\n", curr_entry->string);
            if (curr_entry->type == SHELL_ENTRY_TYPE_ASSIGNMENT) {
                printf (" --    -- variable: %s\n", curr_entry->variable);
                printf (" --    -- value:    %s\n", curr_entry->unquoted_value);
            }
        }
    }
}
DEBUG end */

/**
 * shell_parser_set_variable:
 * @parser: (not nullable): the parser on which to act
 * @variable: (not nullable): the variable to set
 * @value: the (unquoted) value to store in variable
 * @add_if_unset: whether the variable should be added to the parser
 *
 * Look for variable in the assignment records of the parser. If found
 * set the value, and update the corresponding assignment string. If not
 * found, and @add_if_unset is set, add a new record containing the
 * variable, the value, and the assignment string.
 *
 * Returns:  %FALSE if the variable is not found and @add_if_unset is not set,
 * %TRUE otherwise
 */

gboolean
shell_parser_set_variable (ShellParser *parser,
                           const gchar *variable,
                           const gchar *value,
                           gboolean add_if_unset)
{
    GList *curr = NULL;
    GList *last = NULL;
    struct ShellEntry *found_entry = NULL;
    gchar *quoted_value = NULL;
    gboolean ret = FALSE;

    g_assert (parser != NULL);
    g_assert (variable != NULL);

/* DEBUG begin: comment out when debugged
    printf ("\nEntering shell_parser_set_variable\n"
            "----------------------------------\n");
    print_parser (parser);
DEBUG end */
    quoted_value = g_shell_quote (value);

    curr = parser->entry_list;
    while (curr != NULL) {
        struct ShellEntry *entry;

        entry = (struct ShellEntry *)(curr->data);
        if (entry->type == SHELL_ENTRY_TYPE_ASSIGNMENT &&
            g_strcmp0 (variable, entry->variable) == 0) {
            found_entry = entry;
            break;
        }
        last = curr;
        curr = curr->next;
    }

    if (found_entry != NULL) {
        g_free (found_entry->string);
        found_entry->string = g_strdup_printf ("%s=%s", variable, quoted_value);
	g_free (found_entry->unquoted_value);
        found_entry->unquoted_value = g_strdup(value);
        ret = TRUE;
    } else {
        if (add_if_unset) {
            struct ShellEntry *last_entry = NULL;
            GList *added = NULL;
            if (last != NULL)
                last_entry = (struct ShellEntry *)last->data;
            g_debug ("Adding variable %s. Last entry type is %d.\n"
                     "Last entry string is %s.",
                      variable,
                      last_entry ? last_entry->type : -1,
                      last_entry ? last_entry->string : "none");
            if (last_entry != NULL &&
                last_entry->type != SHELL_ENTRY_TYPE_SEPARATOR &&
                last_entry->type != SHELL_ENTRY_TYPE_COMMENT) {

                last_entry = g_new0 (struct ShellEntry, 1);
                last_entry->type = SHELL_ENTRY_TYPE_SEPARATOR;
                last_entry->string = g_strdup ("\n");
                added = g_list_alloc();
                added->next = NULL;
                added->prev = last;
                added->data = (gpointer)last_entry;
/* Note that last_entry is not NULL, so last is not NULL either */
                last->next = added;
                last = added;
            }
            found_entry = g_new0 (struct ShellEntry, 1);
            found_entry->type = SHELL_ENTRY_TYPE_ASSIGNMENT;
            found_entry->variable = g_strdup (variable);
            found_entry->unquoted_value = g_strdup(value);
            found_entry->string = g_strdup_printf ("%s=%s", variable,
                                                            quoted_value);
            added = g_list_alloc();
            added->next = NULL;
            added->prev = last;
            added->data = (gpointer)found_entry;
            if (last != NULL)
                last->next = added;
	    else
                parser->entry_list = added;
            last = added;
/* End the file with a newline char */
            last_entry = g_new0 (struct ShellEntry, 1);
            last_entry->type = SHELL_ENTRY_TYPE_SEPARATOR;
            last_entry->string = g_strdup_printf ("\n");
            added = g_list_alloc();
            added->next = NULL;
            added->prev = last;
            added->data = (gpointer)last_entry;
            last->next = added;
            ret = TRUE;
        }
    }

    g_free (quoted_value);
/* DEBUG begin: comment out when debugged
    printf ("\nExiting shell_parser_set_variable\n"
            "----------------------------------\n");
    print_parser (parser);
DEBUG end */
    return ret;
}

/**
 * shell_parser_clear_variable:
 * @parser: (not nullable): the parser on which to act
 * @variable: (not nullable): the variable to set
 *
 * Remove the assignment record containing @variable in @parser
 */

void
shell_parser_clear_variable (ShellParser *parser,
                             const gchar *variable)
{
    GList *curr = NULL;
    gboolean ret = FALSE;

    g_assert (parser != NULL);
    g_assert (variable != NULL);

/* DEBUG begin: comment out when debugged
    printf ("\nEntering shell_parser_clear_variable\n"
            "-----------------------------------\n");
    print_parser (parser);
DEBUG end */

    for (curr = parser->entry_list; curr != NULL; ) {
        struct ShellEntry *entry;

        entry = (struct ShellEntry *)(curr->data);
        if (entry->type == SHELL_ENTRY_TYPE_ASSIGNMENT && g_strcmp0 (variable, entry->variable) == 0) {
            GList *prev, *next;

            prev = curr->prev;
            next = curr->next;
            curr->prev = NULL;
            curr->next = NULL;
            g_list_free_full (curr, (GDestroyNotify)shell_entry_free);
            /* Normally, a variable assignment is between two (separator
             * or comment). But if the variable assignment is at the
             * beginning or the end of the file, either prev or next is NULL.
             * So that we have 9 cases:
             * prev      next      action
             *--------------------------------
             * NULL      NULL      nothing
             * NULL      separator remove next
             * NULL      comment   nothing
             * separator NULL      remove prev (not mandatory, just symmetry :)
             * separator separator remove next (either one, my choice :)
             * separator comment   remove prev
             * comment   NULL      nothing
             * comment   separator remove next
             * comment   comment   nothing
             *--------------------------------
             * Summary: if next is a separator, remove it, otherwise, if prev
             * is a separator, remove it.
             */
            if (next != NULL &&
                ((struct ShellEntry *)next->data)->type ==
                                           SHELL_ENTRY_TYPE_SEPARATOR) {
                GList *next_next = next->next;
                next->prev = next->next = NULL;
                g_list_free_full (next, (GDestroyNotify)shell_entry_free);
                next = next_next;
            } else if (prev != NULL &&
                       ((struct ShellEntry *)prev->data)->type ==
                                           SHELL_ENTRY_TYPE_SEPARATOR) {
                GList *prev_prev = prev->prev;
                prev->prev = prev->next = NULL;
                g_list_free_full (prev, (GDestroyNotify)shell_entry_free);
                prev = prev_prev;
            }
            if (prev != NULL)
                prev->next = next;
	    else
                parser->entry_list = next;
            if (next != NULL)
                next->prev = prev;
            curr = next;
        } else
            curr = curr->next;
    }
/* DEBUG begin: comment out when debugged
    printf ("\nExiting shell_parser_clear_variable\n"
            "-----------------------------------\n");
    print_parser (parser);
DEBUG end */
}

/**
 * shell_parser_save:
 * @parser: parser to write back to its file
 * @error: set in case of error
 *
 * Saves the parser back to its member file
 *
 * Returns: %FALSE in case of error, %TRUE if the operation succeeded.
 */

gboolean
shell_parser_save (ShellParser *parser,
                   GError **error)
{
    gboolean ret = FALSE;
    GList *curr = NULL;
    GFileOutputStream *os;

    g_assert (parser != NULL && parser->file != NULL && parser->filename != NULL);
    if ((os = g_file_replace (parser->file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error)) == NULL) {
        g_prefix_error (error, "Unable to save '%s': ", parser->filename);
        goto out;
    }

    for (curr = parser->entry_list; curr != NULL; curr = curr->next) {
        struct ShellEntry *entry;
        gsize written;

        entry = (struct ShellEntry *)(curr->data);
        if (!g_output_stream_write_all (G_OUTPUT_STREAM (os), entry->string, strlen (entry->string), &written, NULL, error)) {
            g_prefix_error (error, "Unable to save '%s': ", parser->filename);
            goto out;
        }
    }
    
    if (!g_output_stream_close (G_OUTPUT_STREAM (os), NULL, error)) {
        g_prefix_error (error, "Unable to save '%s': ", parser->filename);
        g_output_stream_close (G_OUTPUT_STREAM (os), NULL, NULL);
        goto out;
    }
    ret = TRUE;

  out:
    if (os)
        g_object_unref (os);
    return ret;
}

/**
 * shell_parser_set_and_save:
 * @file: file where the variables should be set
 * @error: set if an error occurs
 * @first_var_name: variable to be set, either if found in @parser, or if
 * not found and @first_alt_var_name is not found either
 * @first_alt_var_name: (nullable): variable to be set if found,
 * and @first_var_name is not
 * @first_value: the value to be stored in variable
 * @...: a series of triplets var_name, alt_var_name, value
 *
 * Parse the @file, Store the values into the associated variables, creating
 * them if necessary, and saves back the file
 *
 * Returns: %FALSE in case of error, %TRUE if the operation succeeded
 */

gboolean
shell_parser_set_and_save (GFile *file,
                           GError **error,
                           const gchar *first_var_name,
                           const gchar *first_alt_var_name,
                           const gchar *first_value,
                           ...)
{
    va_list ap;
    ShellParser *parser;
    gboolean ret = FALSE;
    const gchar *var_name, *alt_var_name, *value;

    va_start (ap, first_value);
    if ((parser = shell_parser_new (file, error)) == NULL)
        goto out;

    var_name = first_var_name;
    alt_var_name = first_alt_var_name;
    value = first_value;
    do {
        if (alt_var_name == NULL) {
            if (!shell_parser_set_variable (parser, var_name, value, TRUE)) {
                g_propagate_error (error,
                        g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                    "Unable to set %s in '%s'", var_name, parser->filename));
                goto out;
            }
        } else {
            if (!shell_parser_set_variable (parser, var_name, value, FALSE) &&
                !shell_parser_set_variable (parser, alt_var_name, value, FALSE) &&
                !shell_parser_set_variable (parser, var_name, value, TRUE)) {
                    g_propagate_error (error,
                            g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                        "Unable to set %s or %s in '%s'", var_name, alt_var_name, parser->filename));
                    goto out;
            }
        }
    } while ((var_name = va_arg (ap, const gchar*)) != NULL ?
                 alt_var_name = va_arg (ap, const gchar*), value = va_arg (ap, const gchar*), 1 : 0);

    if (!shell_parser_save (parser, error))
        goto out;

    ret = TRUE;

  out:
    va_end (ap);
    if (parser != NULL)
        shell_parser_free (parser);
    return ret;
}

/**
 * shell_parser_source_var_list:
 * @file: the file where variables assignments are sought
 * @var_names: list of variables whose values should be returned
 * @error: set if an error occurs
 *
 * Parse a file, and, for each variable in var_names, assign its value
 * at the same position in the returned vector. Note that if a file
 * contains twice an asignment to the same variable, only the second
 * is returned.
 *
 * Returns: A %NULL terminated vector of strings of the same size as
 * @var_names
 */

gchar **
shell_parser_source_var_list (GFile *file,
                              const gchar * const *var_names,
                              GError **error)
{
    ShellParser *parser;
    gchar **ret = NULL, **value;
    const gchar* const* var_name;

    if (var_names == NULL)
        return NULL;

    if ((parser = shell_parser_new (file, error)) == NULL)
        return NULL;

    ret = g_new0 (gchar *, g_strv_length ((gchar **)var_names) + 1);
    for (var_name = var_names, value = ret; *var_name != NULL; var_name++, value++) {
        GList *curr;
        for (curr = parser->entry_list; curr != NULL; curr = curr->next) {
            struct ShellEntry *entry;

            entry = (struct ShellEntry *)(curr->data);
            if (entry->type == SHELL_ENTRY_TYPE_ASSIGNMENT && g_strcmp0 (*var_name, entry->variable) == 0)
                *value = g_strdup (entry->unquoted_value);
        }
    }
    *value = NULL;
    shell_parser_free (parser);
    return ret;
}

/**
 * shell_parser_destroy:
 *
 * Free memory from the regexes allocated by shell_parser_init
 */

void
shell_parser_destroy (void)
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

/**
 * shell_parser_init:
 *
 * Set various regexes used for parsing files
 */

void
shell_parser_init (void)
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
        var_equals_regex = g_regex_new ("^(?:(?:export|local)[ \\t]+)?([a-zA-Z_][a-zA-Z0-9_]*)(?:(?:\\\\\\n)*)=(?:(?:\\\\\\n)*)", G_REGEX_ANCHORED|G_REGEX_MULTILINE, 0, NULL);
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
