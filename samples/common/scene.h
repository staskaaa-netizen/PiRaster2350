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

/* Where the scene puts the things a check wants to find, in view coordinates. */
#define SCENE_BORDER_COLOR LVDS_COL_GRAY_192
#define SCENE_BACKGROUND_COLOR LVDS_COL_BLACK
#define SCENE_SWATCH_X 24
#define SCENE_SWATCH_Y 560
#define SCENE_MOVING_BAR_X 40
#define SCENE_MOVING_BAR_Y 420

#endif /* SCENE_H */
