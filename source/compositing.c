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

#include "uif-tools-1bit/compositing.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if YOTTA_CFG_HARDWARE_WRD_BIT_BAND_PRESENT
#define SRAM_BASE        YOTTA_CFG_HARDWARE_WRD_BIT_BAND_RAM_BASE
#define BITBAND_RAM_BASE YOTTA_CFG_HARDWARE_WRD_BIT_BAND_BIT_BAND_BASE
#else
#define SRAM_BASE        0xFFFFFFFF
#define BITBAND_RAM_BASE 0
#endif

typedef enum {
    WORD = 32,
    HALFWORD = 16,
    BYTE = 8
} word_size_t;


/// - Private Function Declarations
static void blitByteAligned(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);
static void blitByteAlignedVariable(struct CompBuf dst, struct CompBuf src, bool update_dst_mask, word_size_t word_size);
static void blitDstByteAligned(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);
static void blitDstByteAlignedVariable(struct CompBuf dst, struct CompBuf src, bool update_dst_mask, word_size_t word_size);

static void compDbgSRAMBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);
static void compDbgCodeBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask);

static int32_t min(int32_t a, int32_t b){
    return a < b? a : b;
}

/// - Public Implementation

struct CompBuf compBufAtPxOffset(int16_t x, int16_t y, struct CompBuf from_buf)
{
    int16_t x_pos = (x > 0) ? x : 0;
    int16_t y_pos = (y > 0) ? y : 0;

    if ( (x_pos >= from_buf.width_bits) || (y_pos >= from_buf.height_strides) )
    {
        struct CompBuf buffer = {
            .buf            = (uint8_t*)Comp_Fill_Zeros,
            .mask           = (uint8_t*)Comp_Fill_Zeros,
            .bit_offset     = 0,
            .stride_bytes   = 0,
            .width_bits     = 0,
            .height_strides = 0
        };

        return buffer;
    }
    else
    {
        const int32_t bytes_right = (x_pos + from_buf.bit_offset) / 8;
        const int32_t bits_right  = (x_pos + from_buf.bit_offset) % 8;
        struct CompBuf buffer = {
            .buf            = compPtrIsMagic(from_buf.buf)?  from_buf.buf  : (from_buf.buf  + from_buf.stride_bytes * y_pos + bytes_right),
            .mask           = compPtrIsMagic(from_buf.mask)? from_buf.mask : (from_buf.mask + from_buf.stride_bytes * y_pos + bytes_right),
            .bit_offset     = bits_right,
            .stride_bytes   = from_buf.stride_bytes,
                              // adjust width for the carry from bit_offset
                              // into the buf pointer
            .width_bits     = from_buf.width_bits - x_pos,
            .height_strides = from_buf.height_strides - y_pos
        };

        return buffer;
    }

#if 0
    {
        NSAssert(0, ""); // this code is untested and probably has errors: remove this assertion and step through it when its first used
        const int32_t bytes_left = (-(x + from_buf.bit_offset) + 7) / 8; // round up, bit_offset isn't allowed to be -ve
        const int32_t bits_right = x + 8 * bytes_left;
        NSAssert(bits_right > 0, "");
        struct CompBuf r = {
            .buf            = compPtrIsMagic(from_buf.buf)?  from_buf.buf  : (from_buf.buf  + from_buf.stride_bytes * y - bytes_left),
            .mask           = compPtrIsMagic(from_buf.mask)? from_buf.mask : (from_buf.mask + from_buf.stride_bytes * y - bytes_left),
            .bit_offset     = bits_right,
            .stride_bytes   = from_buf.stride_bytes,
                              // adjust for carry from buf pointer into the
                              // bit_offset
            .width_bits     = from_buf.width_bits + 8 * bytes_left - (bits_right - from_buf.bit_offset),
            .height_strides = from_buf.height_strides - y
        };
        return r;
    }
#endif
}

struct CompBuf compBufAtPxOffsetWithSize(int16_t x, int16_t y, uint16_t width, uint16_t height, struct CompBuf from_buf)
{
    // get compbuf with (x, y) offset
    struct CompBuf buffer = compBufAtPxOffset(x, y, from_buf);

    // adjust compbuf width and height

    /*  x-offset is outside compbuf.
    */
    if (x < 0)
    {
        /*  Make (x + width) == buffer.width_bits.
        */
        int32_t x_width = x + width;

        if (x_width < 0)
        {
            // x-offset is larger than width, compbuf is empty
            buffer.width_bits = 0;
        }
        else
        {
            // x is negative, x_width is less than width
            buffer.width_bits = x_width;
        }
    }
    /*  x-offset is inside compbuf
    */
    else if (buffer.width_bits > width)
    {
        // clip combBuf to the requested width
        buffer.width_bits = width;
    }

    /*  y-offset is outside compbuf
    */
    if (y < 0)
    {
        /*  Make (y + height) == buffer.height_strides.
        */
        int32_t y_height = y + height;

        if (y_height < 0)
        {
            // y-offset is larger than height, compbuf is empty
            buffer.height_strides = 0;
        }
        else
        {
            // y is negative, y_height is less than height
            buffer.height_strides = y_height;
        }
    }
    /*  y-offset is inside compbuf
    */
    else if (buffer.height_strides > height)
    {
        // clip compbuf to the requested height
        buffer.height_strides = height;
    }

    return buffer;
}

