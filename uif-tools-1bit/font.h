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

#ifndef __FONT_H__
#define __FONT_H__


#include "uif-tools-1bit/compositing.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// !!! FIXME: no kerning yet (source font file doesn't contain any kerning info)

/// - Public Types (Font Structures should probably be private)
struct FontGlyphData{
    uint8_t         x;    // x position of top left of gylph image in font bitmap [0,256]
    uint8_t         y;    // y position of top left of gylph image in font bitmap [0,256]
    uint8_t         w;    // glyph image with,   unsigned, [0, 64]
    uint8_t         h;    // glyph image height, unsigned, [0, 64]
    int8_t   y_offset;    // y_offset of top left of glyph from origin (can be negative) [-128,127]
    uint8_t x_advance;    // character x_advance [0,256]
};

// A contiguous block of ascii characters (e.g. A-Z, or !-/)
struct FontGlyphDataBlock{
    struct FontGlyphData const* glyphs;
    uint8_t                  num_chars; // number of characters in this block
                                        // of characters.
    char                    start_char; // first ascii char in this block of
                                        // characters, subsequent chars are the
                                        // subsequent ascii characters.
};

struct FontData{
    struct FontGlyphDataBlock const* glyph_blocks;
    uint8_t const*       buf;
    uint32_t          stride;
    // lineHeight? base? padding?
    uint8_t num_glyph_blocks;
    uint8_t          space_x;
    uint8_t          space_y;
    uint8_t             size;
    uint8_t             base; // y offset of baseline from origin, see diagram
    uint8_t      line_height;
};

/** Font Metrics
 *
 *          |---------------------------> width(opl)
 *                      |-----> width(p)
 *                                  |---> width (l)
 *
 * y_off(o) y_off(opl) - - - - - - - - - y_off(l) - - - - - - --- origin
 *     |       |                            |                  |
 *     |       |                            |                  |
 *     |       |                            |      height(opl) |
 *     |      \|/             height(l)    \|/          .      |
 *     |    o  '                 .  o_      '          /|\     |
 *    \|/                       /|\ | |                 |      |
 *     '    o  _        o  _     |  | |                 |      |
 *           /   \       /   \   |  | |                 |      |
 *          |  O  |     | |O| |  |  | |                 |      |
 *           \   /      |  _ /   |  |  \                |     \|/
 *           -"""- - - -| |- - - - - """ - - - - - - - -|- - - ' base -
 *                      | |                             |
 *                      |_|                             _
 *          |----------> advance(o)
 *                      |-----------> advance(p)
 *                                   |-----> advance(l)
 *          |------------------------------> advance(opl)
 */
struct FontMetrics{
    uint16_t width;
    uint16_t height;
    uint16_t x_advance; // x advance of composited string
    uint16_t y_offset;  // y offset of baseline in composited string
};

/// - Public Functions
/** Return the FontGlyphData structure for the specified character in the
 *  specified font. */
struct FontGlyphData const* fontGlyphDataForChar(struct FontData const* with_font, char c);
/** Return a CompBuf allowing the specified character in the specified font to
 *  be rendered, filled with value (on/off pixel values) */
struct CompBuf fontGlyphForChar(struct FontData const* with_font, char c, bool value);
/** Calculate the combined metrics for a string of characters. All control
 *  characters (including newlines) are ignored. */
struct FontMetrics fontMetricsForStr(struct FontData const* with_font, const char* s);
/** Render a string of characters into a user-provided destination CompBuf.
 *
 * See FontMetrics structure documentation for a diagram of how combined
 * metrics are calculated.
 *
 * Note that because this function does not accept an x offset position to
 * render to (yet) it is NOT possible to instruct the rendering to be clipped
 * by the left of the buffer. To achieve that you must render the whole string
 * into a temporary CompBuf, then create a CompBuf referencing the non-clipped
 * portion of the rendered string, then render the referencing CompBuf to the
 * ultimate destination.
 *
 * !!! FIXME: add an offset to allow left clipping if it proves necessary
 */
void fontRenderStr(struct CompBuf into_buffer, struct FontData const* with_font, int16_t at_y_baseline, const char* s, bool value);


/// - Display List Functions (conform to comp_dl_action_fn)

/** argument structure for fontDlRenderStr */
struct FontDLRenderStringArgs{
    const char*            str;
    struct FontData const* font;
    int16_t                x_origin;
    int16_t                y_baseline;
    bool                   value;
};
/** draw a rectangle on the destination */
comp_dl_action_fn fontDlRenderStr;

#ifdef __cplusplus
}
#endif

#endif // __FONT_H__
