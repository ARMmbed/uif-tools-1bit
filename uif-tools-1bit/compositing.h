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

#ifndef __COMPOSITING_H__
#define __COMPOSITING_H__

#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

enum CompMagicPointers{
    Comp_Fill_Zeros = 0,
    Comp_Fill_Ones  = 1,
    Comp_Magic_Pointer_Bits = 1
};

/** Basic buffer object
 *
 *  Blitting is always done into one of these, and raster blitting is done from
 *  a CompBuf into another CompBuf.
 *
 *  Note that there is no way to specify an off-the-top or off-the-left
 *  origin in a destination CompBuf: to draw things that are partially
 *  off-the-top of off-the-left the source CompBuf must be adjusted / the
 *  parametrised rendering function must accept negative coordinate (in which
 *  case it MUST only render to output pixels that are indexed > 0).
 *
 *  The above limitation is by careful design: it reduces the size of this
 *  structure.
 *
 */
struct CompBuf{
    uint8_t* buf;   // if equal to one of the magic pointer values (enum
                    // CompMagicPointers), then the buffer is interpreted as
                    // ones/zeros. Test for this with compPtrIsMagic
    uint8_t* mask;  // if equal to one of the magic pointer values (enum
                    // CompMagicPointers), then the buffer is interpreted as
                    // ones/zeros. Test for this with compPtrIsMagic
    uint8_t  bit_offset;
    uint16_t stride_bytes;
    // width is the valid region width, i.e. stride must be at least:
    //  (bit_offset + width + 7) / 8
    uint16_t width_bits;
    uint16_t height_strides;
};


/** Structure representing an individual drawing action */
typedef void (comp_dl_action_fn)(struct CompBuf dst, void* arg);
struct CompDisplayAction{
    comp_dl_action_fn* fn;
    //iotos_tick_t (validity_fn)(void* arg);
    void* arg;
};
/** Convenience constructor for CompDisplayAction */
inline static struct CompDisplayAction compDisplayAction(
    comp_dl_action_fn* fn,
                 void* arg
){
    struct CompDisplayAction r = {fn, arg};
    return r;
}

/** Structure representing an element of a linked list holding
 *  CompDisplayAction instances (the structures that represent individual
 *  drawing actions).
 */
struct CompDisplayListElement{
    struct CompDisplayAction action;
    struct CompDisplayListElement *next;
};
/** Convenience constructor for CompDisplayListElement */
inline static struct CompDisplayListElement compDisplayListElement(
         struct CompDisplayAction action,
    struct CompDisplayListElement *next
 ){
    struct CompDisplayListElement r = {action, next};
    return r;
}


/** Calculate the buffer at the specified offset from `from_buf'.
 */
struct CompBuf compBufAtPxOffset(int16_t x, int16_t y, struct CompBuf from_buf);
struct CompBuf compBufAtPxOffsetWithSize(int16_t x, int16_t y, uint16_t width, uint16_t height, struct CompBuf from_buf);

/** Copy pixels from src to dst wherever src mask is 1. If update_dst_mask,
 *  then the destination transparency mask is set for each pixel copied.
 */
void compBlit(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);
/** Adjust src and/or dst as necessary to bit src at x,y (which may be -ve) into
 *  dst.
 */
void compBlitAt(struct CompBuf dst, struct CompBuf src, int16_t x, int16_t y, bool update_dst_mask);

/** Fill (or clear) a rectangle in the destination buffer */
void compFillRect(struct CompBuf dst, int16_t x0, int16_t x1, int16_t y0, int16_t y1, bool fill);

/** Invert a rectangle in the destination buffer */
void compInvertRect(struct CompBuf dst, int16_t x0s, int16_t _x1s, int16_t y0s, int16_t _y1s);

/** render the specified display list into the specified buffer */
void compRenderDL(struct CompBuf dst, struct CompDisplayListElement* head);

/** Return true for pointer values with special meanings: test buf and mask
 *  pointers with this before dereferencing.
 */
inline static bool compPtrIsMagic(uint8_t const* p){
    return (((int) p) & Comp_Magic_Pointer_Bits) == (int) p;
}

/// - Display List Functions (conform to comp_dl_action_fn)

/** argument structure for compDlFillRect */
struct CompDLFillRectArgs{
    int16_t x0;
    int16_t x1;
    int16_t y0;
    int16_t y1;
    bool    fill;
};
/** draw a rectangle on the destination */
comp_dl_action_fn compDlFillRect;

/** argument structure for compBlit */
struct CompDLBlitArgs{
    struct CompBuf src;
    int16_t x_off;
    int16_t y_off;
};
/** blit a CompBuf into the destination */
comp_dl_action_fn compDlBlit;

/** render a display list as if it were a single display list element: allows
 *  display list functions to be bundled opaquely. Pass a CompDisplayListElement
 *  pointer as the argument;
 */
comp_dl_action_fn compDlDrawDisplayList;

/// Convenient wrappers for display list functions
//struct CompDisplayAction compMakeDLFillRect(int16_t x_off, int16_t y_off, struct CompBuf src);
//void compFreeDLFillRect(struct CompDisplayListAction act);
//struct CompDisplayAction compMakeDLBlit(int16_t x_off, int16_t y_off, struct CompBuf src);
//void compFreeDLBlit(struct CompDisplayListAction act);

/// - Debug / Dev utilities

/** slow, reference version of the blitter, which iterates over bits instead of
 *  bit twiddling. Should be equivalent to compBlit.
 */
void compDbgBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);
void compDbgFillRect(struct CompBuf dst, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, bool fill);
void compDbgInvertRect(struct CompBuf dst, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
void compDbgPrint(struct CompBuf buf);
struct CompBuf compDbgAllocCircle(int32_t diameter, uint8_t bit_offset, uint8_t fill);

void compFillFun(struct CompBuf dst, bool(*userFillFunction)(uint16_t, uint16_t), int16_t xOffset, int16_t yOffset, bool fill);


#ifdef __cplusplus
}
#endif

#endif // __COMPOSITING_H__