/** Blitting.
 *
 *  XXX WARNING: incomplete, calls out to compDbgBlitReference  XXX
 *
 * Note that we treat the bits as least-significant-bit-first, and the bytes as
 * least-significant-byte-first, so a row of pixels is all the active bits in a
 * row of bytes in the order of their number B.b where B is the byte address
 * and b is the bit number
 *
 * The origin of the buffer is at the top left corner of the image, as is
 * conventional.
 *
 * Diagrammatically, if the width of dst clips src (o is byte origin of each
 * buffer).
 *
 *  o- -o- - - - - ----------------------
 *  :   :         |///////////////|      |
 *  :   : dst px  |///////////////|      |
 *  :   : offset  |///////////////|      |
 *  :------------>|/// blitted ///|      |
 *  :   :-------->|///////////////|      |
 *  :   :  src px |///////////////|      |
 *  :   :  offset |///////////////|   src|
 *  :   : - - - - |---------------|------
 *  :             |               |
 *  :             |               |
 *  :             |           dst |
 *   - - - - - - - ---------------|
 *
 * If the with of src is limiting:
 *
 *  o- -o- - - - - ----------------------------
 *  :   :         |//////////////////////|     |
 *  :   : dst px  |//////////////////////|     |
 *  :   : offset  |//////////////////////|     |
 *  :------------>|////// blitted ///////|     |
 *  :   :-------->|//////////////////////|     |
 *  :   :  src px |//////////////////////|     |
 *  :   :  offset |///////////////// src |     |
 *  :   : - - - - |----------------------      |
 *  :             |                            |
 *  :             |                            |
 *  :             |                         dst|
 *   - - - - - - - ----------------------------
 *
 *
 * Note the alignment due to the bit offsets: src.bit_offset is subtracted from
 * dst.bit_offset
 *
 * dst
 *  o----->|  dst.bit_offset
 *     |<--  -src.bit_offset
 *
 *  dst:
 *        byte0     :     byte1     : ... :    byteN
 *  lsb             :               :     :
 *   0 1 2 3 4 5 6 7:0 1 2 3 4 5 6 7: ... :0 1 2 3 4 5 6 7
 *   x x x a b c d e:f g h i j k l m: ... :w x y z - - - -
 *                  :               :     :
 *  src:            :               :     :
 *   X X X X A B C D:E F G H I K K L: ... :V W + + + + + +
 *                  :               :     :
 *  dst blit src:   :               :     :
 *   x x x A B C D E:F G H I J K L M: ... :W x y z - - - -
 *
 *                                      __ mask of least significant 3 bits
 *                                    /    (which are saved in dst)
 *                                   |
 *                                   |                        __ mask of most
 *                                   |                      /    significant 4 bits
 *                                   |                     |     (used bits in src)
 *                                   |                     |
 *                               /-------\            /--------\
 *      byte 0 : dst0 = (dst0 & ((1>>3)-1)) | ((src0 >> 1) | (src1 << 7 )) & ~((1>>3)-1))
 *      byte 1 : dst1 = (src1 >> 1) | (src2 << 7)
 *      byte 2 : dst2 = (src2 >> 1) | (src3 << 7)
 *            ...
 *      bits_left_mask = ((1 << (copy_bits_end % 8)) -1)
 *      byte N : (dstN = dstN & ~bits_left_mask)  | ((srcN >> 1) | (srcN+1 << 7)) & bits_left_mask
 *
 **/
void compBlit(struct CompBuf dst, struct CompBuf src, bool update_dst_mask)
{
    if (dst.bit_offset == 0 &&
        src.bit_offset == 0 &&
        min(dst.width_bits, src.width_bits) % 8 == 0
       ){
        // easy case: source and destination are both byte-aligned and the
        // length to copy is a whole number of bytes
        blitByteAligned(dst, src, update_dst_mask);

    } else if(dst.width_bits >= 24 &&
              src.width_bits >= 24) {
        // if we're blitting something wide enough that there are definitely
        // whole bytes to be blitted, then special case the start and end,

        // create a separate dst buffer for the unaligned start:
        struct CompBuf start_dst = dst;
        start_dst.width_bits = 8 - dst.bit_offset;

        // blit the unaligned start destination bits
        compDbgBlitReference(start_dst, src, update_dst_mask);

        // adjust dst/src to ignore the start
        src = compBufAtPxOffset(start_dst.width_bits, 0, src);
        dst = compBufAtPxOffset(start_dst.width_bits, 0, dst);

        // dst buffer for the aligned middle bit:
        struct CompBuf mid_dst = dst;
        mid_dst.width_bits = min(src.width_bits, dst.width_bits);
        mid_dst.width_bits -= mid_dst.width_bits % 8;

        // blit the dst-aligned middle
        blitDstByteAligned(mid_dst, src, update_dst_mask);

        // adjust dst/src to additionally ignore the middle
        src = compBufAtPxOffset(mid_dst.width_bits, 0, src);
        dst = compBufAtPxOffset(mid_dst.width_bits, 0, dst);

        // blit the end bit
        compDbgBlitReference(dst, src, update_dst_mask);
    } else {
        compDbgBlitReference(dst, src, update_dst_mask);
    }

    return;

    /*NSAssert(dst.bit_offset < 8 && src.bit_offset < 8, "");

    int32_t copy_bits = src.width_bits;
    int32_t dst_width = dst.width_bits + dst.bit_offset - src.bit_offset;
    if (dst_width < copy_bits)
        copy_bits = dst_width;

    int32_t bit_offset = dst.bit_offset - src.bit_offset;

    // bits into first byte of dst (this might be 8)
    const int32_t copy_bits_start = 8 - dst.bit_offset;
    // whole bytes into successive bytes of dst
    const int32_t copy_bits_wholebytes = (copy_bits - copy_bits_start) & ~7;
    // what remains
    const int32_t copy_bits_end = copy_bits - copy_bits_wholebytes - copy_bits_start;

    uint8_t const* srcrow = src.buf
    uint8_t*       dstrow = dst.buf

    if (bit_offset < 0){
        bit_offset += 8;
        srcrow += 1;
    }

    for (int32_t r = 0; r < copy_rows; r++) {
        const uint8_t d0 = dstrow[0];
        const uint8_t s0 = srcrow[0];
        const uint8_t s1 = srcrow[1];

        if (copy_bits_start >= copy_bits) {
            // only need one-sided mask
            dstrow[0] = (d0 &  ((1 >> dst.bit_offset) - 1)) |               // keep dst.bit_offset lsbits
                        (s0 & ~((1 >> src.bit_offset) - 1) >> bit_offset) | // masked bits from s0 msbits
                        (s1 & << (8 - bit_offset)) |                        // masked bits from s1 lsbits
        } else {
            // need to mask both sides...
            dstrow
        }

        for (int32_t b = 1; b < 1+copy_bits_wholebytes/8; b++) {
            dstrow[b] = (srcrow[b]   >> bit_offset) |
                        (srcrow[b+1] << (8-bit_offset));
        }

        if (copy_bits_end) {
        }

        srcrow += src.stride_bytes;
        dstrow += src.stride_bytes;
    }*/
}

