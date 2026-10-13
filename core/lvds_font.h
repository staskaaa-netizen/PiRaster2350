#ifndef LVDS_FONT_H
#define LVDS_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The machine's two bitmap faces, one byte a row and 256 rows each: the
   condensed 6x8 (`lvds_font_6x8.c`) and the 8x14 (`lvds_font_8x14.c`). They are
   generated files beside the fonts they came from, and `lvds_draw_text()`
   draws them as filled rectangles - the glyphs never touch a pixel. */

extern const uint8_t FontCond6x8[256 * 8];
extern const uint8_t FontIbm8x14[256 * 14];

#define LVDS_FONT_COND_6X8 FontCond6x8
#define LVDS_FONT_IBM_8X14 FontIbm8x14

#ifdef __cplusplus
}
#endif

#endif /* LVDS_FONT_H */
