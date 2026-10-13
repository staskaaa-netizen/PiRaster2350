#include "scene.h"

#include <stddef.h>

#include "lvds_renderer.h"

/* The colours the swatches are drawn from, in palette order. */
static const lvds_palette_color_id_t g_swatch[] = {
    LVDS_COL_GRAY_192, LVDS_COL_GRAY_128, LVDS_COL_GRAY_160,
    LVDS_COL_GRAY_96, LVDS_COL_BLACK, LVDS_COL_WHITE_WARM,
    LVDS_COL_YELLOW, LVDS_COL_YELLOW_LIGHT, LVDS_COL_RED,
    LVDS_COL_GREEN, LVDS_COL_WHITE, LVDS_COL_BLUE,
    LVDS_COL_TOOL_ORANGE, LVDS_COL_RED_BRIGHT, LVDS_COL_GREEN_BRIGHT,
    LVDS_COL_TOOL_TIP
};

/* The colours a switch redefines, and four banks for them: phase 0 is the
   panel's own values, so the demo always has a way home. */
static const lvds_palette_color_id_t g_switch_ids[] = {
    LVDS_COL_RED, LVDS_COL_GREEN, LVDS_COL_YELLOW, LVDS_COL_YELLOW_LIGHT,
    LVDS_COL_BLUE, LVDS_COL_TOOL_ORANGE, LVDS_COL_RED_BRIGHT,
    LVDS_COL_GREEN_BRIGHT
};
static const uint8_t g_banks[SCENE_PALETTE_PHASES][8][3] = {
    /* 0: the panel's own */
    {{0xE6, 0x00, 0x00}, {0x24, 0xC8, 0x63}, {0xFF, 0xF7, 0x01}, {0xFF, 0xF5, 0x82},
     {0x30, 0x50, 0xC0}, {0xEA, 0xFA, 0x41}, {0xFF, 0x00, 0x00}, {0x3E, 0xFF, 0x24}},
    /* 1: cool */
    {{0x00, 0xC0, 0xC0}, {0x18, 0x60, 0xE0}, {0x80, 0xE0, 0xFF}, {0xC0, 0xF0, 0xFF},
     {0x30, 0x50, 0xC0}, {0x00, 0xA0, 0xFF}, {0x60, 0x80, 0xFF}, {0x00, 0xFF, 0xC0}},
    /* 2: warm */
    {{0xFF, 0x40, 0x00}, {0xFF, 0xB0, 0x00}, {0xFF, 0xE0, 0x40}, {0xFF, 0xC0, 0x80},
     {0xC0, 0x30, 0x00}, {0xFF, 0x80, 0x00}, {0xFF, 0x20, 0x20}, {0xFF, 0xD0, 0x20}},
    /* 3: green and grey */
    {{0x40, 0x80, 0x40}, {0x80, 0xC0, 0x80}, {0xC0, 0xE0, 0x80}, {0xE0, 0xF0, 0xC0},
     {0x60, 0x80, 0x60}, {0xA0, 0xC0, 0x60}, {0x80, 0xA0, 0x80}, {0xC0, 0xFF, 0xC0}}
};

#define SCENE_SWATCH_W 60
#define SCENE_SWATCH_H 60
#define SCENE_SWATCH_GAP 8
#define SCENE_SWATCH_COLUMNS 8

void scene_swatch_rect(int index, int *x, int *y, int *w, int *h)
{
    int pitch = SCENE_SWATCH_W + SCENE_SWATCH_GAP;
    int rows = (int)(sizeof(g_swatch) / sizeof(g_swatch[0])) / SCENE_SWATCH_COLUMNS;
    int total = SCENE_SWATCH_COLUMNS * pitch - SCENE_SWATCH_GAP;
    int x0 = (lvds_canvas_width() - total) / 2;
    int y0 = lvds_canvas_height() - rows * (SCENE_SWATCH_H + SCENE_SWATCH_GAP) - 24;

    if (index < 0) {
        index = 0;
    }
    if (x) *x = x0 + (index % SCENE_SWATCH_COLUMNS) * pitch;
    if (y) *y = y0 + (index / SCENE_SWATCH_COLUMNS) * (SCENE_SWATCH_H + SCENE_SWATCH_GAP);
    if (w) *w = SCENE_SWATCH_W;
    if (h) *h = SCENE_SWATCH_H;
}

void scene_palette_switch(int phase)
{
    int p = ((phase % SCENE_PALETTE_PHASES) + SCENE_PALETTE_PHASES) % SCENE_PALETTE_PHASES;

    for (unsigned i = 0; i < sizeof(g_switch_ids) / sizeof(g_switch_ids[0]); i++) {
        lvds_palette_define(g_switch_ids[i],
                            lvds_rgb565(g_banks[p][i][0], g_banks[p][i][1],
                                        g_banks[p][i][2]));
    }
}

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
    lvds_draw_text(16, 52, "palette phase", ink, paper, LVDS_FONT_SMALL);

    /* Something that moves with the frame number, and something that only
       turns: a filled disc and a ring, drawn from the same two calls. */
    lvds_draw_fill_ellipse(bar_x, SCENE_MOVING_BAR_Y, 28, 28,
                           lvds_palette_color(LVDS_COL_GREEN));
    lvds_draw_ellipse(w / 2, 200, 140, 90, ink);
    lvds_draw_line(24, 320, w - 24, 320, lvds_palette_color(LVDS_COL_RED));

    /* The palette, on the screen, one swatch each. */
    {
        int x0, y0;
        scene_swatch_rect(0, &x0, &y0, NULL, NULL);
        lvds_draw_text(x0, y0 - 22, "the sixteen, in order", ink, paper,
                       LVDS_FONT_SMALL);
    }
    for (unsigned i = 0; i < sizeof(g_swatch) / sizeof(g_swatch[0]); i++) {
        int x, y;
        scene_swatch_rect((int)i, &x, &y, NULL, NULL);
        lvds_canvas_fill_rect(x, y, SCENE_SWATCH_W, SCENE_SWATCH_H,
                              lvds_palette_color(g_swatch[i]));
    }
}
