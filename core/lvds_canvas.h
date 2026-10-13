#ifndef LVDS_CANVAS_H
#define LVDS_CANVAS_H

#include <stdbool.h>

#include "lvds_color.h"
#include "lvds_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The surface a drawing is drawn on.

   Everything here is in **view** coordinates - the picture the operator reads,
   600x800 while the panel is turned - and the core maps it onto the glass and
   clips it. A caller never sees a panel coordinate, and a target never sees a
   view coordinate.

   Only two calls write a pixel: `lvds_canvas_pixel` and
   `lvds_canvas_fill_rect`. Lines, rectangles, ellipses and text are built out
   of those two (`lvds_draw.h`), which is why the turn, the clipping and the
   bounds cost nothing anywhere else.

   Out-of-bounds coordinates are dropped, not wrapped: a caller that draws off
   the screen gets nothing on the screen, never a wrapped pixel somewhere else.
   The guard is deliberately loose (`LVDS_CANVAS_COORD_GUARD`), so a drawing
   that is far off still costs nothing more than a comparison. */

#define LVDS_CANVAS_WIDTH LVDS_VIEW_WIDTH
#define LVDS_CANVAS_HEIGHT LVDS_VIEW_HEIGHT

/* Bring the target up (the panel, the framebuffer, the scanout). False if the
   target says it could not - no clock lock, no memory, whatever its reasons. */
bool lvds_canvas_init(void);
bool lvds_canvas_ready(void);

int lvds_canvas_width(void);
int lvds_canvas_height(void);

void lvds_canvas_clear(lvds_color_t color);
void lvds_canvas_pixel(int x, int y, lvds_color_t color);
void lvds_canvas_fill_rect(int x, int y, int w, int h, lvds_color_t color);

/* Hand the picture to the target: on the machine this is the copy from the
   draw buffer into the scanout framebuffer, and it is the caller that decides
   when a frame is finished. */
void lvds_canvas_present(void);

/* Give the palette back to the panel's own colours (see `lvds_target.h`). */
void lvds_canvas_palette_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* LVDS_CANVAS_H */