void compBlitAt(struct CompBuf dst, struct CompBuf src, int16_t x, int16_t y, bool update_dst_mask){
    if (x < 0 && y < 0) {
        src = compBufAtPxOffset(-x, -y, src);
        x = 0;
        y = 0;
    } else if(x < 0) {
        src = compBufAtPxOffset(-x, 0, src);
        x = 0;
    } else if(y < 0) {
        src = compBufAtPxOffset(0, -y, src);
        y = 0;
    }
    compBlit(compBufAtPxOffset(x, y, dst), src, update_dst_mask);
}


/// - Display List Functions

void compDlFillRect(struct CompBuf dst, void* arg){
    struct CompDLFillRectArgs* a = (struct CompDLFillRectArgs*) arg;
    int16_t x0 = a->x0 <= a->x1? a->x0 : a->x1;
    int16_t x1 = a->x0 <= a->x1? a->x1 : a->x0;
    int16_t y0 = a->y0 <= a->y1? a->y0 : a->y1;
    int16_t y1 = a->y0 <= a->y1? a->y1 : a->y0;
    // completely clipped
    if (x1 < 0 || y1 < 0)
        return;
    // partially clipped
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    compFillRect(dst, x0, x1, y0, y1, a->fill);
}

void compDlBlit(struct CompBuf dst, void* arg){
    struct CompDLBlitArgs* a = (struct CompDLBlitArgs*) arg;
    compBlitAt(dst, a->src, a->x_off, a->y_off, true);
}

void compDlDrawDisplayList(struct CompBuf dst, void* arg){
    struct CompDisplayListElement* l = (struct CompDisplayListElement*) arg;
    while (l) {
        if(l->action.fn)
            l->action.fn(dst, l->action.arg);
        l = l->next;
    }
}

// fill the rectangle (x0,y0) (x1,y1) with `fill'
void compFillRect(struct CompBuf dst, int16_t x0s, int16_t _x1s, int16_t y0s, int16_t _y1s, bool fill)
{
    uint16_t  x0 = ( x0s < 0) ? 0 : x0s;
    uint16_t _x1 = (_x1s < 0) ? 0 : _x1s;
    uint16_t  y0 = ( y0s < 0) ? 0 : y0s;
    uint16_t _y1 = (_y1s < 0) ? 0 : _y1s;

    // clip the ends
    int32_t x1 = _x1 > dst.width_bits? dst.width_bits : _x1;
    int32_t y1 = _y1 > dst.height_strides? dst.height_strides : _y1;

    if (dst.bit_offset == 0 &&
        x0 % 8 == 0 &&
        (min(dst.width_bits, x1) - x0) % 32 == 0
       ){
        // simple word-aligned case
        uint32_t fill_word = fill? 0xffffffffu : 0;
        uint8_t* dst_buf_row = dst.buf + y0*dst.stride_bytes + (x0/8) % 4;
        const int32_t start_word = x0/32;
        const int32_t end_word   = x1/32;
        for (int32_t i = y0; i < y1; i++) {
            for (int32_t j = start_word; j < end_word; j++) {
                *(((uint32_t*)dst_buf_row) + j) = fill_word;
            }
            dst_buf_row  += dst.stride_bytes;
        }
    }else if (dst.bit_offset == 0 && x0 % 8 == 0 && x1 % 8 == 0) {
        // simple byte-aligned case
        uint8_t fill_byte = fill? 0xff : 0x00;
        uint8_t* dst_buf_row = dst.buf + y0*dst.stride_bytes;
        const int32_t start_byte = x0/8;
        const int32_t end_byte   = x1/8;
        for (int32_t i = y0; i < y1; i++) {
            for (int32_t j = start_byte; j < end_byte; j++) {
                *(dst_buf_row + j) = fill_byte;
            }
            dst_buf_row  += dst.stride_bytes;
        }
    } else {
        compDbgFillRect(dst, x0, _x1, y0, _y1, fill);
    }
}

// Invert the rectangle (x0,y0) (x1,y1)
void compInvertRect(struct CompBuf dst, int16_t x0s, int16_t _x1s, int16_t y0s, int16_t _y1s)
{
    uint16_t  x0 = ( x0s < 0) ? 0 : x0s;
    uint16_t _x1 = (_x1s < 0) ? 0 : _x1s;
    uint16_t  y0 = ( y0s < 0) ? 0 : y0s;
    uint16_t _y1 = (_y1s < 0) ? 0 : _y1s;

    // clip the ends
    int32_t x1 = _x1 > dst.width_bits? dst.width_bits : _x1;
    int32_t y1 = _y1 > dst.height_strides? dst.height_strides : _y1;

    if (dst.bit_offset == 0 &&
        x0 % 8 == 0 &&
        (min(dst.width_bits, x1) - x0) % 32 == 0
       ){
        // simple word-aligned case
        uint8_t* dst_buf_row = dst.buf + y0*dst.stride_bytes + (x0/8) % 4;
        const int32_t start_word = x0/32;
        const int32_t end_word   = x1/32;
        for (int32_t i = y0; i < y1; i++) {
            for (int32_t j = start_word; j < end_word; j++)
            {
                *(((uint32_t*)dst_buf_row) + j) = ~(*(((uint32_t*)dst_buf_row) + j));
            }
            dst_buf_row  += dst.stride_bytes;
        }
    }else if (dst.bit_offset == 0 && x0 % 8 == 0 && x1 % 8 == 0) {
        // simple byte-aligned case
        uint8_t* dst_buf_row = dst.buf + y0*dst.stride_bytes;
        const int32_t start_byte = x0/8;
        const int32_t end_byte   = x1/8;
        for (int32_t i = y0; i < y1; i++) {
            for (int32_t j = start_byte; j < end_byte; j++)
            {
                *(dst_buf_row + j) = ~(*(dst_buf_row + j));
            }
            dst_buf_row  += dst.stride_bytes;
        }
    } else {
        compDbgInvertRect(dst, x0, _x1, y0, _y1);
    }
}

