#include "host_panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvds_target.h"
#include "lvds_view.h"

static uint32_t *g_frame;
static unsigned g_presents;
/* What each palette slot means, so a blit of indices can be expanded. It is
   filled by `lvds_target_slot_set()`; until then slot i is its own grey. */
static uint32_t g_slot_rgb[16];
static bool g_slot_known[16];

bool lvds_target_init(void)
{
    g_frame = (uint32_t *)calloc((size_t)LVDS_WIDTH * LVDS_HEIGHT,
                                 sizeof(uint32_t));
    return g_frame != NULL;
}

void lvds_target_pixel(int x, int y, lvds_color_t color)
{
    if (!g_frame ||
        (unsigned)x >= (unsigned)LVDS_WIDTH ||
        (unsigned)y >= (unsigned)LVDS_HEIGHT) {
        return;
    }
    g_frame[(size_t)y * LVDS_WIDTH + (size_t)x] = lvds_rgb888(color);
}

void lvds_target_fill_rect(int x, int y, int w, int h, lvds_color_t color)
{
    uint32_t rgb;
    int x2 = x + w;
    int y2 = y + h;

    if (!g_frame || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > LVDS_WIDTH) x2 = LVDS_WIDTH;
    if (y2 > LVDS_HEIGHT) y2 = LVDS_HEIGHT;
    if (x >= x2 || y >= y2) {
        return;
    }
    rgb = lvds_rgb888(color);
    for (int yy = y; yy < y2; yy++) {
        uint32_t *row = g_frame + (size_t)yy * LVDS_WIDTH + (size_t)x;
        for (int xx = x; xx < x2; xx++) {
            *row++ = rgb;
        }
    }
}

void lvds_target_clear(lvds_color_t color)
{
    if (!g_frame) {
        return;
    }
    lvds_target_fill_rect(0, 0, LVDS_WIDTH, LVDS_HEIGHT, color);
}

void lvds_target_present(void)
{
    if (g_frame) {
        g_presents++;
    }
}

lvds_color_t lvds_target_slot(uint16_t rgb565)
{
    /* No palette on the host: the word is the colour. */
    return rgb565;
}

void lvds_target_slot_set(lvds_color_t slot, uint16_t rgb565)
{
    /* Nothing on the glass to re-colour in place - a host frame is 32bpp, so a
       colour redefined shows up the next time something is *drawn* with it. But
       the slot's meaning is remembered, because a blit of indices has to be
       expanded into pixels. */
    if (slot < 16) {
        g_slot_rgb[slot] = lvds_rgb888(rgb565);
        g_slot_known[slot] = true;
    }
}

void lvds_target_palette_reset(void)
{
    /* Nothing to give back, but the remembered slot meanings go with it. */
    memset(g_slot_known, 0, sizeof(g_slot_known));
}

/* A row of the caller's own pixels: indices, expanded through the remembered
   slot table. A host frame is 32bpp with no palette of its own, so this is the
   only path by which an index has a colour here. */
void lvds_target_blit(int x, int y, int w, const uint8_t *indices)
{
    if (!g_frame || !indices || w <= 0 ||
        (unsigned)y >= (unsigned)LVDS_HEIGHT) {
        return;
    }
    if (x < 0) {
        indices += -x;
        w += x;
        x = 0;
    }
    if (x >= (int)LVDS_WIDTH) {
        return;
    }
    if (x + w > (int)LVDS_WIDTH) {
        w = (int)LVDS_WIDTH - x;
    }
    for (int i = 0; i < w; i++) {
        uint8_t index = (uint8_t)(indices[i] & 0x0fu);
        uint32_t rgb = g_slot_known[index]
                           ? g_slot_rgb[index]
                           : (uint32_t)index * 0x111111u;  /* the greys, until told */
        g_frame[(size_t)y * LVDS_WIDTH + (size_t)(x + i)] = rgb;
    }
}

unsigned host_panel_presents(void)
{
    return g_presents;
}

int host_panel_width(void)
{
    return LVDS_WIDTH;
}

int host_panel_height(void)
{
    return LVDS_HEIGHT;
}

uint32_t host_panel_pixel(int x, int y)
{
    if (!g_frame || (unsigned)x >= (unsigned)LVDS_WIDTH ||
        (unsigned)y >= (unsigned)LVDS_HEIGHT) {
        return 0;
    }
    return g_frame[(size_t)y * LVDS_WIDTH + (size_t)x];
}

/* A 24bpp BMP, bottom-up with the rows padded to four bytes, written by hand:
   a demo that needs a library to look at its own picture is not a demo. */
static bool write_bmp(const char *path, const uint32_t *pixels, int w, int h)
{
    uint8_t header[54];
    uint32_t row_bytes = (uint32_t)w * 3u;
    uint32_t pad = (4u - (row_bytes & 3u)) & 3u;
    uint32_t image_bytes = (row_bytes + pad) * (uint32_t)h;
    uint8_t padding[3] = {0, 0, 0};
    FILE *f = fopen(path, "wb");

    if (!f) {
        return false;
    }
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)((54u + image_bytes) >> 0);
    header[3] = (uint8_t)((54u + image_bytes) >> 8);
    header[4] = (uint8_t)((54u + image_bytes) >> 16);
    header[5] = (uint8_t)((54u + image_bytes) >> 24);
    header[10] = 54;
    header[14] = 40;                              /* BITMAPINFOHEADER */
    header[18] = (uint8_t)(w >> 0);
    header[19] = (uint8_t)(w >> 8);
    header[20] = (uint8_t)(w >> 16);
    header[21] = (uint8_t)(w >> 24);
    header[22] = (uint8_t)(h >> 0);
    header[23] = (uint8_t)(h >> 8);
    header[24] = (uint8_t)(h >> 16);
    header[25] = (uint8_t)(h >> 24);
    header[26] = 1;                               /* planes */
    header[28] = 24;                              /* bits per pixel */
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return false;
    }
    for (int y = h - 1; y >= 0; y--) {
        const uint32_t *row = pixels + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            uint32_t rgb = row[x];
            uint8_t bgr[3];
            bgr[0] = (uint8_t)(rgb >> 0);
            bgr[1] = (uint8_t)(rgb >> 8);
            bgr[2] = (uint8_t)(rgb >> 16);
            if (fwrite(bgr, 1, sizeof(bgr), f) != sizeof(bgr)) {
                fclose(f);
                return false;
            }
        }
        if (pad && fwrite(padding, 1, pad, f) != pad) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

bool host_panel_write_bmp(const char *path, bool as_view)
{
    int w = as_view ? LVDS_VIEW_WIDTH : LVDS_WIDTH;
    int h = as_view ? LVDS_VIEW_HEIGHT : LVDS_HEIGHT;
    uint32_t *out;
    bool ok;

    if (!g_frame || !path) {
        return false;
    }
    out = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!out) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* Reading the view is reading the panel through the turn: one
               statement, in `lvds_view.h`, decides what "up" means. */
            int px = as_view ? LVDS_PANEL_X(x, y) : x;
            int py = as_view ? LVDS_PANEL_Y(x, y) : y;
            out[(size_t)y * (size_t)w + (size_t)x] =
                g_frame[(size_t)py * LVDS_WIDTH + (size_t)px];
        }
    }
    ok = write_bmp(path, out, w, h);
    free(out);
    return ok;
}
