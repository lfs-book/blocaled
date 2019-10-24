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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-protocol.h> /* for the error names */
#include <glib.h>
#include <gio/gio.h>

#include "localed.h"
#include "locale1-generated.h"
#include "main.h"
#include "polkitasync.h"
#include "shellparser.h"

#include "config.h"

#define SERVICE_NAME "localed"

static guint bus_id = 0;
static gboolean read_only = FALSE;

static BLocaledLocale1 *locale1 = NULL;

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
static GFile *x11_file = NULL;
G_LOCK_DEFINE_STATIC (xorg_conf);

/* keyboard model map file parser */

static GFile *kbd_model_map_file = NULL;

GRegex *kbd_model_map_line_comment_re = NULL;
GRegex *kbd_model_map_line_re = NULL;

struct kbd_model_map_entry {
    gchar *vconsole_keymap;
    gchar *x11_layout;
    gchar *x11_model;
    gchar *x11_variant;
    gchar *x11_options;
};

static void
kbd_model_map_regex_destroy ()
{
    if (kbd_model_map_line_comment_re != NULL) {
        g_regex_unref (kbd_model_map_line_comment_re);
        kbd_model_map_line_comment_re = NULL;
    }
    if (kbd_model_map_line_re != NULL) {
        g_regex_unref (kbd_model_map_line_re);
        kbd_model_map_line_re = NULL;
    }
}

static void
kbd_model_map_regex_init ()
{
    if (kbd_model_map_line_comment_re == NULL) {
        kbd_model_map_line_comment_re = g_regex_new ("^\\s*(?:#.*)?$", G_REGEX_ANCHORED, 0, NULL);
        g_assert (kbd_model_map_line_comment_re != NULL);
    }
    if (kbd_model_map_line_re == NULL) {
        kbd_model_map_line_re = g_regex_new ("^\\s*(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)", G_REGEX_ANCHORED, 0, NULL);
        g_assert (kbd_model_map_line_re != NULL);
    }
}

static gboolean
kbd_model_map_entry_matches_vconsole (const struct kbd_model_map_entry *entry,
                                      const gchar *vconsole_keymap)
{
    return !g_strcmp0 (vconsole_keymap, entry->vconsole_keymap);
}

static gboolean
matches_delimeted (const gchar *left,
                   const gchar *right,
                   const gchar *delimeter,
                   unsigned int *failure_score)
{
    gboolean ret = FALSE;
    gchar **leftv = NULL, **rightv = NULL;
    gchar **leftcur = NULL, **rightcur = NULL;

    if (left == NULL || left[0] == 0)
        leftv = g_new0 (gchar *, 1);
    else
        leftv = g_strsplit (left, delimeter, 0);

    if (right == NULL || right[0] == 0)
        rightv = g_new0 (gchar *, 1);
    else
        rightv = g_strsplit (right, delimeter, 0);

    if (failure_score != NULL)
        *failure_score = 0;

    for (leftcur = leftv; *leftcur != NULL; leftcur++) {
        gboolean found = FALSE;
        for (rightcur = rightv; *rightcur != NULL; rightcur++)
            if (!g_strcmp0 (*leftcur, *rightcur)) {
                found = TRUE;
                break;
            }
        if (found)
            ret = TRUE;
        else if (failure_score != NULL)
            (*failure_score)++;
    }

    for (rightcur = rightv; *rightcur != NULL; rightcur++) {
        gboolean found = FALSE;
        for (leftcur = leftv; *leftcur != NULL; leftcur++)
            if (!g_strcmp0 (*rightcur, *leftcur)) {
                found = TRUE;
                break;
            }
        if (found)
            ret = TRUE;
        else if (failure_score != NULL)
            (*failure_score)++;
    }

    g_strfreev (leftv);
    g_strfreev (rightv);
    return ret;
}