void compRenderDL(struct CompBuf dst, struct CompDisplayListElement* l){
    //printf("%s %p into b:%p w:%d h:%d s:%d\n\r", __func__, l, dst.buf, (int32_t)dst.width_bits, (int32_t)dst.height_strides, (int32_t)dst.stride_bytes);
    while (l) {
        //printf("%s [%p,%p(%p)]->[%p]\n\r", __func__, l, l->action.fn, l->action.arg, l->next);
        if(l->action.fn)
            l->action.fn(dst, l->action.arg);
        l = l->next;
    }
}

static bool bitN(uint8_t x, int32_t n){
    return (x >> n) & 1;
}

static void setBitOfByte(uint8_t* xp, uint8_t n, bool val){
    *xp = (val << n) | (*xp & ~(((uint8_t) !val) << n));
}

static void setBitOfRow(uint8_t* row, uint16_t n, bool val){
    setBitOfByte(row + n/8, (uint8_t) (n & 7), val);
}

static bool bitOfRow(uint8_t const* row, uint16_t n){
    return bitN(*(row + n/8), (uint8_t) (n & 7));
}

/* Blits two CompBufs together using the top left corner as the anchor point */
/* and only over the region common to both.                                  */
/* For CompBufs residing in SRAM, bit access is done using the hardware bit  */
/* alias band found on the Cortex M3                                         */
void compDbgBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask)
{
    if ((src.buf >= (uint8_t*)SRAM_BASE) || (src.mask >= (uint8_t*)SRAM_BASE))
    {
        compDbgSRAMBlitReference(dst, src, update_dst_mask);
    }
    else
    {
        compDbgCodeBlitReference(dst, src, update_dst_mask);
    }
}

void compDbgSRAMBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask)
{
    /*  Setup 2-dimensional array access to the dst and src CompBufs
        by defining fixed height array pointers to the bit alias band
    */
    uint32_t (*dstBitBuf)[][dst.stride_bytes * 8] = (uint32_t (*)[][dst.stride_bytes * 8])(BITBAND_RAM_BASE + ((uint32_t)dst.buf - SRAM_BASE) * 32 + dst.bit_offset * 4);
    uint32_t (*dstBitMask)[][dst.stride_bytes * 8] = (uint32_t (*)[][dst.stride_bytes * 8])(BITBAND_RAM_BASE + ((uint32_t)dst.mask - SRAM_BASE) * 32 + dst.bit_offset * 4);

    uint32_t (*srcBitBuf)[][src.stride_bytes * 8] = (uint32_t (*)[][src.stride_bytes * 8])(BITBAND_RAM_BASE + ((uint32_t)src.buf - SRAM_BASE) * 32 + src.bit_offset * 4);
    uint32_t (*srcBitMask)[][src.stride_bytes * 8] = (uint32_t (*)[][src.stride_bytes * 8])(BITBAND_RAM_BASE + ((uint32_t)src.mask - SRAM_BASE) * 32 + src.bit_offset * 4);

    /*  Ensure widths and heights are valid.
        When called with negative values no blitting is performed.
        The region common to both buffers is used for blitting.
    */
//    uint32_t src_width_end = (src.width_bits < 0) ? 0 : src.width_bits;
//    uint32_t dst_width_end = (dst.width_bits < 0) ? 0 : dst.width_bits;
//    uint32_t src_height_end = (src.height_strides < 0) ? 0 : src.height_strides;
//    uint32_t dst_height_end = (dst.height_strides < 0) ? 0 : dst.height_strides;

    uint32_t src_width_end = src.width_bits;
    uint32_t dst_width_end = dst.width_bits;

    uint32_t src_height_end = src.height_strides;
    uint32_t dst_height_end = dst.height_strides;

    uint32_t height = min(dst_height_end, src_height_end);
    uint32_t width = min(dst_width_end, src_width_end);

    for (uint32_t row = 0; row < height; row++)
    {
        for (uint32_t col = 0; col < width; col++)
        {
            if ((int32_t)src.mask == Comp_Fill_Ones || (*srcBitMask)[row][col])
            {
                if (!compPtrIsMagic(dst.buf))
                {
                    if ((int32_t)src.buf == Comp_Fill_Zeros)
                    {
                        (*dstBitBuf)[row][col] = 0;
                    }
                    else if ((int32_t) src.buf == Comp_Fill_Ones)
                    {
                        (*dstBitBuf)[row][col] = 1;
                    }
                    else
                    {
                        (*dstBitBuf)[row][col] = (*srcBitBuf)[row][col];
                    }
                }

                if (update_dst_mask && !compPtrIsMagic(dst.mask))
                {
                    (*dstBitMask)[row][col] = 1;
                }
            }
        }
    }
}

