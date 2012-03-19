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

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-protocol.h>
#include <glib.h>
#include <gio/gio.h>

#include "localed.h"
#include "locale1-generated.h"
#include "bus-utils.h"
#include "shell-utils.h"

#include "config.h"

#define SERVICE_NAME "openrc-settingsd localed"

static guint bus_id = 0;
static gboolean read_only = FALSE;

static OpenrcSettingsdLocaledLocale1 *locale1 = NULL;

static gchar *locale_variables[] = {
    "LANG", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION", NULL
};

static gchar **locale = NULL; /* Expected format is { "LANG=foo", "LC_TIME=bar", NULL } */
static GFile *locale_file = NULL;
G_LOCK_DEFINE_STATIC (locale);

static gchar *vconsole_keymap = NULL;
static gchar *vconsole_keymap_toggle = NULL;
static GFile *keymaps_file = NULL;
G_LOCK_DEFINE_STATIC (keymaps);

static gchar *x11_layout = NULL;
static gchar *x11_model = NULL;
static gchar *x11_variant = NULL;
static gchar *x11_options = NULL;
static GFile *x11_gentoo_file = NULL;
static GFile *x11_systemd_file = NULL;
G_LOCK_DEFINE_STATIC (xorg_conf);

/* Trivial /etc/X11/xorg.conf.d/30-keyboard.conf parser */

enum XORG_CONFD_LINE_TYPE {
    XORG_CONFD_LINE_TYPE_UNKNOWN,
    XORG_CONFD_LINE_TYPE_COMMENT,
    XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS,
    XORG_CONFD_LINE_TYPE_SECTION_OTHER,
    XORG_CONFD_LINE_TYPE_END_SECTION,
    XORG_CONFD_LINE_TYPE_MATCH_IS_KEYBOARD,
    XORG_CONFD_LINE_TYPE_XKB_LAYOUT,
    XORG_CONFD_LINE_TYPE_XKB_MODEL,
    XORG_CONFD_LINE_TYPE_XKB_VARIANT,
    XORG_CONFD_LINE_TYPE_XKB_OPTIONS,
};

GRegex *xorg_confd_line_comment_re = NULL;
GRegex *xorg_confd_line_section_input_class_re = NULL;
GRegex *xorg_confd_line_section_re = NULL;
GRegex *xorg_confd_line_end_section_re = NULL;
GRegex *xorg_confd_line_match_is_keyboard_re = NULL;
GRegex *xorg_confd_line_xkb_layout_re = NULL;
GRegex *xorg_confd_line_xkb_model_re = NULL;
GRegex *xorg_confd_line_xkb_variant_re = NULL;
GRegex *xorg_confd_line_xkb_options_re = NULL;

struct xorg_confd_line_entry {
    gchar *string;
    gchar *value; /* for one of the options we are interested in */
    enum XORG_CONFD_LINE_TYPE type;
};

struct xorg_confd_parser {
    GFile *file;
    gchar *filename;
    GList *line_list;
    GList *section; /* start of relevant InputClass section */
};

static void
xorg_confd_regex_destroy ()
{
    if (xorg_confd_line_comment_re != NULL) {
        g_regex_unref (xorg_confd_line_comment_re);
        xorg_confd_line_comment_re = NULL;
    }
    if (xorg_confd_line_section_input_class_re != NULL) {
        g_regex_unref (xorg_confd_line_section_input_class_re);
        xorg_confd_line_section_input_class_re = NULL;
    }
    if (xorg_confd_line_section_re != NULL) {
        g_regex_unref (xorg_confd_line_section_re);
        xorg_confd_line_section_re = NULL;
    }
    if (xorg_confd_line_end_section_re != NULL) {
        g_regex_unref (xorg_confd_line_end_section_re);
        xorg_confd_line_end_section_re = NULL;
    }
    if (xorg_confd_line_match_is_keyboard_re != NULL) {
        g_regex_unref (xorg_confd_line_match_is_keyboard_re);
        xorg_confd_line_match_is_keyboard_re = NULL;
    }
    if (xorg_confd_line_xkb_layout_re != NULL) {
        g_regex_unref (xorg_confd_line_xkb_layout_re);
        xorg_confd_line_xkb_layout_re = NULL;
    }
    if (xorg_confd_line_xkb_model_re != NULL) {
        g_regex_unref (xorg_confd_line_xkb_model_re);
        xorg_confd_line_xkb_model_re = NULL;
    }
    if (xorg_confd_line_xkb_variant_re != NULL) {
        g_regex_unref (xorg_confd_line_xkb_variant_re);
        xorg_confd_line_xkb_variant_re = NULL;
    }
    if (xorg_confd_line_xkb_options_re != NULL) {
        g_regex_unref (xorg_confd_line_xkb_options_re);
        xorg_confd_line_xkb_options_re = NULL;
    }
}

