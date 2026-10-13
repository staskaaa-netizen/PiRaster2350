#ifndef LVDS_PALETTE_H
#define LVDS_PALETTE_H

#include "lvds_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The colours a drawing may ask for by name.

   **The table is full.** The HSTX output is paletted and the framebuffer is
   4bpp, so there are sixteen colours and no more: a new colour means retiring
   one. The names are the machine's own - its stock is `WHITE`, its ink is
   `WHITE_WARM`, the fault block is `WHITE` on `RED` - and the hex beside each
   is what the panel is asked for.

   A colour does not take a palette slot here. `lvds_palette_color()` asks the
   target for one, and the target hands them out on first use (see
   `lvds_target.h`), so a picture that brings colours of its own finds the
   palette empty as long as it says so with `lvds_canvas_palette_reset()`.

   A name can also be **redefined while the picture runs**
   (`lvds_palette_define()`): the slot it already holds is re-coloured in
   place, so on a scanout target everything drawn in that colour changes on the
   next line without anything being redrawn. That is the dynamic palette
   switch, and it costs no slots and no repaint. */
typedef enum {
    LVDS_COL_GRAY_192 = 0,
    LVDS_COL_GRAY_128,
    LVDS_COL_GRAY_160,
    LVDS_COL_GRAY_96,
    LVDS_COL_BLACK,
    LVDS_COL_WHITE_WARM,
    LVDS_COL_YELLOW,
    LVDS_COL_YELLOW_LIGHT,
    LVDS_COL_RED,
    LVDS_COL_GREEN,
    LVDS_COL_WHITE,
    LVDS_COL_BLUE,
    LVDS_COL_TOOL_ORANGE,
    LVDS_COL_RED_BRIGHT,
    LVDS_COL_GREEN_BRIGHT,
    LVDS_COL_TOOL_TIP,
    LVDS_COL_COUNT
} lvds_palette_color_id_t;

void lvds_palette_init(void);
/* Forget which colours have been asked for. Called by
   `lvds_canvas_palette_reset()`, which also resets the target. */
void lvds_palette_reset(void);

lvds_color_t lvds_palette_color(lvds_palette_color_id_t id);
const char *lvds_palette_color_name(lvds_palette_color_id_t id);

/* What a name means from now on, 5-6-5. The slot a name already holds is
   re-coloured the next time the colour is asked for. */
void lvds_palette_define(lvds_palette_color_id_t id, uint16_t rgb565);
/* Put every name back to the built-in table. */
void lvds_palette_defaults(void);
/* What a name means now - either what the table says, or the last
   `lvds_palette_define()` for it. */
uint16_t lvds_palette_value(lvds_palette_color_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* LVDS_PALETTE_H */