void compDbgCodeBlitReference(struct CompBuf dst, struct CompBuf src, bool update_dst_mask){
    const int32_t srci_start = src.bit_offset;
//    const int32_t srci_end   = src.bit_offset + (src.width_bits < 0? 0 : src.width_bits);
    const int32_t srci_end   = src.bit_offset + src.width_bits;
    const int32_t dsti_start = dst.bit_offset;
//    const int32_t dsti_end   = dst.bit_offset + (dst.width_bits < 0? 0 : dst.width_bits);
    const int32_t dsti_end   = dst.bit_offset + dst.width_bits;
    uint8_t const* src_mask_row = src.mask;
    uint8_t const* src_buf_row  = src.buf;
    uint8_t*       dst_mask_row = dst.mask;
    uint8_t*       dst_buf_row  = dst.buf;
    for (int32_t i = 0; i < src.height_strides && i < dst.height_strides; i++) {
        for (int32_t srci = srci_start, dsti = dsti_start;
             srci < srci_end && dsti < dsti_end;
             srci++, dsti++) {
            if ((int32_t)src.mask == Comp_Fill_Ones || bitOfRow(src_mask_row, srci)) {

                if (!compPtrIsMagic(dst.buf)) {
                    if ((int32_t)src.buf == Comp_Fill_Zeros)
                        setBitOfRow(dst_buf_row, dsti, 0);
                    else if ((int32_t) src.buf == Comp_Fill_Ones)
                        setBitOfRow(dst_buf_row, dsti, 1);
                    else
                        setBitOfRow(dst_buf_row, dsti, bitOfRow(src_buf_row, srci));
                }
                if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                    setBitOfRow(dst_mask_row, dsti, 1);
                }
            }
        }
        if (!compPtrIsMagic(dst.buf))
            dst_buf_row  += dst.stride_bytes;
        if (!compPtrIsMagic(src.mask))
            src_mask_row += src.stride_bytes;
        if (!compPtrIsMagic(src.buf))
            src_buf_row  += src.stride_bytes;
        if (update_dst_mask && (!compPtrIsMagic(dst.mask)))
            dst_mask_row += dst.stride_bytes;
    }
}

/* Draw rectangle in CompBuf.
*/
void compDbgFillRect(struct CompBuf dst, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, bool fill)
{
    if (dst.buf >= (uint8_t*)SRAM_BASE)
    {
        /*  Setup 2-dimensional array access to the dst CompBufs
            by defining fixed height array pointers to the bit alias band
        */
        uint32_t (*dstBitBuf)[][dst.stride_bytes * 8] = (uint32_t (*)[][dst.stride_bytes * 8])(BITBAND_RAM_BASE + (uint32_t)(dst.buf - SRAM_BASE) * 32 + dst.bit_offset * 4);

        for (int32_t row = y0; (row < y1) && (row < dst.height_strides); row++)
        {
            for (int32_t col = x0; (col < x1) && (col < dst.width_bits); col++)
            {
                (*dstBitBuf)[row][col] = fill;
            }
        }
    }
    else
    {
        /*  Revert to non-bit-band version
        */
        const int32_t dsti_start = dst.bit_offset;
        const int32_t dsti_end   = dst.bit_offset + dst.width_bits;

        uint8_t* dst_buf_row  = dst.buf + y0*dst.stride_bytes;

        for (int32_t i = y0; i < dst.height_strides && i < y1; i++)
        {
            for (int32_t j = x0; j < x1 && j+dsti_start < dsti_end; j++)
            {
                setBitOfRow(dst_buf_row, j+dsti_start, fill);
            }

            dst_buf_row  += dst.stride_bytes;
        }
    }
}

/* Invert rectangle in CompBuf.
*/
void compDbgInvertRect(struct CompBuf dst, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1)
{
    if (dst.buf >= (uint8_t*)SRAM_BASE)
    {
        /*  Setup 2-dimensional array access to the dst CompBufs
            by defining fixed height array pointers to the bit alias band
        */
        uint32_t (*dstBitBuf)[][dst.stride_bytes * 8] = (uint32_t (*)[][dst.stride_bytes * 8])(BITBAND_RAM_BASE + (uint32_t)(dst.buf - SRAM_BASE) * 32 + dst.bit_offset * 4);

        for (int32_t row = y0; (row < y1) && (row < dst.height_strides); row++)
        {
            for (int32_t col = x0; (col < x1) && (col < dst.width_bits); col++)
            {
                (*dstBitBuf)[row][col] = ~((*dstBitBuf)[row][col]);
            }
        }
    }
    else
    {
        /*  Revert to non-bit-band version
        */
        const int32_t dsti_start = dst.bit_offset;
        const int32_t dsti_end   = dst.bit_offset + dst.width_bits;

        uint8_t* dst_buf_row  = dst.buf + y0*dst.stride_bytes;

        for (int32_t i = y0; i < dst.height_strides && i < y1; i++)
        {
            for (int32_t j = x0; j < x1 && j+dsti_start < dsti_end; j++)
            {
                setBitOfRow(dst_buf_row, j+dsti_start, !bitOfRow(dst_buf_row, j+dsti_start));
            }

            dst_buf_row  += dst.stride_bytes;
        }
    }
}