static void
xorg_confd_regex_init ()
{
    if (xorg_confd_line_comment_re == NULL) {
        xorg_confd_line_comment_re = g_regex_new ("^\\s*#", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_comment_re != NULL);
    }
    if (xorg_confd_line_section_input_class_re == NULL) {
        xorg_confd_line_section_input_class_re = g_regex_new ("^\\s*Section\\s+\"InputClass\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_section_input_class_re != NULL);
    }
    if (xorg_confd_line_section_re == NULL) {
        xorg_confd_line_section_re = g_regex_new ("^\\s*Section\\s+\"([^\"])\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_section_re != NULL);
    }
    if (xorg_confd_line_end_section_re == NULL) {
        xorg_confd_line_end_section_re = g_regex_new ("^\\s*EndSection", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_end_section_re != NULL);
    }
    if (xorg_confd_line_match_is_keyboard_re == NULL) {
        xorg_confd_line_match_is_keyboard_re = g_regex_new ("^\\s*MatchIsKeyboard(?:\\s*$|\\s+\"(?:1|on|true|yes)\")", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_match_is_keyboard_re != NULL);
    }
    if (xorg_confd_line_xkb_layout_re == NULL) {
        xorg_confd_line_xkb_layout_re = g_regex_new ("^(\\s*Option\\s+\"XkbLayout\"\\s+)\"([^\"]*)\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_xkb_layout_re != NULL);
    }
    if (xorg_confd_line_xkb_model_re == NULL) {
        xorg_confd_line_xkb_model_re = g_regex_new ("^(\\s*Option\\s+\"XkbModel\"\\s+)\"([^\"]*)\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_xkb_model_re != NULL);
    }
    if (xorg_confd_line_xkb_variant_re == NULL) {
        xorg_confd_line_xkb_variant_re = g_regex_new ("^(\\s*Option\\s+\"XkbVariant\"\\s+)\"([^\"]*)\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_xkb_variant_re != NULL);
    }
    if (xorg_confd_line_xkb_options_re == NULL) {
        xorg_confd_line_xkb_options_re = g_regex_new ("^(\\s*Option\\s+\"XkbOptions\"\\s+)\"([^\"]*)\"", G_REGEX_ANCHORED|G_REGEX_CASELESS, 0, NULL);
        g_assert (xorg_confd_line_xkb_options_re != NULL);
    }
}

static void
xorg_confd_line_entry_free (struct xorg_confd_line_entry *entry)
{
    if (entry == NULL)
        return;

    g_free (entry->string);
    g_free (entry->value);

    g_free (entry);
}

/* Note that string and value are not duplicated */
static struct xorg_confd_line_entry *
xorg_confd_line_entry_new (const gchar *string,
                           const gchar *value,
                           enum XORG_CONFD_LINE_TYPE type)
{
    struct xorg_confd_line_entry *entry;

    entry = g_new0 (struct xorg_confd_line_entry, 1);
    entry->string = g_strdup (string);
    entry->value = g_strdup (value);
    entry->type = type;
}

static void
xorg_confd_parser_free (struct xorg_confd_parser *parser)
{
    if (parser == NULL)
        return;

    if (parser->file != NULL)
        g_object_unref (parser->file);

    g_free (parser->filename);

    if (parser->line_list != NULL)
        g_list_free_full (parser->line_list, (GDestroyNotify)xorg_confd_line_entry_free);

    g_free (parser);
}

static struct xorg_confd_parser *
xorg_confd_parser_new (GFile *xorg_confd_file,
                       GError **error)
{
    struct xorg_confd_parser *parser = NULL;
    gchar *filebuf = NULL;
    gchar **linebuf = NULL;
    gchar **lines = NULL;
    GList *input_class_section_start = NULL;
    gboolean in_section = FALSE, in_xkb_section = FALSE;

    if (xorg_confd_file == NULL)
        return NULL;

    parser = g_new0 (struct xorg_confd_parser, 1);
    parser->file = g_object_ref (xorg_confd_file);
    parser->filename = g_file_get_path (xorg_confd_file);
    g_debug ("Parsing xorg.conf.d file: '%s'", parser->filename);
    if (!g_file_load_contents (xorg_confd_file, NULL, &filebuf, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", parser->filename);
        goto fail;
    }

    lines = g_strsplit (filebuf, "\n", 0);
    if (lines == NULL)
        goto out;

    for (linebuf = lines; *linebuf != NULL; linebuf++) {
        struct xorg_confd_line_entry *entry = NULL;
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;

        entry = xorg_confd_line_entry_new (*linebuf, NULL, XORG_CONFD_LINE_TYPE_UNKNOWN);

        if (g_regex_match (xorg_confd_line_comment_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as comment", *linebuf);
            entry->type = XORG_CONFD_LINE_TYPE_COMMENT;
        } else if (g_regex_match (xorg_confd_line_section_input_class_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as InputClass section", *linebuf);
            if (in_section)
                goto no_match;
            in_section = TRUE;
            entry->type = XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS;
        } else if (g_regex_match (xorg_confd_line_section_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as non-InputClass section", *linebuf);
            if (in_section)
                goto no_match;
            in_section = TRUE;
            entry->type = XORG_CONFD_LINE_TYPE_SECTION_OTHER;
        } else if (g_regex_match (xorg_confd_line_end_section_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as end of section", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_END_SECTION;
        } else if (g_regex_match (xorg_confd_line_match_is_keyboard_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as MatchIsKeyboard declaration", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_MATCH_IS_KEYBOARD;
            in_xkb_section = TRUE;
        } else if (g_regex_match (xorg_confd_line_xkb_layout_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbLayout option", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_LAYOUT;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (g_regex_match (xorg_confd_line_xkb_model_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbModel option", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_MODEL;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (g_regex_match (xorg_confd_line_xkb_variant_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbVariant option", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_VARIANT;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (g_regex_match (xorg_confd_line_xkb_options_re, *linebuf, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbOptions option", *linebuf);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_OPTIONS;
            entry->value = g_match_info_fetch (match_info, 2);
        }

        if (entry->type == XORG_CONFD_LINE_TYPE_UNKNOWN)
            g_debug ("Parsing line '%s' as unknown", *linebuf);

        g_match_info_free (match_info);
        parser->line_list = g_list_prepend (parser->line_list, entry);
        if (in_section) {
            if (entry->type == XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS)
                input_class_section_start = parser->line_list;
            else if (entry->type == XORG_CONFD_LINE_TYPE_END_SECTION) {
                if (in_xkb_section)
                    parser->section = input_class_section_start;

                input_class_section_start = NULL;
                in_section = FALSE;
                in_xkb_section = FALSE;
            }
        }
        continue;

  no_match:
        /* Nothing matched... */
        g_free (entry);
        g_match_info_free (match_info);
        goto parse_fail;
    }

    if (in_section) {
        /* Unterminated section */
        goto parse_fail;
    }

    parser->line_list = g_list_reverse (parser->line_list);

  out:
    g_free (filebuf);
    g_strfreev (lines);
    return parser;

  parse_fail:
    g_propagate_error (error,
                       g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                   "Unable to parse '%s'", parser->filename));
  fail:
    g_free (filebuf);
    g_strfreev (lines);
    xorg_confd_parser_free (parser);
    return NULL;
}

static void
xorg_confd_parser_get_xkb (const struct xorg_confd_parser *parser,
                           gchar **layout_p,
                           gchar **model_p,
                           gchar **variant_p,
                           gchar **options_p)
{
    GList *curr = NULL;
    gchar *layout = NULL, *model = NULL, *variant = NULL, *options = NULL;

    if (parser == NULL)
        return;
    for (curr = parser->section; curr != NULL; curr = curr->next) {
        GMatchInfo *match_info = NULL;
        struct xorg_confd_line_entry *entry = (struct xorg_confd_line_entry *) curr->data;

        if (entry->type == XORG_CONFD_LINE_TYPE_END_SECTION)
            break;
        else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_LAYOUT)
            layout = entry->value;
        else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_MODEL)
            model = entry->value;
        else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_VARIANT)
            variant = entry->value;
        else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_OPTIONS)
            options = entry->value;
    }
    *layout_p = g_strdup (layout);
    *model_p = g_strdup (model);
    *variant_p = g_strdup (variant);
    *options_p = g_strdup (options);
}

static GList *
xorg_confd_parser_line_set_or_delete (GList *line,
                                      const gchar *value,
                                      const GRegex *re)
{
    gchar *replacement = NULL, *replaced = NULL;

    g_assert (line != NULL);

    struct xorg_confd_line_entry *entry = (struct xorg_confd_line_entry *) line->data;

    if (value == NULL || !g_strcmp0 (value, "")) {
        /* If value is null, we delete the line and return previous one */
        GList *prev = line->prev;
        prev->next = line->next;
        prev->next->prev = prev;
        line->prev = NULL;
        line->next = NULL;
        g_list_free_full (line, (GDestroyNotify)xorg_confd_line_entry_free);
        return prev;
    }
    entry->value = g_strdup (value);
    replacement = g_strdup_printf ("\1\"%s\"", value);
    replaced = g_regex_replace (re, entry->string, 0, 0, replacement, 0, NULL);
    g_free (replacement);
    g_free (entry->string);
    entry->string = replaced;

    return line;
}

static void
xorg_confd_parser_set_xkb (struct xorg_confd_parser *parser,
                           const gchar *layout,
                           const gchar *model,
                           const gchar *variant,
                           const gchar *options)
{
    GList *curr = NULL, *end = NULL;
    gboolean layout_found = FALSE, model_found = FALSE, variant_found = FALSE, options_found = FALSE;

    if (parser == NULL)
        return;

    if (parser->section == NULL) {
        struct xorg_confd_line_entry *entry = NULL;
        GList *section = NULL;

        entry = xorg_confd_line_entry_new ("Section \"InputClass\"\n", NULL, XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS);
        section = g_list_prepend (section, entry);

        entry = xorg_confd_line_entry_new ("        Identifier \"keyboard-all\"\n", NULL, XORG_CONFD_LINE_TYPE_UNKNOWN);
        section = g_list_prepend (section, entry);

        entry = entry = xorg_confd_line_entry_new ("        MatchIsKeyboard \"on\"\n", NULL, XORG_CONFD_LINE_TYPE_MATCH_IS_KEYBOARD);
        section = g_list_prepend (section, entry);

        entry = entry = xorg_confd_line_entry_new ("EndSection\n", NULL, XORG_CONFD_LINE_TYPE_END_SECTION);
        section = g_list_prepend (section, entry);

        section = g_list_reverse (section);
        parser->section = section;
        parser->line_list = g_list_concat (parser->line_list, section);
    }

    for (curr = parser->section; curr != NULL; curr = curr->next) {
        struct xorg_confd_line_entry *entry = (struct xorg_confd_line_entry *) curr->data;

        if (entry->type == XORG_CONFD_LINE_TYPE_END_SECTION) {
            end = curr;
            break;
        } else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_LAYOUT) {
            layout_found = TRUE;
            curr = xorg_confd_parser_line_set_or_delete (curr, layout, xorg_confd_line_xkb_layout_re);
        } else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_MODEL) {
            model_found = TRUE;
            curr = xorg_confd_parser_line_set_or_delete (curr, model, xorg_confd_line_xkb_model_re);
        } else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_VARIANT) {
            variant_found = TRUE;
            curr = xorg_confd_parser_line_set_or_delete (curr, variant, xorg_confd_line_xkb_variant_re);
        } else if (entry->type == XORG_CONFD_LINE_TYPE_XKB_OPTIONS) {
            options_found = TRUE;
            curr = xorg_confd_parser_line_set_or_delete (curr, options, xorg_confd_line_xkb_options_re);
        }
    }
    if (!layout_found && layout != NULL && g_strcmp0 (layout, "")) {
        struct xorg_confd_line_entry *entry;
        gchar *string;

        string = g_strdup_printf ("        Option \"XkbLayout\" \"%s\"", layout);
        entry = xorg_confd_line_entry_new (string, layout, XORG_CONFD_LINE_TYPE_XKB_LAYOUT);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!model_found && model != NULL && g_strcmp0 (model, "")) {
        struct xorg_confd_line_entry *entry;
        gchar *string;

        string = g_strdup_printf ("        Option \"XkbModel\" \"%s\"", model);
        entry = xorg_confd_line_entry_new (string, model, XORG_CONFD_LINE_TYPE_XKB_MODEL);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!variant_found && variant != NULL && g_strcmp0 (variant, "")) {
        struct xorg_confd_line_entry *entry;
        gchar *string;

        string = g_strdup_printf ("        Option \"XkbVariant\" \"%s\"", variant);
        entry = xorg_confd_line_entry_new (string, variant, XORG_CONFD_LINE_TYPE_XKB_VARIANT);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!options_found && options != NULL && g_strcmp0 (options, "")) {
        struct xorg_confd_line_entry *entry;
        gchar *string;

        string = g_strdup_printf ("        Option \"XkbOptions\" \"%s\"", options);
        entry = xorg_confd_line_entry_new (string, options, XORG_CONFD_LINE_TYPE_XKB_OPTIONS);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
}

static gboolean
xorg_confd_parser_save (const struct xorg_confd_parser *parser,
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

    for (curr = parser->line_list; curr != NULL; curr = curr->next) {
        struct xorg_confd_line_entry *entry = (struct xorg_confd_line_entry *) curr->data;
        gsize written;

        if (!g_output_stream_write_all (G_OUTPUT_STREAM (os), entry->string, strlen (entry->string), &written, NULL, error)) {
            g_prefix_error (error, "Unable to save '%s': ", parser->filename);
            goto out;
        }
        if (!g_output_stream_write_all (G_OUTPUT_STREAM (os), "\n", 1, &written, NULL, error)) {
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

/* End of trivial /etc/X11/xorg.conf.d/30-keyboard.conf parser */

static gboolean
on_handle_set_locale (OpenrcSettingsdLocaledLocale1 *locale1,
                      GDBusMethodInvocation *invocation,
                      const gchar * const *_locale,
                      const gboolean user_interaction,
                      gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static gboolean
on_handle_set_vconsole_keyboard (OpenrcSettingsdLocaledLocale1 *locale1,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *keymap,
                                 const gchar *keymap_toggle,
                                 const gboolean convert,
                                 const gboolean user_interaction,
                                 gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static gboolean
on_handle_set_x11_keyboard (OpenrcSettingsdLocaledLocale1 *locale1,
                            GDBusMethodInvocation *invocation,
                            const gchar *layout,
                            const gchar *model,
                            const gchar *variant,
                            const gchar *options,
                            const gboolean convert,
                            const gboolean user_interaction,
                            gpointer user_data)
{
    g_dbus_method_invocation_return_dbus_error (invocation,
                                                DBUS_ERROR_NOT_SUPPORTED,
                                                SERVICE_NAME " is in read-only mode");

    return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *bus_name,
                 gpointer         user_data)
{
    gchar *name;
    GError *err = NULL;

    g_debug ("Acquired a message bus connection");

    locale1 = openrc_settingsd_localed_locale1_skeleton_new ();

    openrc_settingsd_localed_locale1_set_locale (locale1, (const gchar * const *) locale);
    openrc_settingsd_localed_locale1_set_vconsole_keymap (locale1, vconsole_keymap);
    openrc_settingsd_localed_locale1_set_vconsole_keymap_toggle (locale1, vconsole_keymap_toggle);
    openrc_settingsd_localed_locale1_set_x11_layout (locale1, x11_layout);
    openrc_settingsd_localed_locale1_set_x11_model (locale1, x11_model);
    openrc_settingsd_localed_locale1_set_x11_variant (locale1, x11_variant);
    openrc_settingsd_localed_locale1_set_x11_options (locale1, x11_options);

    g_signal_connect (locale1, "handle-set-locale", G_CALLBACK (on_handle_set_locale), NULL);
    g_signal_connect (locale1, "handle-set-vconsole-keyboard", G_CALLBACK (on_handle_set_vconsole_keyboard), NULL);
    g_signal_connect (locale1, "handle-set-x11-keyboard", G_CALLBACK (on_handle_set_x11_keyboard), NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (locale1),
                                           connection,
                                           "/org/freedesktop/locale1",
                                           &err)) {
        if (err != NULL) {
            g_printerr ("Failed to export interface on /org/freedesktop/locale1: %s\n", err->message);
            g_error_free (err);
        }
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data)
{
    g_debug ("Acquired the name %s", bus_name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *bus_name,
              gpointer         user_data)
{
    if (connection == NULL)
        g_printerr ("Failed to acquire a dbus connection\n");
    else
        g_printerr ("Failed to acquire dbus name %s\n", bus_name);
    exit(-1);
}

void
localed_init (gboolean _read_only)
{
    GError *err = NULL;
    gchar **locale_values = NULL;
    struct xorg_confd_parser *x11_parser = NULL;

    read_only = _read_only;
    locale_file = g_file_new_for_path (SYSCONFDIR "/env.d/02locale");
    keymaps_file = g_file_new_for_path (SYSCONFDIR "/conf.d/keymaps");

    /* See http://www.gentoo.org/doc/en/xorg-config.xml */
    x11_gentoo_file = g_file_new_for_path (SYSCONFDIR "/X11/xorg.conf.d/30-keyboard.conf");
    x11_systemd_file = g_file_new_for_path (SYSCONFDIR "/X11/xorg.conf.d/00-keyboard.conf");

    locale = g_new0 (gchar *, g_strv_length (locale_variables) + 1);
    locale_values = shell_utils_trivial_source_var_list (locale_file, (const gchar * const *)locale_variables, &err);
    if (locale_values != NULL) {
        gchar **variable, **value, **loc;
        loc = locale;
        for (variable = locale_variables, value = locale_values; *variable != NULL; variable++, value++) {
            if (*value != NULL) {
                *loc = g_strdup_printf ("%s=%s", *variable, *value);
                loc++;
            }
        }
            
        g_strfreev (locale_values);
    }
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    vconsole_keymap = shell_utils_source_var (keymaps_file, "${keymap}", &err);
    if (vconsole_keymap == NULL)
        vconsole_keymap = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    /* We don't have a good equivalent for this in openrc at the moment */
    vconsole_keymap_toggle = g_strdup ("");

    xorg_confd_regex_init ();

    if (!g_file_query_exists (x11_gentoo_file, NULL) && g_file_query_exists (x11_systemd_file, NULL))
        x11_parser = xorg_confd_parser_new (x11_systemd_file, &err);
    else
        x11_parser = xorg_confd_parser_new (x11_gentoo_file, &err);

    if (x11_parser != NULL) {
        xorg_confd_parser_get_xkb (x11_parser, &x11_layout, &x11_model, &x11_variant, &x11_options);
        xorg_confd_parser_free (x11_parser);
    } else {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    bus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                             "org.freedesktop.locale1",
                             G_BUS_NAME_OWNER_FLAGS_NONE,
                             on_bus_acquired,
                             on_name_acquired,
                             on_name_lost,
                             NULL,
                             NULL);
}

void
localed_destroy (void)
{
    g_bus_unown_name (bus_id);
    bus_id = 0;
    read_only = FALSE;
    g_strfreev (locale);
    xorg_confd_regex_destroy ();
    g_free (vconsole_keymap);
    g_free (vconsole_keymap_toggle);
    g_free (x11_layout);
    g_free (x11_model);
    g_free (x11_variant);
    g_free (x11_options);

    g_object_unref (locale_file);
    g_object_unref (keymaps_file);
    g_object_unref (x11_gentoo_file);
    g_object_unref (x11_systemd_file);
}
