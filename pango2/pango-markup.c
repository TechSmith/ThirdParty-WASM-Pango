/* Pango2
 * pango-markup.c: Parse markup into attributed text
 *
 * Copyright (C) 2000 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "pango-markup.h"

#include "pango-attributes.h"
#include "pango-attr-private.h"
#include "pango-font.h"
#include "pango-enum-types.h"
#include "pango-impl-utils.h"
#include "pango-font-description-private.h"

/* CSS size levels */
typedef enum
{
  XXSmall = -3,
  XSmall = -2,
  Small = -1,
  Medium = 0,
  Large = 1,
  XLarge = 2,
  XXLarge = 3
} SizeLevel;

typedef struct _MarkupData MarkupData;

struct _MarkupData
{
  Pango2AttrList *attr_list;
  GString *text;
  GSList *tag_stack;
  gsize index;
  GSList *to_apply;
  gunichar accel_marker;
  gunichar accel_char;
};

typedef struct _OpenTag OpenTag;

struct _OpenTag
{
  GSList *attrs;
  gsize start_index;
  /* Current total scale level; reset whenever
   * an absolute size is set.
   * Each "larger" ups it 1, each "smaller" decrements it 1
   */
  int scale_level;
  /* Our impact on scale_level, so we know whether we
   * need to create an attribute ourselves on close
   */
  int scale_level_delta;
  /* Base scale factor currently in effect
   * or size that this tag
   * forces, or parent's scale factor or size.
   */
  double base_scale_factor;
  int base_font_size;
  guint has_base_font_size : 1;
};

typedef gboolean (*TagParseFunc) (MarkupData            *md,
                                  OpenTag               *tag,
                                  const char           **names,
                                  const char           **values,
                                  GMarkupParseContext   *context,
                                  GError               **error);

