#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>

#include "lvds_palette.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One picture, drawn by every sample.

   This is where a reader should start: it uses nothing but the renderer's own
   API - colours by name, text, lines, rectangles, ellipses - and it is the
   same code on the desk and on the glass.

   Everything that moves is a function of `now_ms` and of nothing else, so the
   picture is the same on every build and as fluid as the loop can draw it: a
   scene paced by a frame counter runs at a different speed on every machine,
   and one stepped eight times a second is a slideshow. */

void scene_draw(uint32_t now_ms);

/* Where swatch `index` (0..15) is drawn, in view coordinates. The layout
   follows the view, so the same scene sits well on an upright panel and on a
   turned one; a check asks for the same rectangle the scene drew. */
void scene_swatch_rect(int index, int *x, int *y, int *w, int *h);

/* Where the disc is at `now_ms`, and how big it is. */
void scene_disc_rect(uint32_t now_ms, int *x, int *y, int *r);

/* The palette bank a moment belongs to, and the switching of it. Phase 0 is
   the panel's own colours, and the scene returns to them; the other phases are
   banks of their own. A switch is cheap and immediate: the slots are
   re-coloured in place, so on the glass the picture changes without being
   redrawn. The scene's own text says which bank is up, so a caller that
   switches on this period and a reader of the picture agree. */
#define SCENE_PALETTE_PHASES 4
#define SCENE_PALETTE_MS 3000
int scene_palette_phase(uint32_t now_ms);
void scene_palette_switch(int phase);

/* The colours a check wants to find. */
#define SCENE_BORDER_COLOR LVDS_COL_GRAY_192
#define SCENE_BACKGROUND_COLOR LVDS_COL_BLACK

#ifdef __cplusplus
}
#endif

#endif /* SCENE_H */
