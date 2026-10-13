#ifndef SCENE_H
#define SCENE_H

#include "lvds_palette.h"

/* One picture, drawn by every sample.

   This is where a reader should start: it uses nothing but the renderer's own
   API - colours by name, text, lines, rectangles, ellipses - and it is the
   same code on the desk and on the glass. `frame` only moves the things that
   move, so a caller in a loop gets a picture that changes without the code
   growing a state machine. */

void scene_draw(int frame);

/* Where swatch `index` (0..15) is drawn, in view coordinates. The layout
   follows the view, so the same scene sits well on an upright panel and on a
   turned one; a check asks for the same rectangle the scene drew. */
void scene_swatch_rect(int index, int *x, int *y, int *w, int *h);

/* Switch the palette the scene draws with. Phase 0 is the panel's own colours,
   and the scene returns to them; the other phases are banks of their own.
   Cheap and immediate: the slots are re-coloured in place, so on the glass the
   picture changes without being redrawn. */
void scene_palette_switch(int phase);
#define SCENE_PALETTE_PHASES 4

/* Where the scene puts the things a check wants to find, in view coordinates. */
#define SCENE_BORDER_COLOR LVDS_COL_GRAY_192
#define SCENE_BACKGROUND_COLOR LVDS_COL_BLACK
#define SCENE_MOVING_BAR_X 40
#define SCENE_MOVING_BAR_Y 380

#endif /* SCENE_H */
