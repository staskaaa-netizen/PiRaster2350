#include "lvds_palette.h"

#include <stdbool.h>
#include <stddef.h>

#include "lvds_target.h"

typedef struct {
    const char *name;
    const char *hex;
} lvds_palette_color_def_t;

/* The colours, and the values the panel is asked for. Edit the hex here; the
   order must match `lvds_palette_color_id_t`. */
static const lvds_palette_color_def_t g_color_def[LVDS_COL_COUNT] = {
    [LVDS_COL_GRAY_192] = {"gray_192", "#C0C0C0"},
    [LVDS_COL_GRAY_128] = {"gray_128", "#808080"},
    [LVDS_COL_GRAY_160] = {"gray_160", "#A0A0A0"},
    [LVDS_COL_GRAY_96] = {"gray_96", "#606060"},
    [LVDS_COL_BLACK] = {"black", "#242506"},
    [LVDS_COL_WHITE_WARM] = {"white_warm", "#F0F0F0"},
    [LVDS_COL_YELLOW] = {"yellow", "#fff701"},
    [LVDS_COL_YELLOW_LIGHT] = {"yellow_light", "#FFF582"},
    [LVDS_COL_RED] = {"red", "#E60000"},
    [LVDS_COL_GREEN] = {"green", "#24c863"},
    [LVDS_COL_WHITE] = {"white", "#DDDDDD"},
    /* `yellow_pale` was a second copy of `yellow` and nothing used it. The
       table is full, so the free slot carries a real blue: the panel had none
       (bench, 2026-10-04). */
    [LVDS_COL_BLUE] = {"blue", "#3050C0"},
    [LVDS_COL_TOOL_ORANGE] = {"tool_orange", "#EAFA41"},
    [LVDS_COL_RED_BRIGHT] = {"red_bright", "#ff0000"},
    [LVDS_COL_GREEN_BRIGHT] = {"green_bright", "#3eff24"},
    [LVDS_COL_TOOL_TIP] = {"tool_tip", "#242506"}
};

/* What each name means now (5-6-5), and **not** palette slots: a slot is taken
   when a colour is first *used*, never when this table is read. A picture that
   brings colours of its own has to find the palette empty when it starts, or
   its colours land on whatever was parsed before it. */
static uint16_t g_color_rgb565[LVDS_COL_COUNT];
/* The slot each name holds, and the value that slot was given. A name whose
   value changes while it holds a slot has that slot re-coloured in place -
   that is the whole of the dynamic switch. */
static lvds_color_t g_slot[LVDS_COL_COUNT];
static uint16_t g_slot_value[LVDS_COL_COUNT];
static bool g_has_slot[LVDS_COL_COUNT];
static bool g_palette_ready;

static int lvds_palette_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static uint8_t lvds_palette_hex_byte(const char *s, uint8_t fallback)
{
    int hi;
    int lo;

    if (!s) {
        return fallback;
    }
    hi = lvds_palette_hex_nibble(s[0]);
    lo = lvds_palette_hex_nibble(s[1]);
    if (hi < 0 || lo < 0) {
        return fallback;
    }
    return (uint8_t)((hi << 4) | lo);
}

static uint16_t lvds_palette_parse_hex(const char *hex)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if (!hex) {
        return 0xffffu;
    }
    if (hex[0] == '#') {
        hex++;
    }
    r = lvds_palette_hex_byte(hex + 0, 255);
    g = lvds_palette_hex_byte(hex + 2, 255);
    b = lvds_palette_hex_byte(hex + 4, 255);
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) |
                      (uint16_t)(b >> 3));
}

void lvds_palette_reset(void)
{
    g_palette_ready = false;
    for (int i = 0; i < LVDS_COL_COUNT; i++) {
        g_has_slot[i] = false;
    }
}

void lvds_palette_init(void)
{
    if (g_palette_ready) {
        return;
    }
    for (int i = 0; i < LVDS_COL_COUNT; i++) {
        g_color_rgb565[i] = lvds_palette_parse_hex(g_color_def[i].hex);
    }
    g_palette_ready = true;
}

lvds_color_t lvds_palette_color(lvds_palette_color_id_t id)
{
    if (!g_palette_ready) {
        lvds_palette_init();
    }
    if ((unsigned)id >= (unsigned)LVDS_COL_COUNT) {
        id = LVDS_COL_WHITE_WARM;
    }
    if (!g_has_slot[id]) {
        /* The slot is taken here, on the first frame that uses this colour -
           see `g_color_rgb565`. */
        g_slot[id] = lvds_target_slot(g_color_rgb565[id]);
        g_slot_value[id] = g_color_rgb565[id];
        g_has_slot[id] = true;
    } else if (g_slot_value[id] != g_color_rgb565[id]) {
        /* The name was redefined under a slot we already hold: re-colour that
           slot rather than take another one, so a switch costs nothing and
           everything already drawn with it changes too. */
        lvds_target_slot_set(g_slot[id], g_color_rgb565[id]);
        g_slot_value[id] = g_color_rgb565[id];
    }
    return g_slot[id];
}

void lvds_palette_define(lvds_palette_color_id_t id, uint16_t rgb565)
{
    if (!g_palette_ready) {
        lvds_palette_init();
    }
    if ((unsigned)id >= (unsigned)LVDS_COL_COUNT) {
        return;
    }
    g_color_rgb565[id] = rgb565;
}

void lvds_palette_defaults(void)
{
    g_palette_ready = false;
    lvds_palette_init();
}

uint16_t lvds_palette_value(lvds_palette_color_id_t id)
{
    if (!g_palette_ready) {
        lvds_palette_init();
    }
    if ((unsigned)id >= (unsigned)LVDS_COL_COUNT) {
        return 0;
    }
    return g_color_rgb565[id];
}

const char *lvds_palette_color_name(lvds_palette_color_id_t id)
{
    if ((unsigned)id >= (unsigned)LVDS_COL_COUNT) {
        return "";
    }
    return g_color_def[id].name;
}
