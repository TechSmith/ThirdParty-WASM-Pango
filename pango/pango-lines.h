#pragma once

#include <glib-object.h>

#include <pango/pango-types.h>
#include <pango/pango-line.h>

G_BEGIN_DECLS

#define PANGO_TYPE_LINES pango_lines_get_type ()

PANGO_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (PangoLines, pango_lines, PANGO, LINES, GObject);

PANGO_AVAILABLE_IN_ALL
PangoLines *            pango_lines_new             (void);

PANGO_AVAILABLE_IN_ALL
guint                   pango_lines_get_serial      (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_add_line        (PangoLines        *lines,
                                                     PangoLine         *line,
                                                     int                line_x,
                                                     int                line_y);

PANGO_AVAILABLE_IN_ALL
int                     pango_lines_get_line_count  (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
PangoLine *             pango_lines_get_line        (PangoLines        *lines,
                                                     int                num,
                                                     int               *line_x,
                                                     int               *line_y);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_get_extents     (PangoLines        *lines,
                                                     PangoRectangle    *ink_rect,
                                                     PangoRectangle    *logical_rect);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_get_size        (PangoLines        *lines,
                                                     int               *width,
                                                     int               *height);

PANGO_AVAILABLE_IN_ALL
int                     pango_lines_get_baseline    (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_get_x_ranges    (PangoLines        *lines,
                                                     PangoLine         *line,
                                                     PangoLine         *start_line,
                                                     int                start_index,
                                                     PangoLine         *end_line,
                                                     int                end_index,
                                                     int              **ranges,
                                                     int               *n_ranges);

PANGO_AVAILABLE_IN_ALL
int                     pango_lines_get_unknown_glyphs_count
                                                    (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
gboolean                pango_lines_wrapped         (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
gboolean                pango_lines_ellipsized      (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
gboolean                pango_lines_hyphenated      (PangoLines        *lines);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_index_to_line   (PangoLines        *lines,
                                                     int                idx,
                                                     PangoLine        **line,
                                                     int               *line_no,
                                                     int               *x_offset,
                                                     int               *y_offset);

PANGO_AVAILABLE_IN_ALL
PangoLine *             pango_lines_pos_to_line     (PangoLines        *lines,
                                                     int                x,
                                                     int                y,
                                                     int               *line_x,
                                                     int               *line_y);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_index_to_pos    (PangoLines        *lines,
                                                     PangoLine         *line,
                                                     int                idx,
                                                     PangoRectangle    *pos);

PANGO_AVAILABLE_IN_ALL
PangoLine *             pango_lines_pos_to_index    (PangoLines        *lines,
                                                     int                x,
                                                     int                y,
                                                     int               *idx,
                                                     int               *trailing);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_get_cursor_pos  (PangoLines        *lines,
                                                     PangoLine         *line,
                                                     int                idx,
                                                     PangoRectangle    *strong_pos,
                                                     PangoRectangle    *weak_pos);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_get_caret_pos   (PangoLines        *lines,
                                                     PangoLine         *line,
                                                     int                idx,
                                                     PangoRectangle    *strong_pos,
                                                     PangoRectangle    *weak_pos);

PANGO_AVAILABLE_IN_ALL
void                    pango_lines_move_cursor     (PangoLines        *lines,
                                                     gboolean           strong,
                                                     PangoLine         *line,
                                                     int                idx,
                                                     int                trailing,
                                                     int                direction,
                                                     PangoLine        **new_line,
                                                     int               *new_idx,
                                                     int               *new_trailing);

PANGO_AVAILABLE_IN_ALL
GBytes *                pango_lines_serialize       (PangoLines        *lines);

G_END_DECLS