void compDbgPrint(struct CompBuf buf){
    int32_t i, j;

    printf("buf: bit_offset=%d stride=%d width_bits=%d height=%d buf=%p mask=%p\n\r", buf.bit_offset, buf.stride_bytes, buf.width_bits, buf.height_strides, buf.buf, buf.mask);
    // print two lines at a time using '|. to represent top, top and bottom,
    // and bottom bits set respectively
    for (i = 0; i < buf.height_strides-1; i+= 2) {
        if (!compPtrIsMagic(buf.buf)) {
            for (j = 0; j < buf.stride_bytes*8; j++) {
                if (j < buf.bit_offset) {
                    putchar('~');
                } else if (j < buf.bit_offset + buf.width_bits) {
                    int32_t this_row_bit = bitN(buf.buf[  i   * buf.stride_bytes + j/8], j&7);
                    int32_t next_row_bit = bitN(buf.buf[(i+1) * buf.stride_bytes + j/8], j&7);
                    if (this_row_bit && !next_row_bit)
                        putchar('\'');
                    else if (this_row_bit && next_row_bit)
                        putchar('|');
                    else if (next_row_bit)
                        putchar('.');
                    else
                        putchar(' ');
                } else {
                    putchar('~');
                }
            }
        }
        if (!compPtrIsMagic(buf.mask)) {
            putchar('$');
            for (j = 0; j < buf.stride_bytes*8; j++) {
                if (j < buf.bit_offset) {
                    putchar('~');
                } else if (j < buf.bit_offset + buf.width_bits) {
                    int32_t this_row_bit = bitN(buf.mask[  i   * buf.stride_bytes + j/8], j&7);
                    int32_t next_row_bit = bitN(buf.mask[(i+1) * buf.stride_bytes + j/8], j&7);
                    if (this_row_bit && !next_row_bit)
                        putchar('\'');
                    else if (this_row_bit && next_row_bit)
                        putchar('|');
                    else if (next_row_bit)
                        putchar('.');
                    else
                        putchar(' ');
                } else {
                    putchar('~');
                }
            }
        }
        putchar('\n');
        putchar('\r');
    }

    if (i == buf.height_strides-1) {
        if (!compPtrIsMagic(buf.buf)) {
            for (j = 0; j < buf.stride_bytes*8; j++) {
                if (j < buf.bit_offset) {
                    putchar('~');
                } else if (j < buf.bit_offset + buf.width_bits) {
                    int32_t this_row_bit = bitN(buf.buf[i * buf.stride_bytes + j/8], j&7);
                    if (this_row_bit)
                        putchar('\'');
                    else
                        putchar(' ');
                } else {
                    putchar('~');
                }
            }
        }
        if (!compPtrIsMagic(buf.mask)) {
            putchar('$');
            for (j = 0; j < buf.stride_bytes*8; j++) {
                if (j < buf.bit_offset) {
                    putchar('~');
                } else if (j < buf.bit_offset + buf.width_bits) {
                    int32_t this_row_bit = bitN(buf.mask[i * buf.stride_bytes + j/8], j&7);
                    if (this_row_bit)
                        putchar('\'');
                    else
                        putchar(' ');
                } else {
                    putchar('~');
                }
            }
        }

        putchar('\n');
        putchar('\r');
    }
}

struct CompBuf compDbgAllocCircle(int32_t diameter, uint8_t bit_offset, uint8_t fill){
    const int32_t stride = (diameter + bit_offset + 7) / 8;
    const int32_t height = diameter;

    struct CompBuf r = {
        .buf  = malloc(stride * height),
        .mask = malloc(stride * height),
        .bit_offset     = bit_offset,
        .stride_bytes   = stride,
        .width_bits     = diameter,
        .height_strides = diameter
    };

    memset(r.buf, fill, stride*height);
    memset(r.mask,   0, stride*height);

    int32_t centre_x = bit_offset + diameter / 2;
    int32_t centre_y = diameter / 2;
    int32_t r2_x2 = (diameter * diameter) / 2;

    for (int32_t i = 0; i < height; i++) {
        for (int32_t j = 0; j < stride*8; j++) {
            int32_t x = j - centre_x;
            int32_t y = i - centre_y;
            if (2*x*x + 2*y*y < r2_x2)
                setBitOfRow(r.mask + i * r.stride_bytes, j, 1);
        }
    }

//    printf("cicle d=%d o=%d f=%x\n\r", diameter, (int32_t)bit_offset, (int32_t)fill);
//    compDbgPrint(r);

    return r;
}

void compFillFun(struct CompBuf dst, bool(*userFillFunction)(uint16_t, uint16_t), int16_t xOffset, int16_t yOffset, bool fill)
{
    if (dst.buf >= (uint8_t*)SRAM_BASE)
    {
        /*  Setup 2-dimensional array access to the dst CompBufs
            by defining fixed height array pointers to the bit alias band
        */
        uint32_t (*dstBitBuf)[][dst.stride_bytes * 8] = (uint32_t (*)[][dst.stride_bytes * 8])(BITBAND_RAM_BASE + (uint32_t)(dst.buf - SRAM_BASE) * 32 + dst.bit_offset * 4);

        for (int32_t row = 0; row < dst.height_strides; row++)
        {
            for (int32_t col = 0; col < dst.width_bits; col++)
            {
                (*dstBitBuf)[row][col] = (userFillFunction(col - xOffset, row - yOffset)) ? fill : !fill;
            }
        }
    }
    else
    {
        /*  Revert to non-bit-band version
        */
        const int32_t dsti_start = dst.bit_offset;
        const int32_t dsti_end   = dst.bit_offset + dst.width_bits;

        uint8_t* dst_buf_row  = dst.buf;

        for (int32_t i = 0; i < dst.height_strides; i++)
        {
            for (int32_t j = 0; j+dsti_start < dsti_end; j++)
            {
                if (userFillFunction(j - xOffset, i - yOffset))
                {
                    setBitOfRow(dst_buf_row, j+dsti_start, fill);
                }
                else
                {
                    setBitOfRow(dst_buf_row, j+dsti_start, !fill);
                }
            }

            dst_buf_row  += dst.stride_bytes;
        }
    }
}




static void blitByteAligned(struct CompBuf dst, struct CompBuf src, bool update_dst_mask)
{
    int32_t width = min(dst.width_bits, src.width_bits);
    int32_t fastBytes =  width / 32;

    if (fastBytes > 0)
    {
        // blit as many word size bytes as possible
        blitByteAlignedVariable(dst, src, update_dst_mask, 32);

        if ((width % 32) > 0)
        {
            // adjust dst/src to ignore the start
            src = compBufAtPxOffset(fastBytes * 32, 0, src);
            dst = compBufAtPxOffset(fastBytes * 32, 0, dst);

            // blit the rest byte wise
            blitByteAlignedVariable(dst, src, update_dst_mask, 8);
        }
    }
    else
    {
        blitByteAlignedVariable(dst, src, update_dst_mask, 8);
    }


}

