#include "lvds_canvas.h"

#include "lvds_palette.h"
#include "lvds_target.h"

/* A drawing coordinate this far off the screen is not a drawing mistake worth
   tracing through: it is dropped by the guard below, so the arithmetic inside
   the primitives cannot overflow. */
#define LVDS_CANVAS_COORD_GUARD 4096

static bool g_ready;

bool lvds_canvas_init(void)
{
    g_ready = lvds_target_init();
    return g_ready;
}

bool lvds_canvas_ready(void)
{
    return g_ready;
}

int lvds_canvas_width(void)
{
    return LVDS_VIEW_WIDTH;
}

int lvds_canvas_height(void)
{
    return LVDS_VIEW_HEIGHT;
}

static bool canvas_coord_sane(int v)
{
    return v >= -LVDS_CANVAS_COORD_GUARD && v <= LVDS_CANVAS_COORD_GUARD;
}

static bool canvas_rect_sane(int x, int y, int w, int h)
{
    if (!g_ready || w <= 0 || h <= 0) {
        return false;
    }
    if (!canvas_coord_sane(x) || !canvas_coord_sane(y) ||
        !canvas_coord_sane(w) || !canvas_coord_sane(h)) {
        return false;
    }
    /* The view decides: a rectangle that reaches it at all is worth the
       mapping, and the target clips the rest. */
    return x < LVDS_VIEW_WIDTH && y < LVDS_VIEW_HEIGHT &&
           x + w > 0 && y + h > 0;
}

void lvds_canvas_clear(lvds_color_t color)
{
    if (!g_ready) {
        return;
    }
    lvds_target_clear(color);
}

void lvds_canvas_pixel(int x, int y, lvds_color_t color)
{
    if (!g_ready) {
        return;
    }
    if ((unsigned)x >= (unsigned)LVDS_VIEW_WIDTH ||
        (unsigned)y >= (unsigned)LVDS_VIEW_HEIGHT) {
        return;
    }
    lvds_target_pixel(LVDS_PANEL_X(x, y), LVDS_PANEL_Y(x, y), color);
}

void lvds_canvas_fill_rect(int x, int y, int w, int h, lvds_color_t color)
{
    int x2;
    int y2;

    if (!canvas_rect_sane(x, y, w, h)) {
        return;
    }

    /* A filled rectangle is still a rectangle after a quarter turn: the whole
       fill turns with two numbers and one swap, so a screen clear is one call
       and not one pixel at a time. */
    x2 = LVDS_PANEL_RECT_X(x, y, w, h);
    y2 = LVDS_PANEL_RECT_Y(x, y, w, h);
    w = LVDS_PANEL_RECT_W(w, h);
    h = LVDS_PANEL_RECT_H(w, h);
    x = x2;
    y = y2;

    x2 = x + w;
    y2 = y + h;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x2 > LVDS_WIDTH) {
        x2 = LVDS_WIDTH;
    }
    if (y2 > LVDS_HEIGHT) {
        y2 = LVDS_HEIGHT;
    }
    if (x >= x2 || y >= y2) {
        return;
    }

    lvds_target_fill_rect(x, y, x2 - x, y2 - y, color);
}

void lvds_canvas_present(void)
{
    if (!g_ready) {
        return;
    }
    lvds_target_present();
}

void lvds_canvas_palette_reset(void)
{
    lvds_palette_reset();
    lvds_target_palette_reset();
}
