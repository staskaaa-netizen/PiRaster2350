#include "lvds_draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvds_font.h"

#define LVDS_DRAW_COORD_GUARD 4096
#define LVDS_DRAW_TEXT_GUARD 256

static bool draw_coord_sane(int v)
{
    return v >= -LVDS_DRAW_COORD_GUARD && v <= LVDS_DRAW_COORD_GUARD;
}

static bool draw_rect_sane(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (!draw_coord_sane(x) || !draw_coord_sane(y) ||
        !draw_coord_sane(w) || !draw_coord_sane(h)) {
        return false;
    }
    return x < LVDS_VIEW_WIDTH && y < LVDS_VIEW_HEIGHT &&
           x + w > 0 && y + h > 0;
}

void lvds_draw_line(int x1, int y1, int x2, int y2, lvds_color_t color)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    if (!lvds_canvas_ready() ||
        !draw_coord_sane(x1) || !draw_coord_sane(y1) ||
        !draw_coord_sane(x2) || !draw_coord_sane(y2)) {
        return;
    }
    /* A line whose both ends are off the same side of the view draws nothing:
       the walk below would be a few thousand dropped pixels. */
    if ((x1 < 0 && x2 < 0) || (x1 >= LVDS_VIEW_WIDTH && x2 >= LVDS_VIEW_WIDTH) ||
        (y1 < 0 && y2 < 0) || (y1 >= LVDS_VIEW_HEIGHT && y2 >= LVDS_VIEW_HEIGHT)) {
        return;
    }
    while (1) {
        lvds_canvas_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void lvds_draw_line_w(int x1, int y1, int x2, int y2, lvds_color_t color,
                      int width)
{
    int r = width > 1 ? width / 2 : 0;

    if (width <= 0 || width > 16) {
        return;
    }
    for (int oy = -r; oy <= r; oy++) {
        for (int ox = -r; ox <= r; ox++) {
            lvds_draw_line(x1 + ox, y1 + oy, x2 + ox, y2 + oy, color);
        }
    }
}

void lvds_draw_rect(int x, int y, int w, int h, lvds_color_t color)
{
    if (!draw_rect_sane(x, y, w, h)) {
        return;
    }
    lvds_draw_line(x, y, x + w - 1, y, color);
    lvds_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    lvds_draw_line(x, y, x, y + h - 1, color);
    lvds_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void lvds_draw_ellipse(int x, int y, int rx, int ry, lvds_color_t color)
{
    if (!lvds_canvas_ready() || rx <= 0 || ry <= 0 ||
        rx > LVDS_VIEW_WIDTH || ry > LVDS_VIEW_HEIGHT ||
        !draw_coord_sane(x) || !draw_coord_sane(y)) {
        return;
    }
    for (int a = 0; a < 360; a += 2) {
        float rad = (float)a * 0.01745329252f;
        lvds_canvas_pixel(x + (int)(rx * cosf(rad)), y + (int)(ry * sinf(rad)),
                          color);
    }
}

void lvds_draw_fill_ellipse(int x, int y, int rx, int ry, lvds_color_t color)
{
    if (!lvds_canvas_ready() || rx <= 0 || ry <= 0 ||
        rx > LVDS_VIEW_WIDTH || ry > LVDS_VIEW_HEIGHT ||
        !draw_coord_sane(x) || !draw_coord_sane(y)) {
        return;
    }
    for (int yy = -ry; yy <= ry; yy++) {
        int span = (int)(rx * sqrtf(1.0f - ((float)(yy * yy) / (float)(ry * ry))));
        lvds_draw_line(x - span, y + yy, x + span, y + yy, color);
    }
}

static int font_scale(int font)
{
    return font == LVDS_FONT_LARGE ? 2 : 1;
}

static int font_char_w(int font)
{
    return font == LVDS_FONT_SMALL ? 6 : 8;
}

static int font_char_h(int font)
{
    return font == LVDS_FONT_SMALL ? 8 : 14;
}

static const uint8_t *font_bitmap(int font)
{
    return font == LVDS_FONT_SMALL ? LVDS_FONT_COND_6X8 : LVDS_FONT_IBM_8X14;
}

static void draw_char_scaled(int x, int y, unsigned char ch, lvds_color_t fg,
                             lvds_color_t bg, int font_id)
{
    int scale = font_scale(font_id);
    int cw = font_char_w(font_id);
    int chh = font_char_h(font_id);
    const uint8_t *bits = font_bitmap(font_id);

    lvds_canvas_fill_rect(x, y, cw * scale, chh * scale, bg);
    for (int row = 0; row < chh; row++) {
        uint8_t line = bits[row * 256 + ch];
        for (int col = 0; col < cw; col++) {
            if (line & 0x80u) {
                lvds_canvas_fill_rect(x + col * scale, y + row * scale, scale,
                                      scale, fg);
            }
            line <<= 1;
        }
    }
}

void lvds_draw_text(int x, int y, const char *text, lvds_color_t fg,
                    lvds_color_t bg, int font_id)
{
    int step = font_char_w(font_id) * font_scale(font_id);
    int guard = 0;

    if (!lvds_canvas_ready() || !text ||
        !draw_coord_sane(x) || !draw_coord_sane(y)) {
        return;
    }
    while (*text && guard++ < LVDS_DRAW_TEXT_GUARD) {
        draw_char_scaled(x, y, (unsigned char)*text++, fg, bg, font_id);
        x += step;
    }
}

int lvds_draw_text_width(const char *text, int font_id)
{
    if (!text) {
        return 0;
    }
    return (int)strlen(text) * font_char_w(font_id) * font_scale(font_id);
}

void lvds_draw_text_clip(int x, int y, const char *text, int cols,
                         lvds_color_t fg, lvds_color_t bg, int font)
{
    char buf[160];
    int len;

    if (!text) {
        text = "";
    }
    if (cols < 1) {
        return;
    }
    if (cols >= (int)sizeof(buf)) {
        cols = (int)sizeof(buf) - 1;
    }
    len = (int)strlen(text);
    if (len > cols) {
        len = cols;
    }
    snprintf(buf, sizeof(buf), "%-*.*s", cols, len, text);
    if ((int)strlen(text) > cols && cols > 3) {
        buf[cols - 3] = '.';
        buf[cols - 2] = '.';
        buf[cols - 1] = '.';
        buf[cols] = '\0';
    }
    lvds_draw_text(x, y, buf, fg, bg, font);
}