static void blitByteAlignedVariable(struct CompBuf dst, struct CompBuf src, bool update_dst_mask, word_size_t word_size)
{
//    const int32_t srci_end   = (src.width_bits < 0? 0 : src.width_bits) / word_size;
//    const int32_t dsti_end   = (dst.width_bits < 0? 0 : dst.width_bits) / word_size;
    const int32_t srci_end   = src.width_bits / word_size;
    const int32_t dsti_end   = dst.width_bits / word_size;
    const int32_t j_end = min(srci_end, dsti_end);
    const int32_t i_end = min(dst.height_strides, src.height_strides);
    uint8_t const* src_mask_row = src.mask;
    uint8_t const* src_buf_row  = src.buf;
    uint8_t*       dst_mask_row = dst.mask;
    uint8_t*       dst_buf_row  = dst.buf;
    const int32_t src_stride = src.stride_bytes;
    const int32_t dst_stride = dst.stride_bytes;

    if ((int32_t)src.mask == Comp_Fill_Zeros) {
        // nothing to do
        return;
    } else if((int32_t)src.mask == Comp_Fill_Ones) {
        // don't need to worry about mask, all ones
        for (int32_t i = 0; i < i_end; i++)
        {
            for (int32_t j = 0; j < j_end; j++)
            {
                if (word_size == 32)
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint32_t*)dst_buf_row)[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint32_t*)dst_buf_row)[j] = 0xFFFFFFFFU;
                        else
                            ((uint32_t*)dst_buf_row)[j] = ((uint32_t*)src_buf_row)[j];
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint32_t*)dst_mask_row)[j] = 0xFFFFFFFFU;
                    }
                }
                else if (word_size == 16)
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint16_t*)dst_buf_row)[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint16_t*)dst_buf_row)[j] = 0xFFFFU;
                        else
                            ((uint16_t*)dst_buf_row)[j] = ((uint16_t*)src_buf_row)[j];
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint16_t*)dst_mask_row)[j] = 0xFFFFU;
                    }
                }
                else
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            dst_buf_row[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            dst_buf_row[j] = 0xFFU;
                        else
                            dst_buf_row[j] = src_buf_row[j];
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        dst_mask_row[j] = 0xFFU;
                    }
                }
            }
            if (!compPtrIsMagic(dst.buf))
                dst_buf_row  += dst_stride;
            if (!compPtrIsMagic(src.buf))
                src_buf_row  += src_stride;
            if (update_dst_mask && (!compPtrIsMagic(dst.mask)))
                dst_mask_row += dst_stride;
        }
    } else {
        // src mask is normal pointer
        for (int32_t i = 0; i < i_end; i++)
        {
            for (int32_t j = 0; j < j_end; j++)
            {
                if (word_size == 32)
                {
                    const uint32_t mask = ((uint32_t*)src_mask_row)[j];

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint32_t*)dst_buf_row)[j] = ((uint32_t*)dst_buf_row)[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint32_t*)dst_buf_row)[j] = ((uint32_t*)dst_buf_row)[j] | mask;
                        else
                            ((uint32_t*)dst_buf_row)[j] = (((uint32_t*)dst_buf_row)[j] & ~mask)
                                                        | (((uint32_t*)src_buf_row)[j] & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint32_t*)dst_mask_row)[j] = ((uint32_t*)dst_mask_row)[j] | mask;
                    }
                }
                else if (word_size == 16)
                {
                    const uint16_t mask = ((uint16_t*)src_mask_row)[j];

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint16_t*)dst_buf_row)[j] = ((uint16_t*)dst_buf_row)[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint16_t*)dst_buf_row)[j] = ((uint16_t*)dst_buf_row)[j] | mask;
                        else
                            ((uint16_t*)dst_buf_row)[j] = (((uint16_t*)dst_buf_row)[j] & ~mask)
                                                        | (((uint16_t*)src_buf_row)[j] & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint16_t*)dst_mask_row)[j] = ((uint16_t*)dst_mask_row)[j] | mask;
                    }
                }
                else
                {
                    const uint8_t mask = src_mask_row[j];

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            dst_buf_row[j] = dst_buf_row[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            dst_buf_row[j] = dst_buf_row[j] | mask;
                        else
                            dst_buf_row[j] = (dst_buf_row[j] & ~mask) | (src_buf_row[j] & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        dst_mask_row[j] = dst_mask_row[j] | mask;
                    }
                }
            }
            if (!compPtrIsMagic(dst.buf))
                dst_buf_row  += dst_stride;
            src_mask_row += src_stride;
            if (!compPtrIsMagic(src.buf))
                src_buf_row  += src_stride;
            if (update_dst_mask && (!compPtrIsMagic(dst.mask)))
                dst_mask_row += dst_stride;
        }
    }
}

static void blitDstByteAligned(struct CompBuf dst, struct CompBuf src, bool update_dst_mask)
{
    int32_t width = min(dst.width_bits, src.width_bits);
    int32_t fastBytes =  width / 32;

    if (fastBytes > 0)
    {
        // blit whole words at the time
        blitDstByteAlignedVariable(dst, src, update_dst_mask, 32);

        if ((width % 32) > 0)
        {
            // adjust dst/src to ignore the start
            src = compBufAtPxOffset(fastBytes * 32, 0, src);
            dst = compBufAtPxOffset(fastBytes * 32, 0, dst);

            // blit the rest byte wise
            blitDstByteAlignedVariable(dst, src, update_dst_mask, 8);
        }
    }
    else
    {
        blitDstByteAlignedVariable(dst, src, update_dst_mask, 8);
    }
}