static gboolean
kbd_model_map_entry_matches_x11 (const struct kbd_model_map_entry *entry,
                                 const gchar *_x11_layout,
                                 const gchar *_x11_model,
                                 const gchar *_x11_variant,
                                 const gchar *_x11_options,
                                 unsigned int *failure_score)
{
    unsigned int x11_layout_failures;
    gboolean ret = FALSE;

    ret = matches_delimeted (_x11_layout, entry->x11_layout, ",", &x11_layout_failures);
    if (failure_score != NULL)
        *failure_score = 10000 * !ret +
                         100 * x11_layout_failures +
                         (g_strcmp0 (_x11_model, entry->x11_model) ? 1 : 0) +
                         10 * (g_strcmp0 (_x11_variant, entry->x11_variant) ? 1 : 0) +
                         !matches_delimeted (_x11_options, entry->x11_options, ",", NULL);
    return ret;
}

static void
kbd_model_map_entry_free (struct kbd_model_map_entry *entry)
{
    if (entry == NULL)
        return;

    g_free (entry->vconsole_keymap);
    g_free (entry->x11_layout);
    g_free (entry->x11_model);
    g_free (entry->x11_variant);
    g_free (entry->x11_options);

    g_free (entry);
}

static GList*
kbd_model_map_load (GError **error)
{
    GList *ret = NULL;
    gchar *filename = NULL, *filebuf = NULL, *line = NULL, *newline = NULL;
    struct kbd_model_map_entry *entry = NULL;

    filename = g_file_get_path (kbd_model_map_file);
    g_debug ("Parsing keyboard model map file file: '%s'", filename);

    if (!g_file_load_contents (kbd_model_map_file, NULL, &filebuf, NULL, NULL, error)) {
        g_prefix_error (error, "Unable to read '%s':", filename);
        goto out;
    }

    for (line = filebuf; *line != 0; line = newline + 1) {
        struct kbd_model_map_entry *entry = NULL;
        GMatchInfo *match_info = NULL;
        gboolean m = FALSE;

        if ((newline = strstr (line, "\n")) != NULL)
            *newline = 0;
        else
            newline = line + strlen (line) - 1;

        m = g_regex_match (kbd_model_map_line_comment_re, line, 0, &match_info);
        _g_match_info_clear (&match_info);
        if (m)
            continue;

        if (!g_regex_match (kbd_model_map_line_re, line,  0, &match_info)) {
            g_propagate_error (error,
                               g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                            "Failed to parse line '%s' in '%s'", line, filename));
            g_match_info_free (match_info);
            if (ret != NULL) {
                g_list_free_full (ret, (GDestroyNotify)kbd_model_map_entry_free);
                ret = NULL;
            }
            goto out;
        }
        entry = g_new0 (struct kbd_model_map_entry, 1);
        entry->vconsole_keymap = g_match_info_fetch (match_info, 1);
        entry->x11_layout = g_match_info_fetch (match_info, 2);
        entry->x11_model = g_match_info_fetch (match_info, 3);
        entry->x11_variant = g_match_info_fetch (match_info, 4);
        entry->x11_options = g_match_info_fetch (match_info, 5);

        // "-" in the map file stands for an empty string
        if (!g_strcmp0 (entry->x11_model, "-"))
            entry->x11_model[0] = 0;
        if (!g_strcmp0 (entry->x11_variant, "-"))
            entry->x11_variant[0] = 0;
        if (!g_strcmp0 (entry->x11_options, "-"))
            entry->x11_options[0] = 0;

        ret = g_list_prepend (ret, entry);
        _g_match_info_clear (&match_info);
    }
  out:
    if (ret != NULL)
        ret = g_list_reverse (ret);

    g_free (filename);
    g_free (filebuf);
    return ret;
}

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
        xorg_confd_line_comment_re = g_regex_new ("^\\s*#", G_REGEX_ANCHORED, 0, NULL);
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
    return entry;
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
    gchar *filebuf = NULL, *line = NULL, *newline = NULL;
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

    for (line = filebuf; *line != 0; line = newline + 1) {
        struct xorg_confd_line_entry *entry = NULL;
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        gboolean m = FALSE;

        if ((newline = strstr (line, "\n")) != NULL)
            *newline = 0;
        else
            newline = line + strlen (line) - 1;

        entry = xorg_confd_line_entry_new (line, NULL, XORG_CONFD_LINE_TYPE_UNKNOWN);

        if (g_regex_match (xorg_confd_line_comment_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as comment", line);
            entry->type = XORG_CONFD_LINE_TYPE_COMMENT;
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_section_input_class_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as InputClass section", line);
            if (in_section)
                goto no_match;
            in_section = TRUE;
            entry->type = XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS;
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_section_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as non-InputClass section", line);
            if (in_section)
                goto no_match;
            in_section = TRUE;
            entry->type = XORG_CONFD_LINE_TYPE_SECTION_OTHER;
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_end_section_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as end of section", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_END_SECTION;
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_match_is_keyboard_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as MatchIsKeyboard declaration", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_MATCH_IS_KEYBOARD;
            in_xkb_section = TRUE;
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_xkb_layout_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbLayout option", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_LAYOUT;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_xkb_model_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbModel option", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_MODEL;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_xkb_variant_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbVariant option", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_VARIANT;
            entry->value = g_match_info_fetch (match_info, 2);
        } else if (_g_match_info_clear (&match_info) && g_regex_match (xorg_confd_line_xkb_options_re, line, 0, &match_info)) {
            g_debug ("Parsed line '%s' as XkbOptions option", line);
            if (!in_section)
                goto no_match;
            entry->type = XORG_CONFD_LINE_TYPE_XKB_OPTIONS;
            entry->value = g_match_info_fetch (match_info, 2);
        }

        if (entry->type == XORG_CONFD_LINE_TYPE_UNKNOWN)
            g_debug ("Parsing line '%s' as unknown", line);

        _g_match_info_clear (&match_info);
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
        _g_match_info_clear (&match_info);
        goto parse_fail;
    }

    if (in_section) {
        /* Unterminated section */
        goto parse_fail;
    }

    parser->line_list = g_list_reverse (parser->line_list);

  out:
    g_free (filebuf);
    return parser;

  parse_fail:
    g_propagate_error (error,
                       g_error_new (G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                   "Unable to parse '%s'", parser->filename));
  fail:
    g_free (filebuf);
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
        g_debug ("Deleting entry '%s'", entry->string);
        GList *prev, *next;

        prev = line->prev;
        next = line->next;
        line->prev = NULL;
        line->next = NULL;
        g_list_free_full (line, (GDestroyNotify)xorg_confd_line_entry_free);
        if (prev != NULL)
            prev->next = next;
        if (next != NULL)
            next->prev = prev;
        return prev;
    }
    g_free (entry->value);
    entry->value = g_strdup (value);
    replacement = g_strdup_printf ("\\1\"%s\"", value);
    replaced = g_regex_replace (re, entry->string, -1, 0, replacement, 0, NULL);
    g_debug ("Setting entry '%s' to new value '%s' i.e. '%s'", entry->string, value, replaced);
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
    struct xorg_confd_line_entry *entry = NULL;
    gchar *string = NULL;

    if (parser == NULL)
        return;

    if (parser->section == NULL) {
        GList *section = NULL;

        entry = xorg_confd_line_entry_new ("Section \"InputClass\"", NULL, XORG_CONFD_LINE_TYPE_SECTION_INPUT_CLASS);
        section = g_list_prepend (section, entry);

        entry = xorg_confd_line_entry_new ("        Identifier \"keyboard-all\"", NULL, XORG_CONFD_LINE_TYPE_UNKNOWN);
        section = g_list_prepend (section, entry);

        entry = entry = xorg_confd_line_entry_new ("        MatchIsKeyboard \"on\"", NULL, XORG_CONFD_LINE_TYPE_MATCH_IS_KEYBOARD);
        section = g_list_prepend (section, entry);

        entry = entry = xorg_confd_line_entry_new ("EndSection", NULL, XORG_CONFD_LINE_TYPE_END_SECTION);
        section = g_list_prepend (section, entry);

        section = g_list_reverse (section);
        parser->section = section;
        parser->line_list = g_list_concat (parser->line_list, section);
    }

    for (curr = parser->section; curr != NULL; curr = curr->next) {
        entry = (struct xorg_confd_line_entry *) curr->data;

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
        string = g_strdup_printf ("        Option \"XkbLayout\" \"%s\"", layout);
        g_debug ("Inserting new entry: '%s'", string);
        entry = xorg_confd_line_entry_new (string, layout, XORG_CONFD_LINE_TYPE_XKB_LAYOUT);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!model_found && model != NULL && g_strcmp0 (model, "")) {
        string = g_strdup_printf ("        Option \"XkbModel\" \"%s\"", model);
        g_debug ("Inserting new entry: '%s'", string);
        entry = xorg_confd_line_entry_new (string, model, XORG_CONFD_LINE_TYPE_XKB_MODEL);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!variant_found && variant != NULL && g_strcmp0 (variant, "")) {
        string = g_strdup_printf ("        Option \"XkbVariant\" \"%s\"", variant);
        g_debug ("Inserting new entry: '%s'", string);
        entry = xorg_confd_line_entry_new (string, variant, XORG_CONFD_LINE_TYPE_XKB_VARIANT);
        parser->line_list = g_list_insert_before (parser->line_list, end, entry);
        g_free (string);
    }
    if (!options_found && options != NULL && g_strcmp0 (options, "")) {
        string = g_strdup_printf ("        Option \"XkbOptions\" \"%s\"", options);
        g_debug ("Inserting new entry: '%s'", string);
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
locale_name_is_valid (gchar *name)
{
    return g_regex_match_simple ("^[a-zA-Z0-9_.@-]*$", name, G_REGEX_MULTILINE, 0);
}

struct invoked_locale {
    GDBusMethodInvocation *invocation;
    gchar **locale; /* newly allocated */
};

static void
invoked_locale_free (struct invoked_locale *data)
{
    if (data == NULL)
        return;
    g_strfreev (data->locale);
    g_free (data);
}

static void
on_handle_set_locale_authorized_cb (GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
    GError *err = NULL;
    struct invoked_locale *data;
    gchar **loc, **var, **val, **locale_values = NULL;
    ShellParser *locale_file_parsed = NULL;
    gint status = 0;

    data = (struct invoked_locale *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (locale);
    locale_values = g_new0 (gchar *, g_strv_length (locale_variables) + 1);
    /* Don't allow unknown locale variables or invalid values */
    if (data->locale != NULL) {
        for (loc = data->locale; *loc != NULL; loc++) {
            gboolean found = FALSE;
            for (val = locale_values, var = locale_variables; *var != NULL; val++, var++) {
                size_t varlen;
                gchar *unquoted = NULL;

                varlen = strlen (*var);
                if (g_str_has_prefix (*loc, *var) && (*loc)[varlen] == '=' &&
                    (unquoted = g_shell_unquote (*loc + varlen + 1, NULL)) != NULL &&
                    locale_name_is_valid (unquoted)) {
                    found = TRUE;
                    if (*val != NULL)
                        g_free (*val);
                    *val = unquoted;
                } else
                    g_free (unquoted);
            }
            if (!found) {
                g_dbus_method_invocation_return_dbus_error (data->invocation, DBUS_ERROR_INVALID_ARGS,
                                                            "Invalid locale variable name or value");
                goto unlock;
            }
        }
    }

    if ((locale_file_parsed = shell_parser_new (locale_file, &err)) == NULL) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }

    if (shell_parser_is_empty (locale_file_parsed)) {
        /* Simply write the new env file */
        shell_parser_free (locale_file_parsed);
        if ((locale_file_parsed = shell_parser_new_from_string (locale_file, "# Configuration file for eselect\n# This file has been automatically generated\n", &err)) == NULL) {
            g_dbus_method_invocation_return_gerror (data->invocation, err);
            goto unlock;
        }
    }

    for (val = locale_values, var = locale_variables; *var != NULL; val++, var++) {
        if (*val == NULL)
            shell_parser_clear_variable (locale_file_parsed, *var);
        else
            shell_parser_set_variable (locale_file_parsed, *var, *val, TRUE);
    }

    if (!shell_parser_save (locale_file_parsed, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }

    g_strfreev (locale);
    locale = g_new0 (gchar *, g_strv_length (locale_variables) + 1);
    loc = locale;
    for (val = locale_values, var = locale_variables; *var != NULL; val++, var++) {
        if (*val != NULL) {
            *loc = g_strdup_printf ("%s=%s", *var, *val);
            loc++;
        }
    }

    blocaled_locale1_complete_set_locale (locale1, data->invocation);
    blocaled_locale1_set_locale (locale1, (const gchar * const *) locale);

  unlock:
    G_UNLOCK (locale);

  out:
    shell_parser_free (locale_file_parsed);
    /* g_strfreev (locale_values) will leak, since it stops at first NULL value */
    for (val = locale_values, var = locale_variables; *var != NULL; val++, var++)
        g_free (*val);
    g_free (locale_values);
    invoked_locale_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_locale (BLocaledLocale1 *locale1,
                      GDBusMethodInvocation *invocation,
                      const gchar * const *_locale,
                      const gboolean user_interaction,
                      gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_locale *data;
        data = g_new0 (struct invoked_locale, 1);
        data->invocation = invocation;
        data->locale = g_strdupv ((gchar**)_locale);
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.locale1.set-locale", user_interaction, on_handle_set_locale_authorized_cb, data);
    }

    return TRUE;
}

struct invoked_vconsole_keyboard {
    GDBusMethodInvocation *invocation;
    gchar *vconsole_keymap; /* newly allocated */
    gchar *vconsole_keymap_toggle; /* newly allocated */
    gboolean convert;
};

static void
invoked_vconsole_keyboard_free (struct invoked_vconsole_keyboard *data)
{
    if (data == NULL)
        return;
    g_free (data->vconsole_keymap);
    g_free (data->vconsole_keymap_toggle);
    g_free (data);
}

static void
on_handle_set_vconsole_keyboard_authorized_cb (GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data)
{
    GError *err = NULL;
    struct invoked_vconsole_keyboard *data;
    GList *kbd_model_map = NULL;
    struct kbd_model_map_entry *best_entry = NULL;
    struct xorg_confd_parser *x11_parser = NULL;

    data = (struct invoked_vconsole_keyboard *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (keymaps);
    if (data->convert) {
        GList *cur;

        G_LOCK (xorg_conf);
        kbd_model_map = kbd_model_map_load (&err);
        if (err != NULL) {
            g_dbus_method_invocation_return_gerror (data->invocation, err);
            goto unlock;
        }

        for (cur = kbd_model_map; cur->next != NULL; cur = cur->next) {
            struct kbd_model_map_entry *cur_entry = NULL;
            cur_entry = (struct kbd_model_map_entry *) cur->data;
            if (kbd_model_map_entry_matches_vconsole (cur_entry, data->vconsole_keymap)) {
                best_entry = cur_entry;
                break;
            }
        }
    }

    /* We do not set vconsole_keymap_toggle */
    if (!shell_parser_set_and_save (keymaps_file, &err, "keymap", NULL, data->vconsole_keymap, NULL)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }

    g_free (vconsole_keymap);
    vconsole_keymap = g_strdup (data->vconsole_keymap);
    blocaled_locale1_set_vconsole_keymap (locale1, vconsole_keymap);

    if (data->convert) {
        if (best_entry == NULL) {
            gchar *filename;
            filename = g_file_get_path (kbd_model_map_file);
            g_printerr ("Failed to find conversion entry for console keymap '%s' in '%s'\n", data->vconsole_keymap, filename);
            g_free (filename);
            G_UNLOCK (xorg_conf);
        } else {
            unsigned int failure_score = 0;

            kbd_model_map_entry_matches_x11 (best_entry, x11_layout, x11_model, x11_variant, x11_options, &failure_score);
            if (failure_score > 0) {
                /* The xkb data has changed, so we want to update it */
                x11_parser = xorg_confd_parser_new (x11_file, &err);

                if (x11_parser == NULL) {
                    g_dbus_method_invocation_return_gerror (data->invocation, err);
                    goto unlock;
                }
                xorg_confd_parser_set_xkb (x11_parser, best_entry->x11_layout, best_entry->x11_model, best_entry->x11_variant, best_entry->x11_options);
                if (!xorg_confd_parser_save (x11_parser, &err)) {
                    g_dbus_method_invocation_return_gerror (data->invocation, err);
                    goto unlock;
                }
                g_free (x11_layout);
                g_free (x11_model);
                g_free (x11_variant);
                g_free (x11_options);
                x11_layout = g_strdup (best_entry->x11_layout);
                x11_model = g_strdup (best_entry->x11_model);
                x11_variant = g_strdup (best_entry->x11_variant);
                x11_options = g_strdup (best_entry->x11_options);
                blocaled_locale1_set_x11_layout (locale1, x11_layout);
                blocaled_locale1_set_x11_model (locale1, x11_model);
                blocaled_locale1_set_x11_variant (locale1, x11_variant);
                blocaled_locale1_set_x11_options (locale1, x11_options);
            }
        }
    }
    /* We do not modify vconsole_keymap_toggle */
    blocaled_locale1_complete_set_vconsole_keyboard (locale1, data->invocation);

  unlock:
    if (data->convert)
        G_UNLOCK (xorg_conf);
    G_UNLOCK (keymaps);

  out:
    if (kbd_model_map != NULL)
        g_list_free_full (kbd_model_map, (GDestroyNotify)kbd_model_map_entry_free);
    xorg_confd_parser_free (x11_parser);
    invoked_vconsole_keyboard_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_vconsole_keyboard (BLocaledLocale1 *locale1,
                                 GDBusMethodInvocation *invocation,
                                 const gchar *keymap,
                                 const gchar *keymap_toggle,
                                 const gboolean convert,
                                 const gboolean user_interaction,
                                 gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_vconsole_keyboard *data;
        data = g_new0 (struct invoked_vconsole_keyboard, 1);
        data->invocation = invocation;
        data->vconsole_keymap = g_strdup (keymap);
        data->vconsole_keymap_toggle = g_strdup (keymap_toggle);
        data->convert = convert;
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.locale1.set-keyboard", user_interaction, on_handle_set_vconsole_keyboard_authorized_cb, data);
    }

    return TRUE;
}

struct invoked_x11_keyboard {
    GDBusMethodInvocation *invocation;
    gchar *x11_layout; /* newly allocated */
    gchar *x11_model; /* newly allocated */
    gchar *x11_variant; /* newly allocated */
    gchar *x11_options; /* newly allocated */
    gboolean convert;
};

static void
invoked_x11_keyboard_free (struct invoked_x11_keyboard *data)
{
    if (data == NULL)
        return;
    g_free (data->x11_layout);
    g_free (data->x11_model);
    g_free (data->x11_variant);
    g_free (data->x11_options);
    g_free (data);
}

static void
on_handle_set_x11_keyboard_authorized_cb (GObject *source_object,
                                          GAsyncResult *res,
                                          gpointer user_data)
{
    GError *err = NULL;
    struct invoked_x11_keyboard *data;
    GList *kbd_model_map = NULL;
    struct kbd_model_map_entry *best_entry = NULL;
    unsigned int best_failure_score = UINT_MAX;
    struct xorg_confd_parser *x11_parser = NULL;

    data = (struct invoked_x11_keyboard *) user_data;
    if (!check_polkit_finish (res, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto out;
    }

    G_LOCK (xorg_conf);
    if (data->convert) {
        GList *cur;

        G_LOCK (keymaps);
        kbd_model_map = kbd_model_map_load (&err);
        if (err != NULL) {
            g_dbus_method_invocation_return_gerror (data->invocation, err);
            goto unlock;
        }

        for (cur = kbd_model_map; cur->next != NULL; cur = cur->next) {
            struct kbd_model_map_entry *cur_entry = NULL;
            unsigned int cur_failure_score = 0;

            cur_entry = (struct kbd_model_map_entry *) cur->data;
            if (kbd_model_map_entry_matches_x11 (cur_entry, data->x11_layout, data->x11_model, data->x11_variant, data->x11_options, &cur_failure_score))
                if (cur_failure_score < best_failure_score) {
                    best_entry = cur_entry;
                    best_failure_score = cur_failure_score;
                }
        }
    }

    x11_parser = xorg_confd_parser_new (x11_file, &err);

    if (x11_parser == NULL) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }
    xorg_confd_parser_set_xkb (x11_parser, data->x11_layout, data->x11_model, data->x11_variant, data->x11_options);
    if (!xorg_confd_parser_save (x11_parser, &err)) {
        g_dbus_method_invocation_return_gerror (data->invocation, err);
        goto unlock;
    }
    g_free (x11_layout);
    g_free (x11_model);
    g_free (x11_variant);
    g_free (x11_options);
    x11_layout = g_strdup (data->x11_layout);
    x11_model = g_strdup (data->x11_model);
    x11_variant = g_strdup (data->x11_variant);
    x11_options = g_strdup (data->x11_options);
    blocaled_locale1_set_x11_layout (locale1, x11_layout);
    blocaled_locale1_set_x11_model (locale1, x11_model);
    blocaled_locale1_set_x11_variant (locale1, x11_variant);
    blocaled_locale1_set_x11_options (locale1, x11_options);

    if (data->convert) {
        if (best_entry == NULL) {
            gchar *filename;
            filename = g_file_get_path (kbd_model_map_file);
            g_printerr ("Failed to find conversion entry for x11 layout '%s' in '%s'\n", data->x11_layout, filename);
            g_free (filename);
        } else {
            if (!shell_parser_set_and_save (keymaps_file, &err, "keymap", NULL, best_entry->vconsole_keymap, NULL)) {
                g_dbus_method_invocation_return_gerror (data->invocation, err);
                goto unlock;
            }
            g_free (vconsole_keymap);
            vconsole_keymap = g_strdup (best_entry->vconsole_keymap);
            blocaled_locale1_set_vconsole_keymap (locale1, vconsole_keymap);
        }
    }

    blocaled_locale1_complete_set_x11_keyboard (locale1, data->invocation);

  unlock:
    if (data->convert)
        G_UNLOCK (keymaps);
    G_UNLOCK (xorg_conf);

  out:
    if (kbd_model_map != NULL)
        g_list_free_full (kbd_model_map, (GDestroyNotify)kbd_model_map_entry_free);
    xorg_confd_parser_free (x11_parser);
    invoked_x11_keyboard_free (data);
    if (err != NULL)
        g_error_free (err);
}

static gboolean
on_handle_set_x11_keyboard (BLocaledLocale1 *locale1,
                            GDBusMethodInvocation *invocation,
                            const gchar *layout,
                            const gchar *model,
                            const gchar *variant,
                            const gchar *options,
                            const gboolean convert,
                            const gboolean user_interaction,
                            gpointer user_data)
{
    if (read_only)
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    DBUS_ERROR_NOT_SUPPORTED,
                                                    SERVICE_NAME " is in read-only mode");
    else {
        struct invoked_x11_keyboard *data;
        data = g_new0 (struct invoked_x11_keyboard, 1);
        data->invocation = invocation;
        data->x11_layout = g_strdup (layout);
        data->x11_model = g_strdup (model);
        data->x11_variant = g_strdup (variant);
        data->x11_options = g_strdup (options);
        data->convert = convert;
        check_polkit_async (g_dbus_method_invocation_get_sender (invocation), "org.freedesktop.locale1.set-keyboard", user_interaction, on_handle_set_x11_keyboard_authorized_cb, data);
    }

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

    locale1 = blocaled_locale1_skeleton_new ();

    blocaled_locale1_set_locale (locale1, (const gchar * const *) locale);
    blocaled_locale1_set_vconsole_keymap (locale1, vconsole_keymap);
    blocaled_locale1_set_vconsole_keymap_toggle (locale1, vconsole_keymap_toggle);
    blocaled_locale1_set_x11_layout (locale1, x11_layout);
    blocaled_locale1_set_x11_model (locale1, x11_model);
    blocaled_locale1_set_x11_variant (locale1, x11_variant);
    blocaled_locale1_set_x11_options (locale1, x11_options);

    g_signal_connect (locale1, "handle-set-locale", G_CALLBACK (on_handle_set_locale), NULL);
    g_signal_connect (locale1, "handle-set-vconsole-keyboard", G_CALLBACK (on_handle_set_vconsole_keyboard), NULL);
    g_signal_connect (locale1, "handle-set-x11-keyboard", G_CALLBACK (on_handle_set_x11_keyboard), NULL);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (locale1),
                                           connection,
                                           "/org/freedesktop/locale1",
                                           &err)) {
        if (err != NULL) {
            g_critical ("Failed to export interface on /org/freedesktop/locale1: %s", err->message);
            localed_exit (1);
        }
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *bus_name,
                  gpointer         user_data)
{
    g_debug ("Acquired the name %s", bus_name);
    localed_started ();
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *bus_name,
              gpointer         user_data)
{
    if (connection == NULL)
        g_critical ("Failed to acquire a dbus connection");
    else
        g_critical ("Failed to acquire dbus name %s", bus_name);
    localed_exit (1);
}

/**
 * localed_init:
 * @_read_only: if set, settings file cannot be written
 *
 * Reads settings from config files (LOCALECONFIG, KEYBOARDCONFIG, and
 * XKBDCONFIG should be set when compiling), connects to the message bus
 * and initialize properties
 */
void
localed_init (gboolean _read_only)
{
    GError *err = NULL;
    gchar **locale_values = NULL;
    struct xorg_confd_parser *x11_parser = NULL;

    read_only = _read_only;
    kbd_model_map_file = g_file_new_for_path (PKGDATADIR "/kbd-model-map");
    locale_file = g_file_new_for_path (LOCALECONFIG);
    keymaps_file = g_file_new_for_path (KEYBOARDCONFIG);

    x11_file = g_file_new_for_path (XKBDCONFIG);

    locale = g_new0 (gchar *, g_strv_length (locale_variables) + 1);
    locale_values = shell_parser_source_var_list (locale_file, (const gchar * const *)locale_variables, &err);
    if (locale_values != NULL) {
        gchar **variable, **value, **loc;
        loc = locale;
        for (variable = locale_variables, value = locale_values; *variable != NULL; variable++, value++) {
            if (*value != NULL) {
                *loc = g_strdup_printf ("%s=%s", *variable, *value);
                g_free (*value);
                loc++;
            }
        }
            
        g_free (locale_values);
    }
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

/* Others may use KEYMAP: TODO */
    vconsole_keymap = shell_source_var (keymaps_file, "${keymap}", &err);
    if (vconsole_keymap == NULL)
        vconsole_keymap = g_strdup ("");
    if (err != NULL) {
        g_debug ("%s", err->message);
        g_clear_error (&err);
    }

    /* We don't have a good equivalent for this TODO: we should! */
    vconsole_keymap_toggle = g_strdup ("");

    kbd_model_map_regex_init ();
    xorg_confd_regex_init ();

    x11_parser = xorg_confd_parser_new (x11_file, &err);

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

/**
 * localed_destroy:
 *
 * Garbage collection: unowns the name on the bus, and free all allocated
 * storages at init
 */

void
localed_destroy (void)
{
    g_bus_unown_name (bus_id);
    bus_id = 0;
    read_only = FALSE;
    g_strfreev (locale);
    kbd_model_map_regex_destroy ();
    xorg_confd_regex_destroy ();
    g_free (vconsole_keymap);
    g_free (vconsole_keymap_toggle);
    g_free (x11_layout);
    g_free (x11_model);
    g_free (x11_variant);
    g_free (x11_options);

    g_object_unref (locale_file);
    g_object_unref (keymaps_file);
    g_object_unref (x11_file);
    g_object_unref (kbd_model_map_file);
}
