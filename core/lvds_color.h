#ifndef LVDS_COLOR_H
#define LVDS_COLOR_H

#include <stdint.h>

/* A colour as the renderer carries it: a 5-6-5 word, or - once it has been
   handed to a target - the index of the slot that word took. The two are the
   same type on purpose: a target that has no palette (the host one) passes the
   5-6-5 word straight through, an indexed target (the RP2350) passes the index
   it gave out.

   The framebuffer is 4bpp indexed, so on the glass there are sixteen colours
   and no blending: a colour that is not in the palette is replaced by the
   nearest one that is. */
typedef uint16_t lvds_color_t;

static inline uint16_t lvds_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((uint16_t)r & 0xf8u) << 8) |
                      (((uint16_t)g & 0xfcu) << 3) |
                      (((uint16_t)b & 0xf8u) >> 3));
}

/* 5-6-5 back to 0xRRGGBB, for a target that keeps a wider frame than the glass
   does (the host one) or has to build a table of wider words. */
static inline uint32_t lvds_rgb888(uint16_t rgb565)
{
    uint32_t r5 = (uint32_t)((rgb565 >> 11) & 0x1fu);
    uint32_t g6 = (uint32_t)((rgb565 >> 5) & 0x3fu);
    uint32_t b5 = (uint32_t)(rgb565 & 0x1fu);

    return (((r5 << 3) | (r5 >> 2)) << 16) |
           (((g6 << 2) | (g6 >> 4)) << 8) |
           ((b5 << 3) | (b5 >> 2));
}

#endif /* LVDS_COLOR_H */
