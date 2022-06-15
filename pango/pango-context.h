/*
 * Copyright (C) 2000 Red Hat Software
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <pango/pango-types.h>
#include <pango/pango-font.h>
#include <pango/pango-fontmap.h>
#include <pango/pango-attributes.h>
#include <pango/pango-direction.h>

G_BEGIN_DECLS

#define PANGO_TYPE_CONTEXT              (pango_context_get_type ())

PANGO_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (PangoContext, pango_context, PANGO, CONTEXT, GObject);

PANGO_AVAILABLE_IN_ALL
PangoContext *          pango_context_new                       (void);
PANGO_AVAILABLE_IN_ALL
PangoContext *          pango_context_new_with_font_map         (PangoFontMap                 *font_map);

PANGO_AVAILABLE_IN_ALL
void                    pango_context_changed                   (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_font_map              (PangoContext                 *context,
                                                                 PangoFontMap                 *font_map);
PANGO_AVAILABLE_IN_ALL
PangoFontMap *          pango_context_get_font_map              (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
guint                   pango_context_get_serial                (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
PangoFont *             pango_context_load_font                 (PangoContext                 *context,
                                                                 const PangoFontDescription   *desc);
PANGO_AVAILABLE_IN_ALL
PangoFontset *          pango_context_load_fontset              (PangoContext                 *context,
                                                                 const PangoFontDescription   *desc,
                                                                 PangoLanguage                *language);

PANGO_AVAILABLE_IN_ALL
PangoFontMetrics *      pango_context_get_metrics               (PangoContext                 *context,
                                                                 const PangoFontDescription   *desc,
                                                                 PangoLanguage                *language);

PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_font_description      (PangoContext                 *context,
                                                                 const PangoFontDescription   *desc);
PANGO_AVAILABLE_IN_ALL
PangoFontDescription *  pango_context_get_font_description      (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
PangoLanguage *         pango_context_get_language              (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_language              (PangoContext                 *context,
                                                                 PangoLanguage                *language);
PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_base_dir              (PangoContext                 *context,
                                                                 PangoDirection                direction);
PANGO_AVAILABLE_IN_ALL
PangoDirection          pango_context_get_base_dir              (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_base_gravity          (PangoContext                 *context,
                                                                 PangoGravity                  gravity);
PANGO_AVAILABLE_IN_ALL
PangoGravity            pango_context_get_base_gravity          (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
PangoGravity            pango_context_get_gravity               (PangoContext                 *context);
PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_gravity_hint          (PangoContext                 *context,
                                                                 PangoGravityHint              hint);
PANGO_AVAILABLE_IN_ALL
PangoGravityHint        pango_context_get_gravity_hint          (PangoContext                 *context);

PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_matrix                (PangoContext                 *context,
                                                                 const PangoMatrix            *matrix);
PANGO_AVAILABLE_IN_ALL
const PangoMatrix *     pango_context_get_matrix                (PangoContext                 *context);

PANGO_AVAILABLE_IN_ALL
void                    pango_context_set_round_glyph_positions (PangoContext                 *context,
                                                                 gboolean                      round_positions);
PANGO_AVAILABLE_IN_ALL
gboolean                pango_context_get_round_glyph_positions (PangoContext                 *context);

G_END_DECLS