static gboolean b_parse_func        (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean big_parse_func      (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean span_parse_func     (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean i_parse_func        (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean markup_parse_func   (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean s_parse_func        (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean sub_parse_func      (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean sup_parse_func      (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean small_parse_func    (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean tt_parse_func       (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);
static gboolean u_parse_func        (MarkupData           *md,
                                     OpenTag              *tag,
                                     const char          **names,
                                     const char          **values,
                                     GMarkupParseContext  *context,
                                     GError              **error);

static gboolean
_pango2_scan_int (const char **pos, int *out)
{
  char *end;
  long temp;

  errno = 0;
  temp = strtol (*pos, &end, 10);
  if (errno == ERANGE)
    {
      errno = 0;
      return FALSE;
    }

  *out = (int)temp;
  if ((long)(*out) != temp)
    {
      return FALSE;
    }

  *pos = end;

  return TRUE;
}

static gboolean
parse_int (const char *word,
           int        *out)
{
  char *end;
  long val;
  int i;

  if (word == NULL)
    return FALSE;

  val = strtol (word, &end, 10);
  i = val;

  if (end != word && *end == '\0' && val >= 0 && val == i)
    {
      if (out)
        *out = i;

      return TRUE;
    }

  return FALSE;
}

static gboolean
_pango2_parse_enum (GType       type,
                   const char *str,
                   int        *value,
                   gboolean    warn,
                   char      **possible_values)
{
  GEnumClass *class = NULL;
  gboolean ret = TRUE;
  GEnumValue *v = NULL;

  class = g_type_class_ref (type);

  if (G_LIKELY (str))
    v = g_enum_get_value_by_nick (class, str);

  if (v)
    {
      if (G_LIKELY (value))
        *value = v->value;
    }
  else if (!parse_int (str, value))
    {
      ret = FALSE;
      if (G_LIKELY (warn || possible_values))
        {
          int i;
          GString *s = g_string_new (NULL);

          for (i = 0, v = g_enum_get_value (class, i); v;
               i++  , v = g_enum_get_value (class, i))
            {
              if (i)
                g_string_append_c (s, '/');
              g_string_append (s, v->value_nick);
            }

          if (warn)
            g_warning ("%s must be one of %s",
                       G_ENUM_CLASS_TYPE_NAME(class),
                       s->str);

          if (possible_values)
            *possible_values = s->str;

          g_string_free (s, possible_values ? FALSE : TRUE);
        }
    }

  g_type_class_unref (class);

  return ret;
}

static gboolean
pango2_parse_flags (GType        type,
                    const char  *str,
                    int         *value,
                    char       **possible_values)
{
  GFlagsClass *class = NULL;
  gboolean ret = TRUE;
  GFlagsValue *v = NULL;

  class = g_type_class_ref (type);

  v = g_flags_get_value_by_nick (class, str);

  if (v)
    {
      *value = v->value;
    }
  else if (!parse_int (str, value))
    {
      char **strv = g_strsplit (str, "|", 0);
      int i;

      *value = 0;

      for (i = 0; strv[i]; i++)
        {
          strv[i] = g_strstrip (strv[i]);
          v = g_flags_get_value_by_nick (class, strv[i]);
          if (!v)
            {
              ret = FALSE;
              break;
            }
          *value |= v->value;
        }
      g_strfreev (strv);

      if (!ret && possible_values)
        {
          int i;
          GString *s = g_string_new (NULL);

          for (i = 0; i < class->n_values; i++)
            {
              v = &class->values[i];
              if (i)
                g_string_append_c (s, '/');
              g_string_append (s, v->value_nick);
            }

          *possible_values = s->str;

          g_string_free (s, FALSE);
        }
    }

  g_type_class_unref (class);

  return ret;
}

static double
scale_factor (int scale_level, double base)
{
  double factor = base;
  int i;

  /* 1.2 is the CSS scale factor between sizes */

  if (scale_level > 0)
    {
      i = 0;
      while (i < scale_level)
        {
          factor *= 1.2;

          ++i;
        }
    }
  else if (scale_level < 0)
    {
      i = scale_level;
      while (i < 0)
        {
          factor /= 1.2;

          ++i;
        }
    }

  return factor;
}

static void
open_tag_free (OpenTag *ot)
{
  g_slist_foreach (ot->attrs, (GFunc) pango2_attribute_destroy, NULL);
  g_slist_free (ot->attrs);
  g_slice_free (OpenTag, ot);
}

static void
open_tag_set_absolute_font_size (OpenTag *ot,
                                 int      font_size)
{
  ot->base_font_size = font_size;
  ot->has_base_font_size = TRUE;
  ot->scale_level = 0;
  ot->scale_level_delta = 0;
}

static void
open_tag_set_absolute_font_scale (OpenTag *ot,
                                  double   scale)
{
  ot->base_scale_factor = scale;
  ot->has_base_font_size = FALSE;
  ot->scale_level = 0;
  ot->scale_level_delta = 0;
}

static OpenTag*
markup_data_open_tag (MarkupData   *md)
{
  OpenTag *ot;
  OpenTag *parent = NULL;

  if (md->attr_list == NULL)
    return NULL;

  if (md->tag_stack)
    parent = md->tag_stack->data;

  ot = g_slice_new (OpenTag);
  ot->attrs = NULL;
  ot->start_index = md->index;
  ot->scale_level_delta = 0;

  if (parent == NULL)
    {
      ot->base_scale_factor = 1.0;
      ot->base_font_size = 0;
      ot->has_base_font_size = FALSE;
      ot->scale_level = 0;
    }
  else
    {
      ot->base_scale_factor = parent->base_scale_factor;
      ot->base_font_size = parent->base_font_size;
      ot->has_base_font_size = parent->has_base_font_size;
      ot->scale_level = parent->scale_level;
    }

  md->tag_stack = g_slist_prepend (md->tag_stack, ot);

  return ot;
}

static void
markup_data_close_tag (MarkupData *md)
{
  OpenTag *ot;
  GSList *tmp_list;

  if (md->attr_list == NULL)
    return;

  /* pop the stack */
  ot = md->tag_stack->data;
  md->tag_stack = g_slist_delete_link (md->tag_stack,
                                       md->tag_stack);

  /* Adjust end indexes, and push each attr onto the front of the
   * to_apply list. This means that outermost tags are on the front of
   * that list; if we apply the list in order, then the innermost
   * tags will "win" which is correct.
   */
  tmp_list = ot->attrs;
  while (tmp_list != NULL)
    {
      Pango2Attribute *a = tmp_list->data;

      a->start_index = ot->start_index;
      a->end_index = md->index;

      md->to_apply = g_slist_prepend (md->to_apply, a);

      tmp_list = g_slist_next (tmp_list);
    }

  if (ot->scale_level_delta != 0)
    {
      /* We affected relative font size; create an appropriate
       * attribute and reverse our effects on the current level
       */
      Pango2Attribute *a;

      if (ot->has_base_font_size)
        {
          /* Create a font using the absolute point size as the base size
           * to be scaled from.
           * We need to use a local variable to ensure that the compiler won't
           * implicitly cast it to integer while the result is kept in registers,
           * leading to a wrong approximation in i386 (with 387 FPU)
           */
          volatile double size;

          size = scale_factor (ot->scale_level, 1.0) * ot->base_font_size;
          a = pango2_attr_size_new (size);
        }
      else
        {
          /* Create a font using the current scale factor
           * as the base size to be scaled from
           */
          a = pango2_attr_scale_new (scale_factor (ot->scale_level,
                                                  ot->base_scale_factor));
        }

      a->start_index = ot->start_index;
      a->end_index = md->index;

      md->to_apply = g_slist_prepend (md->to_apply, a);
    }

  g_slist_free (ot->attrs);
  g_slice_free (OpenTag, ot);
}

static void
start_element_handler  (GMarkupParseContext *context,
                        const char          *element_name,
                        const char         **attribute_names,
                        const char         **attribute_values,
                        gpointer             user_data,
                        GError             **error)
{
  TagParseFunc parse_func = NULL;
  OpenTag *ot;

  switch (*element_name)
    {
    case 'b':
      if (strcmp ("b", element_name) == 0)
        parse_func = b_parse_func;
      else if (strcmp ("big", element_name) == 0)
        parse_func = big_parse_func;
      break;

    case 'i':
      if (strcmp ("i", element_name) == 0)
        parse_func = i_parse_func;
      break;

    case 'm':
      if (strcmp ("markup", element_name) == 0)
        parse_func = markup_parse_func;
      break;

    case 's':
      if (strcmp ("span", element_name) == 0)
        parse_func = span_parse_func;
      else if (strcmp ("s", element_name) == 0)
        parse_func = s_parse_func;
      else if (strcmp ("sub", element_name) == 0)
        parse_func = sub_parse_func;
      else if (strcmp ("sup", element_name) == 0)
        parse_func = sup_parse_func;
      else if (strcmp ("small", element_name) == 0)
        parse_func = small_parse_func;
      break;

    case 't':
      if (strcmp ("tt", element_name) == 0)
        parse_func = tt_parse_func;
      break;

    case 'u':
      if (strcmp ("u", element_name) == 0)
        parse_func = u_parse_func;
      break;

    default:
      break;
    }

  if (parse_func == NULL)
    {
      int line_number, char_number;

      g_markup_parse_context_get_position (context,
                                           &line_number, &char_number);

      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                   "Unknown tag '%s' on line %d char %d",
                   element_name,
                   line_number, char_number);

      return;
    }

  ot = markup_data_open_tag (user_data);

  /* note ot may be NULL if the user didn't want the attribute list */

  if (!(*parse_func) (user_data, ot,
                      attribute_names, attribute_values,
                      context, error))
    {
      /* there's nothing to do; we return an error, and end up
       * freeing ot off the tag stack later.
       */
    }
}

static void
end_element_handler    (GMarkupParseContext *context G_GNUC_UNUSED,
                        const char          *element_name G_GNUC_UNUSED,
                        gpointer             user_data,
                        GError             **error G_GNUC_UNUSED)
{
  markup_data_close_tag (user_data);
}

static void
text_handler           (GMarkupParseContext *context G_GNUC_UNUSED,
                        const char          *text,
                        gsize                text_len,
                        gpointer             user_data,
                        GError             **error G_GNUC_UNUSED)
{
  MarkupData *md = user_data;

  if (md->accel_marker == 0)
    {
      /* Just append all the text */

      md->index += text_len;

      g_string_append_len (md->text, text, text_len);
    }
  else
    {
      /* Parse the accelerator */
      const char *p;
      const char *end;
      const char *range_start;
      const char *range_end;
      gssize uline_index = -1;
      gsize uline_len = 0;      /* Quiet GCC */

      range_end = NULL;
      range_start = text;
      p = text;
      end = text + text_len;

      while (p != end)
        {
          gunichar c;

          c = g_utf8_get_char (p);

          if (range_end)
            {
              if (c == md->accel_marker)
                {
                  /* escaped accel marker; move range_end
                   * past the accel marker that came before,
                   * append the whole thing
                   */
                  range_end = g_utf8_next_char (range_end);
                  g_string_append_len (md->text,
                                       range_start,
                                       range_end - range_start);
                  md->index += range_end - range_start;

                  /* set next range_start, skipping accel marker */
                  range_start = g_utf8_next_char (p);
                }
              else
                {
                  /* Don't append the accel marker (leave range_end
                   * alone); set the accel char to c; record location for
                   * underline attribute
                   */
                  if (md->accel_char == 0)
                    md->accel_char = c;

                  g_string_append_len (md->text,
                                       range_start,
                                       range_end - range_start);
                  md->index += range_end - range_start;

                  /* The underline should go underneath the char
                   * we're setting as the next range_start
                   */
                  if (md->attr_list != NULL)
                    {
                      /* Add the underline indicating the accelerator */
                      Pango2Attribute *attr;

                      attr = pango2_attr_underline_new (PANGO2_LINE_STYLE_SOLID);

                      uline_index = md->index;
                      uline_len = g_utf8_next_char (p) - p;

                      attr->start_index = uline_index;
                      attr->end_index = uline_index + uline_len;

                      pango2_attr_list_change (md->attr_list, attr);

                      attr = pango2_attr_underline_position_new (PANGO2_UNDERLINE_POSITION_UNDER);

                      attr->start_index = uline_index;
                      attr->end_index = uline_index + uline_len;

                      pango2_attr_list_change (md->attr_list, attr);
                    }

                  /* set next range_start to include this char */
                  range_start = p;
                }

              /* reset range_end */
              range_end = NULL;
            }
          else if (c == md->accel_marker)
            {
              range_end = p;
            }

          p = g_utf8_next_char (p);
        }

      g_string_append_len (md->text,
                           range_start,
                           end - range_start);
                           md->index += end - range_start;
    }
}

static gboolean
xml_isspace (char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const GMarkupParser pango2_markup_parser = {
  start_element_handler,
  end_element_handler,
  text_handler,
  NULL,
  NULL
};

static void
destroy_markup_data (MarkupData *md)
{
  g_slist_free_full (md->tag_stack, (GDestroyNotify) open_tag_free);
  g_slist_free_full (md->to_apply, (GDestroyNotify) pango2_attribute_destroy);
  if (md->text)
      g_string_free (md->text, TRUE);

  if (md->attr_list)
    pango2_attr_list_unref (md->attr_list);

  g_slice_free (MarkupData, md);
}

static GMarkupParseContext *
pango2_markup_parser_new_internal (char       accel_marker,
                                   GError   **error,
                                   gboolean   want_attr_list)
{
  MarkupData *md;
  GMarkupParseContext *context;

  md = g_slice_new (MarkupData);

  /* Don't bother creating these if they weren't requested;
   * might be useful e.g. if you just want to validate
   * some markup.
   */
  if (want_attr_list)
    md->attr_list = pango2_attr_list_new ();
  else
    md->attr_list = NULL;

  md->text = g_string_new (NULL);

  md->accel_marker = accel_marker;
  md->accel_char = 0;

  md->index = 0;
  md->tag_stack = NULL;
  md->to_apply = NULL;

  context = g_markup_parse_context_new (&pango2_markup_parser,
                                        0, md,
                                        (GDestroyNotify)destroy_markup_data);

  if (!g_markup_parse_context_parse (context, "<markup>", -1, error))
    g_clear_pointer (&context, g_markup_parse_context_free);

  return context;
}

/**
 * pango2_parse_markup:
 * @markup_text: markup to parse (see the [Pango2 Markup](pango2_markup.html) docs)
 * @length: length of @markup_text, or -1 if nul-terminated
 * @accel_marker: character that precedes an accelerator, or 0 for none
 * @attr_list: (out) (optional): address of return location for a `Pango2AttrList`
 * @text: (out) (optional): address of return location for text with tags stripped
 * @accel_char: (out) (optional): address of return location for accelerator char
 * @error: address of return location for errors
 *
 * Parses marked-up text to create a plain-text string and an attribute list.
 *
 * See the [Pango2 Markup](pango2_markup.html) docs for details about the
 * supported markup.
 *
 * If @accel_marker is nonzero, the given character will mark the
 * character following it as an accelerator. For example, @accel_marker
 * might be an ampersand or underscore. All characters marked
 * as an accelerator will receive a %PANGO2_UNDERLINE_LOW attribute,
 * and the first character so marked will be returned in @accel_char.
 * Two @accel_marker characters following each other produce a single
 * literal @accel_marker character.
 *
 * To parse a stream of pango markup incrementally, use [func@markup_parser_new].
 *
 * If any error happens, none of the output arguments are touched except
 * for @error.
 *
 * Return value: %FALSE if @error is set, otherwise %TRUE
 **/
gboolean
pango2_parse_markup (const char      *markup_text,
                     int              length,
                     gunichar         accel_marker,
                     Pango2AttrList **attr_list,
                     char           **text,
                     gunichar        *accel_char,
                     GError         **error)
{
  GMarkupParseContext *context = NULL;
  gboolean ret = FALSE;
  const char *p;
  const char *end;

  g_return_val_if_fail (markup_text != NULL, FALSE);

  if (length < 0)
    length = strlen (markup_text);

  p = markup_text;
  end = markup_text + length;
  while (p != end && xml_isspace (*p))
    ++p;

  context = pango2_markup_parser_new_internal (accel_marker,
                                               error,
                                               (attr_list != NULL));

  if (!g_markup_parse_context_parse (context,
                                     markup_text,
                                     length,
                                     error))
    goto out;

  if (!pango2_markup_parser_finish (context,
                                    attr_list,
                                    text,
                                    accel_char,
                                    error))
    goto out;

  ret = TRUE;

 out:
  if (context != NULL)
    g_markup_parse_context_free (context);
  return ret;
}

/**
 * pango2_markup_parser_new:
 * @accel_marker: character that precedes an accelerator, or 0 for none
 *
 * Incrementally parses marked-up text to create a plain-text string
 * and an attribute list.
 *
 * See the [Pango2 Markup](pango2_markup.html) docs for details about the
 * supported markup.
 *
 * If @accel_marker is nonzero, the given character will mark the
 * character following it as an accelerator. For example, @accel_marker
 * might be an ampersand or underscore. All characters marked
 * as an accelerator will receive a %PANGO2_UNDERLINE_LOW attribute,
 * and the first character so marked will be returned in @accel_char,
 * when calling [func@markup_parser_finish]. Two @accel_marker characters
 * following each other produce a single literal @accel_marker character.
 *
 * To feed markup to the parser, use [method@GLib.MarkupParseContext.parse]
 * on the returned [struct@GLib.MarkupParseContext]. When done with feeding markup
 * to the parser, use [func@markup_parser_finish] to get the data out
 * of it, and then use [method@GLib.MarkupParseContext.free] to free it.
 *
 * This function is designed for applications that read Pango2 markup
 * from streams. To simply parse a string containing Pango2 markup,
 * the [func@Pango2.parse_markup] API is recommended instead.
 *
 * Return value: (transfer none): a `GMarkupParseContext` that should be
 * destroyed with [method@GLib.MarkupParseContext.free].
 **/
GMarkupParseContext *
pango2_markup_parser_new (gunichar accel_marker)
{
  return pango2_markup_parser_new_internal (accel_marker, NULL, TRUE);
}

/**
 * pango2_markup_parser_finish:
 * @context: A valid parse context that was returned from [func@markup_parser_new]
 * @attr_list: (out) (optional): address of return location for a `Pango2AttrList`
 * @text: (out) (optional): address of return location for text with tags stripped
 * @accel_char: (out) (optional): address of return location for accelerator char
 * @error: address of return location for errors
 *
 * Finishes parsing markup.
 *
 * After feeding a Pango2 markup parser some data with [method@GLib.MarkupParseContext.parse],
 * use this function to get the list of attributes and text out of the
 * markup. This function will not free @context, use [method@GLib.MarkupParseContext.free]
 * to do so.
 *
 * Return value: %FALSE if @error is set, otherwise %TRUE
 */
gboolean
pango2_markup_parser_finish (GMarkupParseContext  *context,
                             Pango2AttrList      **attr_list,
                             char                **text,
                             gunichar             *accel_char,
                             GError              **error)
{
  gboolean ret = FALSE;
  MarkupData *md = g_markup_parse_context_get_user_data (context);
  GSList *tmp_list;

  if (!g_markup_parse_context_parse (context,
                                     "</markup>",
                                     -1,
                                     error))
    goto out;

  if (!g_markup_parse_context_end_parse (context, error))
    goto out;

  if (md->attr_list)
    {
      /* The apply list has the most-recently-closed tags first;
       * we want to apply the least-recently-closed tag last.
       */
      tmp_list = md->to_apply;
      while (tmp_list != NULL)
        {
          Pango2Attribute *attr = tmp_list->data;

          /* Innermost tags before outermost */
          pango2_attr_list_insert (md->attr_list, attr);

          tmp_list = g_slist_next (tmp_list);
        }
      g_slist_free (md->to_apply);
      md->to_apply = NULL;
    }

  if (attr_list)
    {
      *attr_list = md->attr_list;
      md->attr_list = NULL;
    }

  if (text)
    {
      *text = g_string_free (md->text, FALSE);
      md->text = NULL;
    }

  if (accel_char)
    *accel_char = md->accel_char;

  g_assert (md->tag_stack == NULL);
  ret = TRUE;

 out:
  return ret;
}

static void
set_bad_attribute (GError              **error,
                   GMarkupParseContext  *context,
                   const char           *element_name,
                   const char           *attribute_name)
{
  int line_number, char_number;

  g_markup_parse_context_get_position (context,
                                       &line_number, &char_number);

  g_set_error (error,
               G_MARKUP_ERROR,
               G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
               "Tag '%s' does not support attribute '%s' on line %d char %d",
               element_name,
               attribute_name,
               line_number, char_number);
}

static void
add_attribute (OpenTag         *ot,
               Pango2Attribute *attr)
{
  if (ot == NULL)
    pango2_attribute_destroy (attr);
  else
    ot->attrs = g_slist_prepend (ot->attrs, attr);
}

#define CHECK_NO_ATTRS(elem) G_STMT_START {                    \
         if (*names != NULL) {                                 \
           set_bad_attribute (error, context, (elem), *names); \
           return FALSE;                                       \
         } }G_STMT_END

static gboolean
b_parse_func (MarkupData            *md G_GNUC_UNUSED,
              OpenTag               *tag,
              const char           **names,
              const char           **values G_GNUC_UNUSED,
              GMarkupParseContext   *context,
              GError               **error)
{
  CHECK_NO_ATTRS("b");
  add_attribute (tag, pango2_attr_weight_new (PANGO2_WEIGHT_BOLD));
  return TRUE;
}

static gboolean
big_parse_func (MarkupData            *md G_GNUC_UNUSED,
                OpenTag               *tag,
                const char           **names,
                const char           **values G_GNUC_UNUSED,
                GMarkupParseContext   *context,
                GError               **error)
{
  CHECK_NO_ATTRS("big");

  /* Grow text one level */
  if (tag)
    {
      tag->scale_level_delta += 1;
      tag->scale_level += 1;
    }

  return TRUE;
}

static gboolean
parse_percentage (const char *input,
                  double     *val)
{
  double v;
  char *end;

  v = g_ascii_strtod (input, &end);
  if (errno == 0 && strcmp (end, "%") == 0 && v > 0)
    {
      *val = v;
      return TRUE;
    }

  return FALSE;
}

static gboolean
parse_absolute_size (OpenTag    *tag,
                     const char *size)
{
  SizeLevel level = Medium;
  double val;
  double factor;

  if (strcmp (size, "xx-small") == 0)
    level = XXSmall;
  else if (strcmp (size, "x-small") == 0)
    level = XSmall;
  else if (strcmp (size, "small") == 0)
    level = Small;
  else if (strcmp (size, "medium") == 0)
    level = Medium;
  else if (strcmp (size, "large") == 0)
    level = Large;
  else if (strcmp (size, "x-large") == 0)
    level = XLarge;
  else if (strcmp (size, "xx-large") == 0)
    level = XXLarge;
  else if (parse_percentage (size, &val))
    {
      factor = val / 100.0;
      goto done;
    }
  else
    return FALSE;

  /* This is "absolute" in that it's relative to the base font,
   * but not to sizes created by any other tags
   */
  factor = scale_factor (level, 1.0);

done:
  add_attribute (tag, pango2_attr_scale_new (factor));
  if (tag)
    open_tag_set_absolute_font_scale (tag, factor);

  return TRUE;
}

/* a string compare func that ignores '-' vs '_' differences */
static int
attr_strcmp (gconstpointer pa,
             gconstpointer pb)
{
  const char *a = pa;
  const char *b = pb;

  int ca;
  int cb;

  while (*a && *b)
    {
      ca = *a++;
      cb = *b++;

      if (ca == cb)
        continue;

      ca = ca == '_' ? '-' : ca;
      cb = cb == '_' ? '-' : cb;

      if (ca != cb)
        return cb - ca;
    }

  ca = *a;
  cb = *b;

  return cb - ca;
}

static gboolean
span_parse_int (const char  *attr_name,
                const char  *attr_val,
                int         *val,
                int          line_number,
                GError     **error)
{
  const char *end = attr_val;

  if (!_pango2_scan_int (&end, val) || *end != '\0')
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "Value of '%s' attribute on <span> tag "
                   "on line %d could not be parsed; "
                   "should be an integer, not '%s'",
                   attr_name, line_number, attr_val);
      return FALSE;
    }

  return TRUE;
}

static gboolean
span_parse_float (const char  *attr_name,
                  const char  *attr_val,
                  double      *val,
                  int          line_number,
                  GError     **error)
{
  *val = g_ascii_strtod (attr_val, NULL);
  if (errno != 0)
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "Value of '%s' attribute on <span> tag "
                   "on line %d could not be parsed; "
                   "should be a number, not '%s'",
                   attr_name, line_number, attr_val);
      return FALSE;
    }

  return TRUE;
}

static gboolean
span_parse_boolean (const char  *attr_name,
                    const char  *attr_val,
                    gboolean    *val,
                    int          line_number,
                    GError     **error)
{
  if (strcmp (attr_val, "true") == 0 ||
      strcmp (attr_val, "yes") == 0 ||
      strcmp (attr_val, "t") == 0 ||
      strcmp (attr_val, "y") == 0)
    *val = TRUE;
  else if (strcmp (attr_val, "false") == 0 ||
           strcmp (attr_val, "no") == 0 ||
           strcmp (attr_val, "f") == 0 ||
           strcmp (attr_val, "n") == 0)
    *val = FALSE;
  else
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "Value of '%s' attribute on <span> tag "
                   "line %d should have one of "
                   "'true/yes/t/y' or 'false/no/f/n': '%s' is not valid",
                   attr_name, line_number, attr_val);
      return FALSE;
    }

  return TRUE;
}

static gboolean
span_parse_color (const char   *attr_name,
                  const char   *attr_val,
                  Pango2Color  *color,
                  int           line_number,
                  GError      **error)
{
  if (!pango2_color_parse (color, attr_val))
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "Value of '%s' attribute on <span> tag "
                   "on line %d could not be parsed; "
                   "should be a color specification, not '%s'",
                   attr_name, line_number, attr_val);
      return FALSE;
    }

  return TRUE;
}

