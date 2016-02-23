/* mbed
 * Copyright (c) 2006-2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "uif-tools-1bit/font.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>


/// - Private Constants

/** This character is rendered when the font doesn't have the requested
 *  character. If a font doesn't have this character, then the character is
 *  ignored completely. */
// !!! FIXME: should be per-font
const char Placeholder_Char = '?';


/// - Private Function Declarations
static void advanceMetricsWithGlyph(
            struct FontMetrics* metrics,
    struct FontGlyphData const* glyph
);
static struct CompBuf glyphForGlyphData(
         struct FontData const* with_font,
    struct FontGlyphData const* glyph,
                           bool value
);


/// - Public Function Definitions
struct FontGlyphData const* fontGlyphDataForChar(struct FontData const* with_font, char c){
    struct FontGlyphDataBlock const* const b = with_font->glyph_blocks;
    for (int i = 0; i < with_font->num_glyph_blocks; i++)
        if (c >= b[i].start_char &&
            c  < b[i].start_char + b[i].num_chars)
            return &b[i].glyphs[c - b[i].start_char];
    return NULL;
}

struct CompBuf fontGlyphForChar(struct FontData const* with_font, char c, bool value){
    return glyphForGlyphData(with_font, fontGlyphDataForChar(with_font, c), value);
}

struct FontMetrics fontMetricsForStr(struct FontData const* with_font, const char* s){
    struct FontMetrics r = {
        .width  = 0,
        .height = 0,
        .x_advance = 0,
        .y_offset  = with_font->base // this is the y_offset for an empty string
    };

    while (*s) {
        if (!iscntrl((int)*s)) {
            struct FontGlyphData const* g = fontGlyphDataForChar(with_font, *s);
            // try the placeholder
            if (!g)
                g = fontGlyphDataForChar(with_font, Placeholder_Char);
            // there is no guarantee the font has a placeholder char
            if (g)
                advanceMetricsWithGlyph(&r, g);
        }
        s++;
    }
    return r;
}

void fontRenderStr(
            struct CompBuf into_buffer,
    struct FontData const* with_font,
                   int16_t at_baseline,
               const char* s,
                      bool value
){
    while (*s) {
        if (!iscntrl((int)*s)) {
            struct FontGlyphData const* g = fontGlyphDataForChar(with_font, *s);
            // try the placeholder
            if (!g)
                g = fontGlyphDataForChar(with_font, Placeholder_Char);
            // there is no guarantee the font has a placeholder char
            if (g) {
                struct CompBuf b = glyphForGlyphData(with_font, g, value);
                // rel_y is top of glyph relative to top of destination buffer:
                // may be -ve, in which case we chop off the top of the glyph
                int rel_y = at_baseline - with_font->base + g->y_offset;
                if (rel_y > 0) {
                    compBlit(compBufAtPxOffset(0, rel_y, into_buffer), b, true);
                } else {
                    b = compBufAtPxOffset(0, -rel_y, b);
                    compBlit(into_buffer, b, true);
                }
                // advance the destination buffer so the origin is at the
                // origin of the next character:
                into_buffer = compBufAtPxOffset(g->x_advance, 0, into_buffer);
            }
        }
        s++;
    }
}

/// - Display List Functions (conform to compDl_action_fn)

void fontDlRenderStr(struct CompBuf dst, void* arg){
    struct FontDLRenderStringArgs* a = (struct FontDLRenderStringArgs*) arg;
    if (a->x_origin < 0) {
        // render to a temporary buffer to do clipping
        // FIXME: just add an x_origin parameter to fontRenderStr
        struct FontMetrics m = fontMetricsForStr(a->font, a->str);
        int16_t stride = (m.width + 7)/8;
        struct CompBuf tmp = {
            .buf  = malloc(stride * m.height),
            .mask = malloc(stride * m.height),
            .bit_offset     = 0,
            .stride_bytes   = stride,
            .width_bits     = m.width,
            .height_strides = m.height
        };

        int16_t at_y_top = a->y_baseline + m.y_offset - a->font->base;
        if (at_y_top < 0) {
            fontRenderStr(tmp, a->font, m.y_offset, a->str, a->value);
            compBlit(dst, compBufAtPxOffset(-a->x_origin, -at_y_top, tmp), true);
        } else {
            fontRenderStr(tmp, a->font, m.y_offset, a->str, a->value);
            compBlit(compBufAtPxOffset(0, at_y_top, dst), compBufAtPxOffset(-a->x_origin, 0, tmp), true);
        }

        free(tmp.buf);
        free(tmp.mask);

    } else {
        fontRenderStr(compBufAtPxOffset(a->x_origin, 0, dst), a->font, a->y_baseline, a->str, a->value);
    }
}


/// - Private Function Definitions
void advanceMetricsWithGlyph(struct FontMetrics* metrics, struct FontGlyphData const* glyph){
    metrics->width      = metrics->x_advance + glyph->w;
    metrics->x_advance += glyph->x_advance;

    // space is defined as {x, y, w, h, y_offset, x_advance} = {0, 0, 0, 0, 0, 6}
    // y_offset should be FontData.base instead of 0
    // workaround, skip glyphs with no height
    if (glyph->h > 0)
    {
        // evaluate glyph metrics above the baseline
        if (glyph->y_offset < metrics->y_offset)
        {
            metrics->height += (metrics->y_offset - glyph->y_offset);
            metrics->y_offset = glyph->y_offset;
        }
        // else, glyph too small to change string metrics

        // evaluate glyph metrics below the baseline
        uint16_t glyph_bottom = glyph->y_offset + glyph->h;
        uint16_t string_bottom = metrics->y_offset + metrics->height;
        if (glyph_bottom > string_bottom)
        {
            metrics->height += glyph_bottom - string_bottom;
        }
        // else, glyph too small to change string metrics
    }
}

static struct CompBuf glyphForGlyphData(
         struct FontData const* with_font,
    struct FontGlyphData const* g,
                           bool value
){
    if (!g) {
        struct CompBuf r = {NULL,NULL,0,0,0,0};
        return r;
    } else {
        struct CompBuf r = {
            .buf            = (uint8_t*) (value? Comp_Fill_Ones : Comp_Fill_Zeros),
            // !!! discarding const qualifier
            .mask           = (uint8_t*) with_font->buf + g->y * with_font->stride
                                                        + g->x / 8,
            .bit_offset     = g->x % 8,
            .stride_bytes   = with_font->stride,
            .width_bits     = g->w,
            .height_strides = g->h

        };
        return r;
    }
}
