#include "scene.h"

#include "lvds_renderer.h"

/* The list the swatches are drawn from, in palette order. */
static const lvds_palette_color_id_t g_swatch[] = {
    LVDS_COL_GRAY_192, LVDS_COL_GRAY_128, LVDS_COL_GRAY_160,
    LVDS_COL_GRAY_96, LVDS_COL_BLACK, LVDS_COL_WHITE_WARM,
    LVDS_COL_YELLOW, LVDS_COL_YELLOW_LIGHT, LVDS_COL_RED,
    LVDS_COL_GREEN, LVDS_COL_WHITE, LVDS_COL_BLUE,
    LVDS_COL_TOOL_ORANGE, LVDS_COL_RED_BRIGHT, LVDS_COL_GREEN_BRIGHT,
    LVDS_COL_TOOL_TIP
};

void scene_draw(int frame)
{
    int w = lvds_canvas_width();
    int h = lvds_canvas_height();
    int bar_x = SCENE_MOVING_BAR_X + (frame % 8) * 32;
    lvds_color_t ink = lvds_palette_color(LVDS_COL_WHITE_WARM);
    lvds_color_t paper = lvds_palette_color(SCENE_BACKGROUND_COLOR);

    lvds_canvas_clear(paper);

    /* The frame's own edge, one pixel wide, so a picture that has been turned
       the wrong way cannot be mistaken for one that has not. */
    lvds_draw_rect(0, 0, w, h, lvds_palette_color(SCENE_BORDER_COLOR));

    lvds_draw_text(16, 16, "lvds-renderer", ink, paper, LVDS_FONT_NORMAL);
    lvds_draw_text(16, 36, "one linear while loop, no RTOS", ink, paper,
                   LVDS_FONT_SMALL);
    lvds_draw_text(16, 52, "frame", ink, paper, LVDS_FONT_SMALL);

    /* Something that moves with the frame number, and something that only
       turns: a filled disc and a ring, drawn from the same two calls. */
    lvds_draw_fill_ellipse(bar_x, SCENE_MOVING_BAR_Y, 28, 28,
                           lvds_palette_color(LVDS_COL_GREEN));
    lvds_draw_ellipse(w / 2, 260, 120, 80, ink);
    lvds_draw_line(24, 380, w - 24, 380, lvds_palette_color(LVDS_COL_RED));

    /* The palette, on the screen, one swatch each. */
    lvds_draw_text(16, SCENE_SWATCH_Y - 22, "the sixteen, in order", ink,
                   paper, LVDS_FONT_SMALL);
    for (unsigned i = 0; i < sizeof(g_swatch) / sizeof(g_swatch[0]); i++) {
        lvds_canvas_fill_rect(SCENE_SWATCH_X + (int)i * 32, SCENE_SWATCH_Y, 26,
                              26, lvds_palette_color(g_swatch[i]));
    }
}