static gboolean
span_parse_enum (const char  *attr_name,
                 const char  *attr_val,
                 GType        type,
                 int         *val,
                 int          line_number,
                 GError     **error)
{
  char *possible_values = NULL;

  if (!_pango2_parse_enum (type, attr_val, val, FALSE, &possible_values))
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "'%s' is not a valid value for the '%s' "
                   "attribute on <span> tag, line %d; valid "
                   "values are %s",
                   attr_val, attr_name, line_number, possible_values);
      g_free (possible_values);
      return FALSE;
    }

  return TRUE;
}

static gboolean
span_parse_flags (const char  *attr_name,
                  const char  *attr_val,
                  GType        type,
                  int         *val,
                  int          line_number,
                  GError     **error)
{
  char *possible_values = NULL;

  if (!pango2_parse_flags (type, attr_val, val, &possible_values))
    {
      g_set_error (error,
                   G_MARKUP_ERROR,
                   G_MARKUP_ERROR_INVALID_CONTENT,
                   "'%s' is not a valid value for the '%s' "
                   "attribute on <span> tag, line %d; valid "
                   "values are %s or combinations with |",
                   attr_val, attr_name, line_number, possible_values);
      g_free (possible_values);
      return FALSE;
    }

  return TRUE;
}

static gboolean
parse_length (const char *attr_val,
              int        *result)
{
  const char *attr;
  int n;

  attr = attr_val;
  if (_pango2_scan_int (&attr, &n) && *attr == '\0')
    {
      *result = n;
      return TRUE;
    }
  else
    {
      double val;
      char *end;

      val = g_ascii_strtod (attr_val, &end);
      if (errno == 0 && strcmp (end, "pt") == 0)
        {
          *result = val * PANGO2_SCALE;
          return TRUE;
        }
    }

  return FALSE;
}

