#ifndef __BREAKOUT_RESOURECE_FONT_1_H__
#define __BREAKOUT_RESOURECE_FONT_1_H__

#include "uif-tools-1bit/font.h"

const uint8_t Font_1_Data[] = {
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff
};

const struct FontGlyphData Font_1_Block_1_Ch_Arr[] = {
    {0, 0, 10, 10, 1, 12},
    {0, 0, 10, 20, 1, 12},
    {0, 0, 10, 10, 1, 12}
};

const struct FontGlyphDataBlock Font_1_Block_Arr[] = {
    { Font_1_Block_1_Ch_Arr, 3, 'a'}
};

const struct FontData Font_1 = {
    Font_1_Block_Arr,
    Font_1_Data,
    2, // data stride
    1, // num charblocks
    1, // space x
    1, // space y
//
    0,
    0,
    0
};

#endif // ndef __BREAKOUT_RESOURECE_FONT_1_H__
