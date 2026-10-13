#ifndef LVDS_DRAW_H
#define LVDS_DRAW_H

#include "lvds_canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The primitives every screen is written in, built out of the canvas's two
   writing calls. None of them touches a pixel itself, which is why they turn
   with the panel and clip to the screen for free.

   Coordinates are the view's, and a primitive whose whole extent misses the
   view costs one comparison and returns. Text is filled rectangles - a glyph
   is a bitmap drawn as blocks - so a scaled font is the same code with a
   bigger block. */

enum {
    LVDS_FONT_SMALL = 0,   /* 6x8, the machine's condensed face */
    LVDS_FONT_NORMAL = 1,  /* 8x14 */
    LVDS_FONT_LARGE = 2    /* 8x14, doubled */
};

void lvds_draw_line(int x1, int y1, int x2, int y2, lvds_color_t color);
void lvds_draw_line_w(int x1, int y1, int x2, int y2, lvds_color_t color,
                      int width);
void lvds_draw_rect(int x, int y, int w, int h, lvds_color_t color);
void lvds_draw_ellipse(int x, int y, int rx, int ry, lvds_color_t color);
void lvds_draw_fill_ellipse(int x, int y, int rx, int ry, lvds_color_t color);

void lvds_draw_text(int x, int y, const char *text, lvds_color_t fg,
                    lvds_color_t bg, int font);
int lvds_draw_text_width(const char *text, int font);
/* The same text in a fixed number of columns: shorter text is padded, longer
   text ends in `...`. This is how the machine's rows and message lines line
   up without the caller measuring anything. */
void lvds_draw_text_clip(int x, int y, const char *text, int cols,
                         lvds_color_t fg, lvds_color_t bg, int font);

#ifdef __cplusplus
}
#endif

#endif /* LVDS_DRAW_H */