static gboolean
span_parse_func (MarkupData            *md G_GNUC_UNUSED,
                 OpenTag               *tag,
                 const char           **names,
                 const char           **values,
                 GMarkupParseContext   *context,
                 GError               **error)
{
  int line_number, char_number;
  int i;

  const char *family = NULL;
  const char *size = NULL;
  const char *style = NULL;
  const char *weight = NULL;
  const char *variant = NULL;
  const char *stretch = NULL;
  const char *desc = NULL;
  const char *foreground = NULL;
  const char *background = NULL;
  const char *underline = NULL;
  const char *underline_position = NULL;
  const char *underline_color = NULL;
  const char *overline = NULL;
  const char *overline_color = NULL;
  const char *strikethrough = NULL;
  const char *strikethrough_color = NULL;
  const char *rise = NULL;
  const char *baseline_shift = NULL;
  const char *letter_spacing = NULL;
  const char *lang = NULL;
  const char *fallback = NULL;
  const char *gravity = NULL;
  const char *gravity_hint = NULL;
  const char *font_features = NULL;
  const char *allow_breaks = NULL;
  const char *insert_hyphens = NULL;
  const char *show = NULL;
  const char *line_height = NULL;
  const char *text_transform = NULL;
  const char *segment = NULL;
  const char *font_scale = NULL;

  g_markup_parse_context_get_position (context,
                                       &line_number, &char_number);

#define CHECK_DUPLICATE(var) G_STMT_START{                              \
          if ((var) != NULL) {                                          \
            g_set_error (error, G_MARKUP_ERROR,                         \
                         G_MARKUP_ERROR_INVALID_CONTENT,                \
                         "Attribute '%s' occurs twice on <span> tag "   \
                         "on line %d char %d, may only occur once",     \
                         names[i], line_number, char_number);           \
            return FALSE;                                               \
          }}G_STMT_END
