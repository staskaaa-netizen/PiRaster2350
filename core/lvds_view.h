#ifndef LVDS_VIEW_H
#define LVDS_VIEW_H

#include "lvds_config.h"

#ifndef LVDS_HEADLESS_LOWRES
#define LVDS_HEADLESS_LOWRES 0
#endif

/* Where a *drawing* puts its pixels, and how that lands on the glass.

   The panel is 800x600, and the machine is built with it turned a quarter: the
   operator reads a 600x800 picture. The turn is the renderer's business - it
   owns the pixels - so everything above this file draws in the view's
   coordinates and never sees a panel coordinate:

     - `LVDS_VIEW_WIDTH`/`HEIGHT` is what a drawing is given (600x800 while the
       panel is turned, the panel's own size when it is not);
     - `LVDS_PANEL_X/Y` map one view point onto the glass, `LVDS_PANEL_RECT_*`
       one view rectangle (a quarter turn swaps width and height);
     - `LVDS_PANEL_TURN` says which way the glass is bolted, and it is stated
       once, in the panel header.

   A picture that comes out upside down is this one number set to the wrong
   quarter. */

#if LVDS_HEADLESS_LOWRES
#define LVDS_VIEW_WIDTH 200
#define LVDS_VIEW_HEIGHT 120
#define LVDS_PANEL_X(vx, vy) (vx)
#define LVDS_PANEL_Y(vx, vy) (vy)
#define LVDS_PANEL_RECT_X(vx, vy, vw, vh) (vx)
#define LVDS_PANEL_RECT_Y(vx, vy, vw, vh) (vy)
#define LVDS_PANEL_RECT_W(vw, vh) (vw)
#define LVDS_PANEL_RECT_H(vw, vh) (vh)
#elif LVDS_PANEL_TURN == 90
#define LVDS_VIEW_WIDTH LVDS_HEIGHT
#define LVDS_VIEW_HEIGHT LVDS_WIDTH
#define LVDS_PANEL_X(vx, vy) (vy)
#define LVDS_PANEL_Y(vx, vy) (LVDS_HEIGHT - 1 - (vx))
#define LVDS_PANEL_RECT_X(vx, vy, vw, vh) (vy)
#define LVDS_PANEL_RECT_Y(vx, vy, vw, vh) (LVDS_HEIGHT - ((vx) + (vw)))
#define LVDS_PANEL_RECT_W(vw, vh) (vh)
#define LVDS_PANEL_RECT_H(vw, vh) (vw)
#elif LVDS_PANEL_TURN == 270
#define LVDS_VIEW_WIDTH LVDS_HEIGHT
#define LVDS_VIEW_HEIGHT LVDS_WIDTH
#define LVDS_PANEL_X(vx, vy) (LVDS_WIDTH - 1 - (vy))
#define LVDS_PANEL_Y(vx, vy) (vx)
#define LVDS_PANEL_RECT_X(vx, vy, vw, vh) (LVDS_WIDTH - ((vy) + (vh)))
#define LVDS_PANEL_RECT_Y(vx, vy, vw, vh) (vx)
#define LVDS_PANEL_RECT_W(vw, vh) (vh)
#define LVDS_PANEL_RECT_H(vw, vh) (vw)
#else
#define LVDS_VIEW_WIDTH LVDS_WIDTH
#define LVDS_VIEW_HEIGHT LVDS_HEIGHT
#define LVDS_PANEL_X(vx, vy) (vx)
#define LVDS_PANEL_Y(vx, vy) (vy)
#define LVDS_PANEL_RECT_X(vx, vy, vw, vh) (vx)
#define LVDS_PANEL_RECT_Y(vx, vy, vw, vh) (vy)
#define LVDS_PANEL_RECT_W(vw, vh) (vw)
#define LVDS_PANEL_RECT_H(vw, vh) (vh)
#endif

#endif /* LVDS_VIEW_H */
