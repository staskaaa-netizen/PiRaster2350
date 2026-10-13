#include "scene.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "lvds_renderer.h"

#define SCENE_PI 3.14159265f

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

/* The moving parts, and how long each takes. Nothing here is a frame count. */
#define SCENE_DISC_R 16
#define SCENE_SWEEP_MS 5000    /* the disc crosses and comes back */
#define SCENE_WAVE_MS 1600     /* the wave travels one wavelength */
#define SCENE_NEEDLE_MS 4000   /* one turn of the hand */
#define SCENE_BAR_MS 3000      /* the bar fills and empties */

int scene_palette_phase(uint32_t now_ms)
{
    return (int)((now_ms / SCENE_PALETTE_MS) % (uint32_t)SCENE_PALETTE_PHASES);
}

void scene_palette_switch(int phase)
{
    int p = ((phase % SCENE_PALETTE_PHASES) + SCENE_PALETTE_PHASES) %
            SCENE_PALETTE_PHASES;

    for (unsigned i = 0; i < sizeof(g_switch_ids) / sizeof(g_switch_ids[0]); i++) {
        lvds_palette_define(g_switch_ids[i],
                            lvds_rgb565(g_banks[p][i][0], g_banks[p][i][1],
                                        g_banks[p][i][2]));
    }
}

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

/* The band the wave lives in, and the wave itself: one function of x and of
   the clock, which the trace, the disc and the checks all ask. */
static void wave_band(int *x0, int *x1, int *y0, int *amp)
{
    int w = lvds_canvas_width();

    if (x0) *x0 = 24;
    if (x1) *x1 = w - 24;
    if (y0) *y0 = 110;
    if (amp) *amp = 30;
}

static int wave_y(int x, uint32_t now_ms)
{
    int y0, amp;
    float phase = (float)(now_ms % SCENE_WAVE_MS) / (float)SCENE_WAVE_MS;

    wave_band(NULL, NULL, &y0, &amp);
    return y0 - (int)((float)amp * sinf((float)x * 0.022f - phase * 2.0f * SCENE_PI));
}

static int disc_x(uint32_t now_ms)
{
    int x0, x1;
    float t = (float)(now_ms % SCENE_SWEEP_MS) / (float)SCENE_SWEEP_MS;
    float s = 0.5f - 0.5f * cosf(t * 2.0f * SCENE_PI);  /* 0..1..0, eased */

    wave_band(&x0, &x1, NULL, NULL);
    x0 += SCENE_DISC_R + 4;
    x1 -= SCENE_DISC_R + 4;
    return x0 + (int)(s * (float)(x1 - x0));
}

void scene_disc_rect(uint32_t now_ms, int *x, int *y, int *r)
{
    int dx = disc_x(now_ms);

    if (x) *x = dx;
    if (y) *y = wave_y(dx, now_ms);
    if (r) *r = SCENE_DISC_R;
}

static void draw_wave(uint32_t now_ms, lvds_color_t color)
{
    int x0, x1, prev_y;

    wave_band(&x0, &x1, NULL, NULL);
    prev_y = wave_y(x0, now_ms);
    for (int x = x0 + 3; x <= x1; x += 3) {
        int y = wave_y(x, now_ms);
        lvds_draw_line(x - 3, prev_y, x, y, color);
        prev_y = y;
    }
}

static void draw_gauge(uint32_t now_ms, lvds_color_t ink, lvds_color_t accent)
{
    int cx = lvds_canvas_width() / 2;
    int cy = 250;
    int r = 70;
    float a = (float)(now_ms % SCENE_NEEDLE_MS) *
              (2.0f * SCENE_PI / (float)SCENE_NEEDLE_MS);

    lvds_draw_ellipse(cx, cy, r, r, ink);
    lvds_draw_ellipse(cx, cy, r - 10, r - 10, ink);
    /* The hand, from the hub to the rim, and a hub to stand on. */
    lvds_draw_line_w(cx, cy, cx + (int)((float)(r - 8) * cosf(a)),
                     cy + (int)((float)(r - 8) * sinf(a)), accent, 2);
    lvds_draw_fill_ellipse(cx, cy, 4, 4, accent);
}

static void draw_meter(uint32_t now_ms, lvds_color_t ink, lvds_color_t accent)
{
    int x0 = 24;
    int x1 = lvds_canvas_width() - 24;
    int y = 350;
    float t = (float)(now_ms % SCENE_BAR_MS) / (float)SCENE_BAR_MS;

    lvds_draw_rect(x0, y, x1 - x0, 14, ink);
    lvds_canvas_fill_rect(x0 + 2, y + 2, (int)(t * (float)(x1 - x0 - 4)), 10,
                          accent);
}

void scene_draw(uint32_t now_ms)
{
    int w = lvds_canvas_width();
    int h = lvds_canvas_height();
    int dx, dy, dr;
    char line[64];
    lvds_color_t ink = lvds_palette_color(LVDS_COL_WHITE_WARM);
    lvds_color_t paper = lvds_palette_color(SCENE_BACKGROUND_COLOR);
    lvds_color_t accent = lvds_palette_color(LVDS_COL_GREEN);

    lvds_canvas_clear(paper);

    /* The frame's own edge, one pixel wide, so a picture that has been turned
       the wrong way cannot be mistaken for one that has not. */
    lvds_draw_rect(0, 0, w, h, lvds_palette_color(SCENE_BORDER_COLOR));

    lvds_draw_text(16, 14, "lvds-renderer", ink, paper, LVDS_FONT_NORMAL);
    lvds_draw_text(16, 34, "one linear while loop, no RTOS", ink, paper,
                   LVDS_FONT_SMALL);
    /* No floats in the text: this board's printf does not format them unless
       the linker is told to pull the formatter in. */
    snprintf(line, sizeof(line), "t %lu s   palette %d   frame %lu ms",
             (unsigned long)(now_ms / 1000u), scene_palette_phase(now_ms),
             (unsigned long)(now_ms % 1000u));
    lvds_draw_text(16, 48, line, ink, paper, LVDS_FONT_SMALL);

    draw_wave(now_ms, lvds_palette_color(LVDS_COL_BLUE));
    scene_disc_rect(now_ms, &dx, &dy, &dr);
    lvds_draw_fill_ellipse(dx, dy, dr, dr, accent);
    lvds_draw_ellipse(dx, dy, dr, dr, ink);

    draw_gauge(now_ms, ink, accent);
    draw_meter(now_ms, ink, lvds_palette_color(LVDS_COL_YELLOW));

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