#define CHECK_ATTRIBUTE2(var, name) \
        if (attr_strcmp (names[i], (name)) == 0) { \
          CHECK_DUPLICATE (var); \
          (var) = values[i]; \
          found = TRUE; \
          break; \
        }
#define CHECK_ATTRIBUTE(var) CHECK_ATTRIBUTE2 (var, G_STRINGIFY (var))

  i = 0;
  while (names[i])
    {
      gboolean found = FALSE;

      switch (names[i][0]) {
      case 'a':
        CHECK_ATTRIBUTE (allow_breaks);
        break;
      case 'b':
        CHECK_ATTRIBUTE (background);
        CHECK_ATTRIBUTE2 (background, "bgcolor");
        CHECK_ATTRIBUTE (baseline_shift);
        break;
      case 'c':
        CHECK_ATTRIBUTE2 (foreground, "color");
        break;
      case 'f':
        CHECK_ATTRIBUTE (fallback);
        CHECK_ATTRIBUTE2 (desc, "font");
        CHECK_ATTRIBUTE2 (desc, "font_desc");
        CHECK_ATTRIBUTE2 (family, "face");

        CHECK_ATTRIBUTE2 (family, "font_family");
        CHECK_ATTRIBUTE2 (size, "font_size");
        CHECK_ATTRIBUTE2 (stretch, "font_stretch");
        CHECK_ATTRIBUTE2 (style, "font_style");
        CHECK_ATTRIBUTE2 (variant, "font_variant");
        CHECK_ATTRIBUTE2 (weight, "font_weight");
        CHECK_ATTRIBUTE (font_scale);

        CHECK_ATTRIBUTE (foreground);
        CHECK_ATTRIBUTE2 (foreground, "fgcolor");

        CHECK_ATTRIBUTE (font_features);
        break;
      case 's':
        CHECK_ATTRIBUTE (show);
        CHECK_ATTRIBUTE (size);
        CHECK_ATTRIBUTE (stretch);
        CHECK_ATTRIBUTE (strikethrough);
        CHECK_ATTRIBUTE (strikethrough_color);
        CHECK_ATTRIBUTE (style);
        CHECK_ATTRIBUTE (segment);
        break;
      case 't':
        CHECK_ATTRIBUTE (text_transform);
        break;
      case 'g':
        CHECK_ATTRIBUTE (gravity);
        CHECK_ATTRIBUTE (gravity_hint);
        break;
      case 'i':
        CHECK_ATTRIBUTE (insert_hyphens);
        break;
      case 'l':
        CHECK_ATTRIBUTE (lang);
        CHECK_ATTRIBUTE (letter_spacing);
        CHECK_ATTRIBUTE (line_height);
        break;
      case 'o':
        CHECK_ATTRIBUTE (overline);
        CHECK_ATTRIBUTE (overline_color);
        break;
      case 'u':
        CHECK_ATTRIBUTE (underline);
        CHECK_ATTRIBUTE (underline_position);
        CHECK_ATTRIBUTE (underline_color);
        break;
      case 'r':
        CHECK_ATTRIBUTE (rise);
        break;
      case 'v':
        CHECK_ATTRIBUTE (variant);
        break;
      case 'w':
        CHECK_ATTRIBUTE (weight);
        break;
      default:;
      }

      if (!found)
        {
          g_set_error (error, G_MARKUP_ERROR,
                       G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                       "Attribute '%s' is not allowed on the <span> tag "
                       "on line %d char %d",
                       names[i], line_number, char_number);
          return FALSE;
        }

      ++i;
    }

  /* Parse desc first, then modify it with other font-related attributes. */
  if (G_UNLIKELY (desc))
    {
      Pango2FontDescription *parsed;

      parsed = pango2_font_description_from_string (desc);
      if (parsed)
        {
          add_attribute (tag, pango2_attr_font_desc_new (parsed));
          if (tag)
            open_tag_set_absolute_font_size (tag, pango2_font_description_get_size (parsed));
          pango2_font_description_free (parsed);
        }
    }

  if (G_UNLIKELY (family))
    {
      add_attribute (tag, pango2_attr_family_new (family));
    }

  if (G_UNLIKELY (size))
    {
      int n;

      if (parse_length (size, &n) && n > 0)
        {
          add_attribute (tag, pango2_attr_size_new (n));
          if (tag)
            open_tag_set_absolute_font_size (tag, n);
        }
      else if (strcmp (size, "smaller") == 0)
        {
          if (tag)
            {
              tag->scale_level_delta -= 1;
              tag->scale_level -= 1;
            }
        }
      else if (strcmp (size, "larger") == 0)
        {
          if (tag)
            {
              tag->scale_level_delta += 1;
              tag->scale_level += 1;
            }
        }
      else if (parse_absolute_size (tag, size))
        ; /* nothing */
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Value of 'size' attribute on <span> tag on line %d "
                       "could not be parsed; should be an integer, or a "
                       "string such as 'small', not '%s'",
                       line_number, size);
          goto error;
        }
    }

  if (G_UNLIKELY (style))
    {
      Pango2Style pango2_style;

      if (pango2_parse_style (style, &pango2_style, FALSE))
        add_attribute (tag, pango2_attr_style_new (pango2_style));
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "'%s' is not a valid value for the 'style' attribute "
                       "on <span> tag, line %d; valid values are "
                       "'normal', 'oblique', 'italic'",
                       style, line_number);
          goto error;
        }
    }

  if (G_UNLIKELY (weight))
    {
      Pango2Weight pango2_weight;

      if (pango2_parse_weight (weight, &pango2_weight, FALSE))
        add_attribute (tag,
                       pango2_attr_weight_new (pango2_weight));
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "'%s' is not a valid value for the 'weight' "
                       "attribute on <span> tag, line %d; valid "
                       "values are for example 'light', 'ultrabold' or a number",
                       weight, line_number);
          goto error;
        }
    }

  if (G_UNLIKELY (variant))
    {
      Pango2Variant pango2_variant;

      if (pango2_parse_variant (variant, &pango2_variant, FALSE))
        add_attribute (tag, pango2_attr_variant_new (pango2_variant));
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "'%s' is not a valid value for the 'variant' "
                       "attribute on <span> tag, line %d; valid values are "
                       "'normal', 'smallcaps'",
                       variant, line_number);
          goto error;
        }
    }

  if (G_UNLIKELY (stretch))
    {
      Pango2Stretch pango2_stretch;

      if (pango2_parse_stretch (stretch, &pango2_stretch, FALSE))
        add_attribute (tag, pango2_attr_stretch_new (pango2_stretch));
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "'%s' is not a valid value for the 'stretch' "
                       "attribute on <span> tag, line %d; valid "
                       "values are for example 'condensed', "
                       "'ultraexpanded', 'normal'",
                       stretch, line_number);
          goto error;
        }
    }

  if (G_UNLIKELY (foreground))
    {
      Pango2Color color;

      if (!span_parse_color ("foreground", foreground, &color, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_foreground_new (&color));
    }

  if (G_UNLIKELY (background))
    {
      Pango2Color color;

      if (!span_parse_color ("background", background, &color, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_background_new (&color));
    }

  if (G_UNLIKELY (underline))
    {
      Pango2LineStyle style = PANGO2_LINE_STYLE_NONE;

      if (!span_parse_enum ("underline", underline, PANGO2_TYPE_LINE_STYLE, (int*)(void*)&style, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_underline_new (style));
    }

  if (G_UNLIKELY (underline_position))
    {
      Pango2UnderlinePosition pos = PANGO2_UNDERLINE_POSITION_NORMAL;

      if (!span_parse_enum ("underline_position", underline_position, PANGO2_TYPE_UNDERLINE_POSITION, (int*)(void*)&pos, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_underline_position_new (pos));
    }

  if (G_UNLIKELY (underline_color))
    {
      Pango2Color color;

      if (!span_parse_color ("underline_color", underline_color, &color, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_underline_color_new (&color));
    }

  if (G_UNLIKELY (overline))
    {
      Pango2LineStyle style = PANGO2_LINE_STYLE_NONE;

      if (!span_parse_enum ("overline", overline, PANGO2_TYPE_LINE_STYLE, (int*)(void*)&style, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_overline_new (style));
    }

  if (G_UNLIKELY (overline_color))
    {
      Pango2Color color;

      if (!span_parse_color ("overline_color", overline_color, &color, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_overline_color_new (&color));
    }

  if (G_UNLIKELY (gravity))
    {
      Pango2Gravity gr = PANGO2_GRAVITY_SOUTH;

      if (!span_parse_enum ("gravity", gravity, PANGO2_TYPE_GRAVITY, (int*)(void*)&gr, line_number, error))
        goto error;

      if (gr == PANGO2_GRAVITY_AUTO)
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "'%s' is not a valid value for the 'gravity' "
                       "attribute on <span> tag, line %d; valid "
                       "values are for example 'south', 'east', "
                       "'north', 'west'",
                       gravity, line_number);
          goto error;
        }

      add_attribute (tag, pango2_attr_gravity_new (gr));
    }

  if (G_UNLIKELY (gravity_hint))
    {
      Pango2GravityHint hint = PANGO2_GRAVITY_HINT_NATURAL;

      if (!span_parse_enum ("gravity_hint", gravity_hint, PANGO2_TYPE_GRAVITY_HINT, (int*)(void*)&hint, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_gravity_hint_new (hint));
    }

  if (G_UNLIKELY (strikethrough))
    {
      Pango2LineStyle style = PANGO2_LINE_STYLE_NONE;

      if (!span_parse_enum ("strikethrough", strikethrough, PANGO2_TYPE_LINE_STYLE, (int*)(void*)&style, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_strikethrough_new (style));
    }

  if (G_UNLIKELY (strikethrough_color))
    {
      Pango2Color color;

      if (!span_parse_color ("strikethrough_color", strikethrough_color, &color, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_strikethrough_color_new (&color));
    }

  if (G_UNLIKELY (fallback))
    {
      gboolean b = FALSE;

      if (!span_parse_boolean ("fallback", fallback, &b, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_fallback_new (b));
    }

  if (G_UNLIKELY (show))
    {
      Pango2ShowFlags flags;

      if (!span_parse_flags ("show", show, PANGO2_TYPE_SHOW_FLAGS, (int*)(void*)&flags, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_show_new (flags));
    }

  if (G_UNLIKELY (text_transform))
    {
      Pango2TextTransform tf;

      if (!span_parse_enum ("text_transform", text_transform, PANGO2_TYPE_TEXT_TRANSFORM, (int*)(void*)&tf, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_text_transform_new (tf));
    }

  if (G_UNLIKELY (rise))
    {
      int n = 0;

      if (!parse_length (rise, &n))
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Value of 'rise' attribute on <span> tag on line %d "
                       "could not be parsed; should be an integer, or a "
                       "string such as '5.5pt', not '%s'",
                       line_number, rise);
          goto error;
        }

      add_attribute (tag, pango2_attr_rise_new (n));
    }

  if (G_UNLIKELY (baseline_shift))
    {
      int shift = 0;

      if (span_parse_enum ("baseline_shift", baseline_shift, PANGO2_TYPE_BASELINE_SHIFT, (int*)(void*)&shift, line_number, NULL))
        add_attribute (tag, pango2_attr_baseline_shift_new (shift));
      else if (parse_length (baseline_shift, &shift) && (shift > 1024 || shift < -1024))
        add_attribute (tag, pango2_attr_baseline_shift_new (shift));
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Value of 'baseline_shift' attribute on <span> tag on line %d "
                       "could not be parsed; should be 'superscript' or 'subscript' or "
                       "an integer, or a string such as '5.5pt', not '%s'",
                       line_number, baseline_shift);
          goto error;
        }

    }

  if (G_UNLIKELY (font_scale))
    {
      Pango2FontScale scale;

      if (!span_parse_enum ("font_scale", font_scale, PANGO2_TYPE_FONT_SCALE, (int*)(void*)&scale, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_font_scale_new (scale));
    }

  if (G_UNLIKELY (letter_spacing))
    {
      int n = 0;

      if (!span_parse_int ("letter_spacing", letter_spacing, &n, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_letter_spacing_new (n));
    }

  if (G_UNLIKELY (line_height))
    {
      double f = 0;

      if (!span_parse_float ("line_height", line_height, &f, line_number, error))
        goto error;

      if (f > 1024.0 && strchr (line_height, '.') == 0)
        add_attribute (tag, pango2_attr_line_height_new_absolute ((int)f));
      else
        add_attribute (tag, pango2_attr_line_height_new (f));
    }

  if (G_UNLIKELY (lang))
    {
      add_attribute (tag,
                     pango2_attr_language_new (pango2_language_from_string (lang)));
    }

  if (G_UNLIKELY (font_features))
    {
      add_attribute (tag, pango2_attr_font_features_new (font_features));
    }

  if (G_UNLIKELY (allow_breaks))
    {
      gboolean b = FALSE;

      if (!span_parse_boolean ("allow_breaks", allow_breaks, &b, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_allow_breaks_new (b));
    }

  if (G_UNLIKELY (insert_hyphens))
    {
      gboolean b = FALSE;

      if (!span_parse_boolean ("insert_hyphens", insert_hyphens, &b, line_number, error))
        goto error;

      add_attribute (tag, pango2_attr_insert_hyphens_new (b));
    }

  if (G_UNLIKELY (segment))
    {
      if (strcmp (segment, "word") == 0)
        add_attribute (tag, pango2_attr_word_new ());
      else if (strcmp (segment, "sentence") == 0)
        add_attribute (tag, pango2_attr_sentence_new ());
      else if (strcmp (segment, "paragraph") == 0)
        add_attribute (tag, pango2_attr_paragraph_new ());
      else
        {
          g_set_error (error,
                       G_MARKUP_ERROR,
                       G_MARKUP_ERROR_INVALID_CONTENT,
                       "Value of 'segment' attribute on <span> tag on line %d "
                       "could not be parsed; should be one of 'word', 'sentence' "
                       "or 'paragraph', not '%s'",
                       line_number, segment);
          goto error;
        }
    }

  return TRUE;

 error:

  return FALSE;
}

static gboolean
i_parse_func (MarkupData            *md G_GNUC_UNUSED,
              OpenTag               *tag,
              const char           **names,
              const char           **values G_GNUC_UNUSED,
              GMarkupParseContext   *context,
              GError               **error)
{
  CHECK_NO_ATTRS ("i");

  add_attribute (tag, pango2_attr_style_new (PANGO2_STYLE_ITALIC));

  return TRUE;
}

static gboolean
markup_parse_func (MarkupData            *md G_GNUC_UNUSED,
                   OpenTag               *tag G_GNUC_UNUSED,
                   const char           **names G_GNUC_UNUSED,
                   const char           **values G_GNUC_UNUSED,
                   GMarkupParseContext   *context G_GNUC_UNUSED,
                   GError               **error G_GNUC_UNUSED)
{
  /* We don't do anything with this tag at the moment. */
  CHECK_NO_ATTRS ("markup");

  return TRUE;
}

static gboolean
s_parse_func (MarkupData            *md G_GNUC_UNUSED,
              OpenTag               *tag,
              const char           **names,
              const char           **values G_GNUC_UNUSED,
              GMarkupParseContext   *context,
              GError               **error)
{
  CHECK_NO_ATTRS ("s");

  add_attribute (tag, pango2_attr_strikethrough_new (TRUE));

  return TRUE;
}

static gboolean
sub_parse_func (MarkupData            *md G_GNUC_UNUSED,
                OpenTag               *tag,
                const char           **names,
                const char           **values G_GNUC_UNUSED,
                GMarkupParseContext   *context,
                GError               **error)
{
  CHECK_NO_ATTRS ("sub");

  add_attribute (tag, pango2_attr_font_scale_new (PANGO2_FONT_SCALE_SUBSCRIPT));
  add_attribute (tag, pango2_attr_baseline_shift_new (PANGO2_BASELINE_SHIFT_SUBSCRIPT));

  return TRUE;
}

static gboolean
sup_parse_func (MarkupData            *md G_GNUC_UNUSED,
                OpenTag               *tag,
                const char           **names,
                const char           **values G_GNUC_UNUSED,
                GMarkupParseContext   *context,
                GError               **error)
{
  CHECK_NO_ATTRS ("sup");

  add_attribute (tag, pango2_attr_font_scale_new (PANGO2_FONT_SCALE_SUPERSCRIPT));
  add_attribute (tag, pango2_attr_baseline_shift_new (PANGO2_BASELINE_SHIFT_SUPERSCRIPT));

  return TRUE;
}

static gboolean
small_parse_func (MarkupData            *md G_GNUC_UNUSED,
                  OpenTag               *tag,
                  const char           **names,
                  const char           **values G_GNUC_UNUSED,
                  GMarkupParseContext   *context,
                  GError               **error)
{
  CHECK_NO_ATTRS ("small");

  /* Shrink text one level */
  if (tag)
    {
      tag->scale_level_delta -= 1;
      tag->scale_level -= 1;
    }

  return TRUE;
}

static gboolean
tt_parse_func (MarkupData            *md G_GNUC_UNUSED,
               OpenTag               *tag,
               const char           **names,
               const char           **values G_GNUC_UNUSED,
               GMarkupParseContext   *context,
               GError               **error)
{
  CHECK_NO_ATTRS ("tt");

  add_attribute (tag, pango2_attr_family_new ("Monospace"));

  return TRUE;
}

static gboolean
u_parse_func (MarkupData            *md G_GNUC_UNUSED,
              OpenTag               *tag,
              const char           **names,
              const char           **values G_GNUC_UNUSED,
              GMarkupParseContext   *context,
              GError               **error)
{
  CHECK_NO_ATTRS ("u");

  add_attribute (tag, pango2_attr_underline_new (PANGO2_LINE_STYLE_SOLID));

  return TRUE;
}