static void blitDstByteAlignedVariable(struct CompBuf dst, struct CompBuf src, bool update_dst_mask, word_size_t word_size)
{
//    const int32_t srci_end   = (src.width_bits < 0? 0 : src.width_bits) / word_size;
//    const int32_t dsti_end   = (dst.width_bits < 0? 0 : dst.width_bits) / word_size;
    const int32_t srci_end = src.width_bits / word_size;
    const int32_t dsti_end = dst.width_bits / word_size;
    const int32_t j_end = min(srci_end, dsti_end);
    const int32_t i_end = min(dst.height_strides, src.height_strides);
    uint8_t const* src_mask_row = src.mask;
    uint8_t const* src_buf_row  = src.buf;
    uint8_t*       dst_mask_row = dst.mask;
    uint8_t*       dst_buf_row  = dst.buf;
    const int32_t src_stride = src.stride_bytes;
    const int32_t dst_stride = dst.stride_bytes;
    const int32_t sbo = src.bit_offset;

    if ((int32_t)src.mask == Comp_Fill_Zeros) {
        // nothing to do
        return;
    } else if((int32_t)src.mask == Comp_Fill_Ones) {
        // don't need to worry about mask, all ones
        for (int32_t i = 0; i < i_end; i++)
        {
            for (int32_t j = 0; j < j_end; j++)
            {
                if (word_size == 32)
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint32_t*)dst_buf_row)[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint32_t*)dst_buf_row)[j] = 0xFFFFFFFFU;
                        else
                            ((uint32_t*)dst_buf_row)[j] = (((uint32_t*)src_buf_row)[j] >> sbo)
                                                        | (((uint32_t*)src_buf_row)[j+1] << (32-sbo));
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask))
                    {
                        ((uint32_t*)dst_mask_row)[j] = 0xFFFFFFFFU;
                    }
                }
                if (word_size == 16)
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint16_t*)dst_buf_row)[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint16_t*)dst_buf_row)[j] = 0xFFFFU;
                        else
                            ((uint16_t*)dst_buf_row)[j] = (((uint16_t*)src_buf_row)[j] >> sbo)
                                                        | (((uint16_t*)src_buf_row)[j+1] << (16-sbo));
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask))
                    {
                        ((uint16_t*)dst_mask_row)[j] = 0xFFFFU;
                    }
                }
                else
                {
                    if (!compPtrIsMagic(dst.buf))
                    {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            dst_buf_row[j] = 0;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            dst_buf_row[j] = 0xFFU;
                        else
                            dst_buf_row[j] = (src_buf_row[j] >> sbo) | (src_buf_row[j+1] << (8-sbo));
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask))
                    {
                        dst_mask_row[j] = 0xFFU;
                    }
                }
            }
            if (!compPtrIsMagic(dst.buf))
                dst_buf_row  += dst_stride;
            if (!compPtrIsMagic(src.buf))
                src_buf_row  += src_stride;
            if (update_dst_mask && (!compPtrIsMagic(dst.mask)))
                dst_mask_row += dst_stride;
        }
    } else {
        // src mask is normal pointer
        for (int32_t i = 0; i < i_end; i++) {
            for (int32_t j = 0; j < j_end; j++)
            {
                if (word_size == 32)
                {
                    const uint32_t mask = (((uint32_t*)src_mask_row)[j] >> sbo)
                                        | (((uint32_t*)src_mask_row)[j+1] << (32-sbo));

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint32_t*)dst_buf_row)[j] = ((uint32_t*)dst_buf_row)[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint32_t*)dst_buf_row)[j] = ((uint32_t*)dst_buf_row)[j] | mask;
                        else
                            ((uint32_t*)dst_buf_row)[j] = (((uint32_t*)dst_buf_row)[j] & ~mask)
                            | (((((uint32_t*)src_buf_row)[j] >> sbo) | (((uint32_t*)src_buf_row)[j+1] << (32-sbo))) & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint32_t*)dst_mask_row)[j] = ((uint32_t*)dst_mask_row)[j] | mask;
                    }
                }
                else if (word_size == 16)
                {
                    const uint16_t mask = (((uint16_t*)src_mask_row)[j] >> sbo)
                                        | (((uint16_t*)src_mask_row)[j+1] << (16-sbo));

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            ((uint16_t*)dst_buf_row)[j] = ((uint16_t*)dst_buf_row)[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            ((uint16_t*)dst_buf_row)[j] = ((uint16_t*)dst_buf_row)[j] | mask;
                        else
                            ((uint16_t*)dst_buf_row)[j] = (((uint16_t*)dst_buf_row)[j] & ~mask)
                            | (((((uint16_t*)src_buf_row)[j] >> sbo) | (((uint16_t*)src_buf_row)[j+1] << (16-sbo))) & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        ((uint16_t*)dst_mask_row)[j] = ((uint16_t*)dst_mask_row)[j] | mask;
                    }
                }
                else
                {
                    const uint8_t mask = (src_mask_row[j] >> sbo) | (src_mask_row[j+1] << (8-sbo));

                    if (!compPtrIsMagic(dst.buf)) {
                        if ((int32_t)src.buf == Comp_Fill_Zeros)
                            dst_buf_row[j] = dst_buf_row[j] & ~mask;
                        else if ((int32_t) src.buf == Comp_Fill_Ones)
                            dst_buf_row[j] = dst_buf_row[j] | mask;
                        else
                            dst_buf_row[j] = (dst_buf_row[j] & ~mask) |
                                             (((src_buf_row[j] >> sbo) | (src_buf_row[j+1] << (8-sbo))) & mask);
                    }
                    if (update_dst_mask && !compPtrIsMagic(dst.mask)) {
                        dst_mask_row[j] = dst_mask_row[j] | mask;
                    }
                }
            }
            if (!compPtrIsMagic(dst.buf))
                dst_buf_row  += dst_stride;
            src_mask_row += src_stride;
            if (!compPtrIsMagic(src.buf))
                src_buf_row  += src_stride;
            if (update_dst_mask && (!compPtrIsMagic(dst.mask)))
                dst_mask_row += dst_stride;
        }
    }
}
