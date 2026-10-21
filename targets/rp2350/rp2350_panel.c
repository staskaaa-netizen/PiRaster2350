/* The RP2350 target: the panel, and the HSTX scanout that feeds it.

   This is the only file that knows what HSTX is. It writes the SRAM
   framebuffer the scanout reads, prepares the active lines on core1 through a
   static descriptor ring, and hands the finished picture over with a CPU copy.
   Everything that decides *what* a picture looks like lives in `core/`; this
   file only knows how to put a pixel, a rectangle and a colour on glass.

   The realtime rules are in `LIMITS.md` and are not negotiable: static aligned
   buffers, no large automatic locals in the core1 path, no PSRAM scanout, no
   DMA present, no health polling. The `LVDS_HEADLESS` bench experiment is a
   separate probe made only of SRAM-resident baked lines; the full-frame PSRAM
   version failed its bench and is not present. */

#include "rp2350_port.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/regs/clocks.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "lvds_config.h"
#include "lvds_palette.h"
#include "lvds_target.h"
#include "rp2350_panel.h"

/* HSTX/LVDS ownership model:
 * - core0 may request draw/present through the target calls only.
 * - core1 owns LUT expansion, active line preparation, and scanout scheduler state.
 * - DMA owns the live HSTX FIFO feed and descriptor/control progression.
 * - UI/render code must not directly modify live DMA channel/control state.
 * - Permanent realtime buffers are static/aligned; no large automatic locals in
 *   the core1/HSTX realtime path.
 */

#ifndef LVDS_TARGET_USE_PSRAM
#define LVDS_TARGET_USE_PSRAM 0
#endif

#ifndef LVDS_HEADLESS_LOWRES
#define LVDS_HEADLESS_LOWRES 0
#endif

#ifndef LVDS_TARGET_BPP
#define LVDS_TARGET_BPP 4
#endif

#if LVDS_TARGET_BPP != 1 && LVDS_TARGET_BPP != 2 && LVDS_TARGET_BPP != 4
#error LVDS_TARGET_BPP must be 1, 2 or 4
#endif

#if LVDS_HEADLESS_LOWRES && LVDS_TARGET_BPP != 4
#error The low-resolution bench currently requires its 4bpp source raster
#endif

#define LVDS_PIXELS_PER_BYTE (8 / LVDS_TARGET_BPP)
#define LVDS_PALETTE_SLOTS (1u << LVDS_TARGET_BPP)
#define LVDS_COLOUR_MASK (LVDS_PALETTE_SLOTS - 1u)

#if LVDS_TARGET_USE_PSRAM
#include "rp2350_psram.h"
#endif

/* The frame is the glass plus the spare active lines the panel's close needs
   (the panel header's `LVDS_PANEL_ACTIVE_HEIGHT`), and the timing is the
   panel's own. Nothing below hard-codes a panel number. */
#define LVDS_FRAME_WIDTH LVDS_WIDTH
#define LVDS_FRAME_HEIGHT LVDS_PANEL_ACTIVE_HEIGHT

#if LVDS_HEADLESS_LOWRES
#define LVDS_DRAW_WIDTH 200
#define LVDS_DRAW_HEIGHT 120
#else
#define LVDS_DRAW_WIDTH LVDS_FRAME_WIDTH
#define LVDS_DRAW_HEIGHT LVDS_FRAME_HEIGHT
#endif
#define LVDS_DRAW_STRIDE (LVDS_DRAW_WIDTH / LVDS_PIXELS_PER_BYTE)

#define LVDS_HSTX_H_FRONT_PORCH LVDS_H_FRONT_PORCH
#define LVDS_HSTX_H_SYNC_WIDTH LVDS_H_SYNC_WIDTH
#define LVDS_HSTX_H_BACK_PORCH LVDS_H_BACK_PORCH
#define LVDS_HSTX_V_FRONT_PORCH LVDS_V_FRONT_PORCH
#define LVDS_HSTX_V_SYNC_WIDTH LVDS_V_SYNC_WIDTH
#define LVDS_HSTX_V_BACK_PORCH LVDS_V_BACK_PORCH

#define LVDS_HSTX_D0P LVDS_D0P
#define LVDS_HSTX_D0M LVDS_D0M
#define LVDS_HSTX_D1P LVDS_D1P
#define LVDS_HSTX_D1M LVDS_D1M
#define LVDS_HSTX_D2P LVDS_D2P
#define LVDS_HSTX_D2M LVDS_D2M
#define LVDS_HSTX_CLKP LVDS_CLKP
#define LVDS_HSTX_CLKM LVDS_CLKM

#define LVDS_HSTX_H_INACTIVE (LVDS_HSTX_H_FRONT_PORCH + LVDS_HSTX_H_SYNC_WIDTH + LVDS_HSTX_H_BACK_PORCH)
#define LVDS_HSTX_V_INACTIVE (LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH + LVDS_HSTX_V_BACK_PORCH)
#define LVDS_HSTX_H_TOTAL (LVDS_FRAME_WIDTH + LVDS_HSTX_H_INACTIVE)
#define LVDS_HSTX_V_TOTAL (LVDS_FRAME_HEIGHT + LVDS_HSTX_V_INACTIVE)

#define LVDS_HSTX_H_FRONT_TRANSFERS (7 * LVDS_HSTX_H_FRONT_PORCH / 8)
#define LVDS_HSTX_H_SYNC_TRANSFERS (7 * LVDS_HSTX_H_SYNC_WIDTH / 8)
#define LVDS_HSTX_H_INACTIVE_TRANSFERS (7 * LVDS_HSTX_H_INACTIVE / 8)
#define LVDS_HSTX_H_ACTIVE_TRANSFERS (7 * LVDS_FRAME_WIDTH / 8)
#define LVDS_HSTX_LINE_TRANSFERS (LVDS_HSTX_H_INACTIVE_TRANSFERS + LVDS_HSTX_H_ACTIVE_TRANSFERS)
#define LVDS_HSTX_PREFIX_WORDS (1u + LVDS_HSTX_H_INACTIVE_TRANSFERS)
#define LVDS_HSTX_TAIL_WORDS (1u + LVDS_HSTX_H_ACTIVE_TRANSFERS)

#define LVDS_FRAME_BYTES \
    (LVDS_DRAW_WIDTH * LVDS_DRAW_HEIGHT / LVDS_PIXELS_PER_BYTE)
#define LVDS_HSTX_CLOCK 0xC6000000u
#define LVDS_HSTX_ACTIVE (LVDS_HSTX_CLOCK | 0x00E00000u)
#define LVDS_HSTX_INACTIVE (LVDS_HSTX_CLOCK | 0x00600000u)
#define LVDS_HSTX_INACTIVE_HS (LVDS_HSTX_CLOCK | 0x00400000u)
#define LVDS_HSTX_INACTIVE_VS (LVDS_HSTX_CLOCK | 0x00200000u)
#define LVDS_HSTX_INACTIVE_HSVS LVDS_HSTX_CLOCK
#define LVDS_HSTX_COORD_GUARD 4096
#define LVDS_HSTX_TEXT_GUARD 256

#define LVDS_HSTX_CMD_RAW (0x0u << 12)
#define LVDS_HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define LVDS_HSTX_CMD_NOP (0xfu << 12)

#if LVDS_HEADLESS
_Static_assert(RP2350_PANEL_LINE_TRANSFERS == LVDS_HSTX_LINE_TRANSFERS,
               "bake count must match the core1 scanline");
_Static_assert(RP2350_PANEL_LINE_TRANSFERS <= 0x0fffu,
               "one RAW command must carry a whole LVDS line");
#endif

#if LVDS_HEADLESS_LOWRES
_Static_assert(LVDS_DRAW_WIDTH * 4u == LVDS_WIDTH,
               "200-pixel source must scale 4x to the panel");
_Static_assert(LVDS_DRAW_HEIGHT * 5u == LVDS_HEIGHT,
               "120-row source must scale 5x to the panel");
_Static_assert(LVDS_HSTX_PREFIX_WORDS + LVDS_HSTX_TAIL_WORDS ==
               LVDS_HSTX_LINE_TRANSFERS + 2u,
               "prefix and tail must preserve one complete scanline");
#endif

/* The scanout's two DMA channels are taken from the allocator, never fixed
   numbers: a client's own DMA - a USB stack, PSRAM, an audio path - can hold
   10 and 11 by the time the panel comes up, and a fixed pair then either
   clobbers that driver's transfers or loses the scanout its own. They are
   claimed on core0 in `lvds_target_init` before core1 starts; the core1 path
   only reads them. */
#define LVDS_HSTX_DMACH_DATA (g_dma_data_channel)
#define LVDS_HSTX_DMACH_CTRL (g_dma_ctrl_channel)
#define LVDS_HSTX_SYS_CLOCK_KHZ LVDS_SYS_CLOCK_KHZ
#define LVDS_HSTX_ACTIVE_LINE_BUFFERS 4u

#ifndef LVDS_HSTX_CORE1_READY_TIMEOUT_MS
#define LVDS_HSTX_CORE1_READY_TIMEOUT_MS 1000u
#endif

#ifndef LVDS_HSTX_DESCRIPTOR_RING_LINES
#define LVDS_HSTX_DESCRIPTOR_RING_LINES 1024u
#endif

#ifndef LVDS_HSTX_CORE1_STACK_WORDS
#define LVDS_HSTX_CORE1_STACK_WORDS 2048u
#endif

#ifndef LVDS_HSTX_SECTION_CTX
#define LVDS_HSTX_SECTION_CTX __attribute__((aligned(64)))
#endif

#ifndef LVDS_HSTX_SECTION_DMA
#define LVDS_HSTX_SECTION_DMA __attribute__((aligned(64)))
#endif

#ifndef LVDS_HSTX_SECTION_LINES
#define LVDS_HSTX_SECTION_LINES __attribute__((aligned(64)))
#endif

#ifndef LVDS_HSTX_SECTION_PALETTE
#define LVDS_HSTX_SECTION_PALETTE __attribute__((aligned(64)))
#endif

#ifndef LVDS_TARGET_USE_HSTX_PLL
#define LVDS_TARGET_USE_HSTX_PLL 0
#endif

#ifndef LVDS_HSTX_PLL_KHZ
#define LVDS_HSTX_PLL_KHZ LVDS_HSTX_SYS_CLOCK_KHZ
#endif

#ifndef LVDS_HSTX_CLOCK_DIV
#define LVDS_HSTX_CLOCK_DIV 2
#endif

typedef struct {
    uint8_t data[LVDS_FRAME_BYTES];
} lvds_hstx_fb_block_t;

typedef struct {
    uint32_t data[LVDS_HSTX_ACTIVE_LINE_BUFFERS][LVDS_HSTX_LINE_TRANSFERS];
} lvds_hstx_active_line_block_t;

typedef struct {
    uint32_t data[LVDS_HSTX_LINE_TRANSFERS];
} lvds_hstx_line_block_t;

typedef struct {
    uint32_t data[LVDS_HSTX_DESCRIPTOR_RING_LINES];
} lvds_hstx_dma_ring_block_t;

#if LVDS_HEADLESS_LOWRES
typedef struct {
    uint32_t data[LVDS_DRAW_HEIGHT][RP2350_PANEL_BAKE_LINE_WORDS];
} lvds_hstx_lowres_rows_block_t;

typedef struct {
    uint32_t data[RP2350_PANEL_BAKE_LINE_WORDS];
} lvds_hstx_full_line_block_t;
#endif

typedef struct {
    uint32_t pad;
    uint32_t data[LVDS_HSTX_CORE1_STACK_WORDS];
} lvds_hstx_stack_block_t;

typedef struct {
    volatile bool display_started;
    bool backbuffer_active;
    bool dirty;
} lvds_hstx_ctrl_state_t;

typedef struct {
    volatile bool ready;
    uint32_t descriptor_last_index;
} lvds_hstx_core1_state_t;

typedef struct {
    uint32_t colour_lut[7][256];
    uint16_t rgb565[16];
    bool used[16];
    uint8_t next;
} lvds_hstx_palette_state_t;

static lvds_hstx_fb_block_t g_framebuffer_block LVDS_HSTX_SECTION_DMA;
#define g_framebuffer (g_framebuffer_block.data)
static uint8_t *g_draw_buffer LVDS_HSTX_SECTION_CTX = g_framebuffer;
static lvds_hstx_active_line_block_t g_active_line_block LVDS_HSTX_SECTION_LINES;
#define g_active_line (g_active_line_block.data)
static lvds_hstx_line_block_t g_inactive_line_block LVDS_HSTX_SECTION_LINES;
#define g_inactive_line (g_inactive_line_block.data)
static lvds_hstx_line_block_t g_inactive_line_vs_block LVDS_HSTX_SECTION_LINES;
#define g_inactive_line_vs (g_inactive_line_vs_block.data)
static lvds_hstx_dma_ring_block_t g_scanout_line_addr_ring_block __attribute__((aligned(4096)));
#define g_scanout_line_addr_ring (g_scanout_line_addr_ring_block.data)
static lvds_hstx_stack_block_t g_core1_stack_block LVDS_HSTX_SECTION_CTX;
#define g_core1_stack (g_core1_stack_block.data)
static lvds_hstx_ctrl_state_t g_hstx_ctrl LVDS_HSTX_SECTION_CTX;
static lvds_hstx_core1_state_t g_hstx_core1 LVDS_HSTX_SECTION_CTX = {
    .descriptor_last_index = 0xffffffffu
};
static lvds_hstx_palette_state_t g_hstx_palette LVDS_HSTX_SECTION_PALETTE;

#if LVDS_TARGET_BPP == 1
static uint32_t g_expand_1bpp[256] LVDS_HSTX_SECTION_PALETTE;
#elif LVDS_TARGET_BPP == 2
static uint16_t g_expand_2bpp[256] LVDS_HSTX_SECTION_PALETTE;
#endif

#define g_colour_lut (g_hstx_palette.colour_lut)
#define g_palette_rgb565 (g_hstx_palette.rgb565)
#define g_palette_used (g_hstx_palette.used)
#define g_palette_next (g_hstx_palette.next)
#define g_display_started (g_hstx_ctrl.display_started)
#define g_backbuffer_active (g_hstx_ctrl.backbuffer_active)
#define g_dirty (g_hstx_ctrl.dirty)
#define g_core1_ready (g_hstx_core1.ready)
#define g_descriptor_last_index (g_hstx_core1.descriptor_last_index)

/* Claimed in `lvds_target_init`, on core0, before core1 is launched: the
   scanout owns them for the life of the program, and nothing below claims or
   releases them again. */
static uint g_dma_data_channel;
static uint g_dma_ctrl_channel;

#if LVDS_HEADLESS_LOWRES
static lvds_hstx_lowres_rows_block_t g_lowres_rows LVDS_HSTX_SECTION_LINES;
static lvds_hstx_full_line_block_t g_lowres_blank LVDS_HSTX_SECTION_LINES;
static lvds_hstx_full_line_block_t g_lowres_vs LVDS_HSTX_SECTION_LINES;
#endif

#if LVDS_HEADLESS && LVDS_HEADLESS_SRAM_BARS
typedef struct {
    uint32_t data[RP2350_PANEL_BAKE_LINE_WORDS];
} lvds_hstx_baked_line_block_t;

static lvds_hstx_baked_line_block_t g_headless_active_sram LVDS_HSTX_SECTION_LINES;
static lvds_hstx_baked_line_block_t g_headless_blank_sram LVDS_HSTX_SECTION_LINES;
static lvds_hstx_baked_line_block_t g_headless_vsync_sram LVDS_HSTX_SECTION_LINES;
#endif

static void __no_inline_not_in_flash_func(core1_entry)(void);

static void lvds_hstx_launch_core1(void)
{
    multicore_launch_core1_with_stack(core1_entry,
                                      g_core1_stack,
                                      sizeof(g_core1_stack));
}

static uint32_t lvds_colour_from_rgb565(uint16_t rgb565)
{
    uint8_t r5 = (uint8_t)((rgb565 >> 11) & 0x1f);
    uint8_t g6 = (uint8_t)((rgb565 >> 5) & 0x3f);
    uint8_t b5 = (uint8_t)(rgb565 & 0x1f);
    uint8_t r6 = (uint8_t)((r5 << 1) | (r5 >> 4));
    uint8_t b6 = (uint8_t)((b5 << 1) | (b5 >> 4));

    // LQ121S1LG44 single-channel LVDS:
    // RXIN0 R0 R1 R2 R3 R4 R5 G0, RXIN1 G1..G5 B0 B1,
    // RXIN2 B2 B3 B4 B5 HS VS DE.
    return LVDS_HSTX_ACTIVE |
           ((uint32_t)(r6 & 0x3f) << 1) |
           ((uint32_t)(g6 & 0x3e) << 8) |
           ((uint32_t)(g6 & 0x01) << 7) |
           ((uint32_t)(b6 & 0x3c) << 15) |
           ((uint32_t)(b6 & 0x03) << 14);
}

static void lvds_rebuild_lut(void)
{
    for (uint16_t i = 0; i < LVDS_PALETTE_SLOTS; i++) {
        for (uint16_t j = 0; j < LVDS_PALETTE_SLOTS; j++) {
            uint16_t colours = (uint16_t)((j << 4) | i);
            uint32_t first_pixel = lvds_colour_from_rgb565(g_palette_rgb565[i]);
            uint32_t next_pixel = lvds_colour_from_rgb565(g_palette_rgb565[j]);
            g_colour_lut[0][colours] = ((first_pixel << 0) & 0xFEFEFEFEu) | ((next_pixel >> 7) & 0x01010101u);
            g_colour_lut[1][colours] = ((first_pixel << 1) & 0xFCFCFCFCu) | ((next_pixel >> 6) & 0x03030303u);
            g_colour_lut[2][colours] = ((first_pixel << 2) & 0xF8F8F8F8u) | ((next_pixel >> 5) & 0x07070707u);
            g_colour_lut[3][colours] = ((first_pixel << 3) & 0xF0F0F0F0u) | ((next_pixel >> 4) & 0x0F0F0F0Fu);
            g_colour_lut[4][colours] = ((first_pixel << 4) & 0xE0E0E0E0u) | ((next_pixel >> 3) & 0x1F1F1F1Fu);
            g_colour_lut[5][colours] = ((first_pixel << 5) & 0xC0C0C0C0u) | ((next_pixel >> 2) & 0x3F3F3F3Fu);
            g_colour_lut[6][colours] = ((first_pixel << 6) & 0x80808080u) | ((next_pixel >> 1) & 0x7F7F7F7Fu);
        }
    }
}

static void palette_set(uint8_t index, uint16_t rgb565)
{
    if (index >= LVDS_PALETTE_SLOTS) {
        return;
    }
    g_palette_rgb565[index] = rgb565;
    g_palette_used[index] = true;
    lvds_rebuild_lut();
}

static void palette_reset(void)
{
    static const uint16_t defaults[16] = {
        0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff,
        0x8410, 0xc618, 0x4208, 0xfc00, 0x03e0, 0x0010, 0xfd20, 0x07ff
    };
    for (uint8_t i = 0; i < LVDS_PALETTE_SLOTS; i++) {
        g_palette_rgb565[i] = defaults[i];
        g_palette_used[i] = false;
    }
    g_palette_used[0] = true;
    g_palette_next = 1;
    lvds_palette_reset();
    lvds_rebuild_lut();
}

#if LVDS_TARGET_BPP == 1
static void lvds_init_bit_expand(void)
{
    for (uint16_t value = 0; value < 256; value++) {
        uint32_t expanded = 0;

        for (uint8_t pixel = 0; pixel < 8; pixel++) {
            expanded |= (uint32_t)((value >> pixel) & 1u) <<
                        (pixel * 4u);
        }
        g_expand_1bpp[value] = expanded;
    }
}
#elif LVDS_TARGET_BPP == 2
static void lvds_init_bit_expand(void)
{
    for (uint16_t value = 0; value < 256; value++) {
        uint16_t expanded = 0;

        for (uint8_t pixel = 0; pixel < 4; pixel++) {
            expanded |= (uint16_t)((value >> (pixel * 2u)) & 3u) <<
                        (pixel * 4u);
        }
        g_expand_2bpp[value] = expanded;
    }
}
#endif

/* The palette taken again by a caller above the renderer - see
   `lvds_target.h`. A picture that brings its own colours hands them back
   through this, so the screen that follows is drawn with its own sixteen. */
void lvds_target_palette_reset(void)
{
    palette_reset();
}

static lvds_color_t palette_lookup(uint16_t rgb565)
{
    uint32_t best_dist = 0xffffffffu;
    uint8_t best = 0;

    for (uint8_t i = 0; i < LVDS_PALETTE_SLOTS; i++) {
        if (g_palette_used[i] && g_palette_rgb565[i] == rgb565) {
            return i;
        }
    }
    if (g_palette_next < LVDS_PALETTE_SLOTS) {
        uint8_t index = g_palette_next++;
        palette_set(index, rgb565);
        return index;
    }
    for (uint8_t i = 0; i < LVDS_PALETTE_SLOTS; i++) {
        int dr = (int)((g_palette_rgb565[i] >> 11) & 0x1f) - (int)((rgb565 >> 11) & 0x1f);
        int dg = (int)((g_palette_rgb565[i] >> 5) & 0x3f) - (int)((rgb565 >> 5) & 0x3f);
        int db = (int)(g_palette_rgb565[i] & 0x1f) - (int)(rgb565 & 0x1f);
        uint32_t dist = (uint32_t)(dr * dr * 2 + dg * dg + db * db * 2);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static void compute_line(uint32_t pixel_template, uint32_t *line, uint16_t from, uint16_t to)
{
    uint32_t px[7];
    px[0] = ((pixel_template << 0) & 0xFEFEFEFEu) | ((pixel_template >> 7) & 0x01010101u);
    px[1] = ((pixel_template << 1) & 0xFCFCFCFCu) | ((pixel_template >> 6) & 0x03030303u);
    px[2] = ((pixel_template << 2) & 0xF8F8F8F8u) | ((pixel_template >> 5) & 0x07070707u);
    px[3] = ((pixel_template << 3) & 0xF0F0F0F0u) | ((pixel_template >> 4) & 0x0F0F0F0Fu);
    px[4] = ((pixel_template << 4) & 0xE0E0E0E0u) | ((pixel_template >> 3) & 0x1F1F1F1Fu);
    px[5] = ((pixel_template << 5) & 0xC0C0C0C0u) | ((pixel_template >> 2) & 0x3F3F3F3Fu);
    px[6] = ((pixel_template << 6) & 0x80808080u) | ((pixel_template >> 1) & 0x7F7F7F7Fu);
    for (uint16_t i = from; i < to; i++) {
        line[i] = px[i % 7];
    }
}

static void init_lines(void)
{
    compute_line(LVDS_HSTX_INACTIVE, g_inactive_line, 0, LVDS_HSTX_H_FRONT_TRANSFERS);
    for (uint8_t i = 0; i < LVDS_HSTX_ACTIVE_LINE_BUFFERS; i++) {
        compute_line(LVDS_HSTX_INACTIVE, g_active_line[i], 0, LVDS_HSTX_H_FRONT_TRANSFERS);
    }
    compute_line(LVDS_HSTX_INACTIVE_VS, g_inactive_line_vs, 0, LVDS_HSTX_H_FRONT_TRANSFERS);

    compute_line(LVDS_HSTX_INACTIVE_HS, g_inactive_line, LVDS_HSTX_H_FRONT_TRANSFERS, LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS);
    for (uint8_t i = 0; i < LVDS_HSTX_ACTIVE_LINE_BUFFERS; i++) {
        compute_line(LVDS_HSTX_INACTIVE_HS, g_active_line[i], LVDS_HSTX_H_FRONT_TRANSFERS, LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS);
    }
    compute_line(LVDS_HSTX_INACTIVE_HSVS, g_inactive_line_vs, LVDS_HSTX_H_FRONT_TRANSFERS, LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS);

    compute_line(LVDS_HSTX_INACTIVE, g_inactive_line, LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS, LVDS_HSTX_H_INACTIVE_TRANSFERS);
    for (uint8_t i = 0; i < LVDS_HSTX_ACTIVE_LINE_BUFFERS; i++) {
        compute_line(LVDS_HSTX_INACTIVE, g_active_line[i], LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS, LVDS_HSTX_H_INACTIVE_TRANSFERS);
    }
    compute_line(LVDS_HSTX_INACTIVE_VS, g_inactive_line_vs, LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS, LVDS_HSTX_H_INACTIVE_TRANSFERS);

    compute_line(LVDS_HSTX_INACTIVE, g_inactive_line, LVDS_HSTX_H_INACTIVE_TRANSFERS, LVDS_HSTX_LINE_TRANSFERS);
    compute_line(LVDS_HSTX_INACTIVE_VS, g_inactive_line_vs, LVDS_HSTX_H_INACTIVE_TRANSFERS, LVDS_HSTX_LINE_TRANSFERS);
}

static inline void __attribute__((always_inline))
lvds_store_eight_pixels(uint32_t eight_pixels, uint32_t *line, uint16_t i)
{
    line[i + 0] = g_colour_lut[0][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 1] = g_colour_lut[1][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 2] = g_colour_lut[2][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 3] = g_colour_lut[3][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 4] = g_colour_lut[4][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 5] = g_colour_lut[5][eight_pixels & 0xffu];
    eight_pixels >>= 4;
    line[i + 6] = g_colour_lut[6][eight_pixels & 0xffu];
}

static inline void __attribute__((always_inline))
compute_active_line_to(uint16_t y, uint32_t *line)
{
    uint32_t base = (uint32_t)y * LVDS_DRAW_STRIDE;
    uint16_t offset = 0;
    for (uint16_t i = LVDS_HSTX_H_INACTIVE_TRANSFERS; i < LVDS_HSTX_LINE_TRANSFERS; i += 7) {
#if LVDS_TARGET_BPP == 1
        const uint8_t *packed_byte = g_framebuffer + base + offset;
        uint32_t eight_pixels = g_expand_1bpp[*packed_byte];
        offset++;
#elif LVDS_TARGET_BPP == 2
        const uint8_t *packed_bytes = g_framebuffer + base + 2u * offset;
        uint32_t eight_pixels =
            (uint32_t)g_expand_2bpp[packed_bytes[0]] |
            ((uint32_t)g_expand_2bpp[packed_bytes[1]] << 16);
        offset++;
#else
        uint32_t eight_pixels = *(const uint32_t *)(g_framebuffer + base + 4u * offset++);
#endif
        lvds_store_eight_pixels(eight_pixels, line, i);
    }
}

static inline void __attribute__((always_inline))
compute_active_line(uint16_t y, uint8_t buffer_index)
{
    compute_active_line_to(y,
                           g_active_line[buffer_index % LVDS_HSTX_ACTIVE_LINE_BUFFERS]);
}

#if LVDS_HEADLESS_LOWRES
static uint32_t lvds_repeat_four_nibbles(uint8_t colour)
{
    colour &= 0x0fu;
    return (uint32_t)colour * 0x00001111u;
}

static inline void __attribute__((always_inline))
compute_lowres_active_line_to(uint16_t y, uint32_t *line)
{
    const uint8_t *row = g_framebuffer + (uint32_t)y * LVDS_DRAW_STRIDE;

    for (uint16_t group = 0; group < (LVDS_DRAW_WIDTH / 2u); group++) {
        uint8_t packed = row[group];
        uint32_t left = lvds_repeat_four_nibbles(packed & 0x0fu);
        uint32_t right = lvds_repeat_four_nibbles(packed >> 4);
        lvds_store_eight_pixels(left | (right << 16), line,
                                LVDS_HSTX_H_INACTIVE_TRANSFERS + group * 7u);
    }
}
#endif

#if LVDS_HEADLESS

/* In steady state, the active-encoded line immediately before the active area
   points at slot zero. Slot zero holds the last active row prepared into it:
   four rows before the end of the 604-row frame. */
#define LVDS_HSTX_CLOSE_ROW \
    (LVDS_FRAME_HEIGHT - LVDS_HSTX_ACTIVE_LINE_BUFFERS)

static bool lvds_range_overlaps_static_framebuffer(const void *dst, size_t bytes)
{
    uintptr_t start = (uintptr_t)dst;
    uintptr_t end = start + bytes;
    uintptr_t fb_start = (uintptr_t)g_framebuffer;
    uintptr_t fb_end = fb_start + sizeof(g_framebuffer_block);

    return start < fb_end && fb_start < end;
}

static void lvds_bake_active_row(uint16_t y, uint32_t *line)
{
    compute_line(LVDS_HSTX_INACTIVE, line, 0, LVDS_HSTX_H_FRONT_TRANSFERS);
    compute_line(LVDS_HSTX_INACTIVE_HS, line,
                 LVDS_HSTX_H_FRONT_TRANSFERS,
                 LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS);
    compute_line(LVDS_HSTX_INACTIVE, line,
                 LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS,
                 LVDS_HSTX_H_INACTIVE_TRANSFERS);
    compute_active_line_to(y, line);
}

static void lvds_bake_inactive_row(bool vsync, uint32_t *line)
{
    uint32_t front = vsync ? LVDS_HSTX_INACTIVE_VS : LVDS_HSTX_INACTIVE;
    uint32_t sync = vsync ? LVDS_HSTX_INACTIVE_HSVS : LVDS_HSTX_INACTIVE_HS;

    compute_line(front, line, 0, LVDS_HSTX_H_FRONT_TRANSFERS);
    compute_line(sync, line,
                 LVDS_HSTX_H_FRONT_TRANSFERS,
                 LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS);
    compute_line(front, line,
                 LVDS_HSTX_H_FRONT_TRANSFERS + LVDS_HSTX_H_SYNC_TRANSFERS,
                 LVDS_HSTX_LINE_TRANSFERS);
}

static void lvds_bake_scanline(uint32_t v, uint32_t *out)
{
    uint32_t *line = out + 1;

    out[0] = LVDS_HSTX_CMD_RAW | LVDS_HSTX_LINE_TRANSFERS;

    if (v < LVDS_HSTX_V_FRONT_PORCH) {
        lvds_bake_inactive_row(false, line);
    } else if (v < LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH) {
        lvds_bake_inactive_row(true, line);
    } else if (v < LVDS_HSTX_V_INACTIVE) {
        if (v == LVDS_HSTX_V_INACTIVE - 1u) {
            lvds_bake_active_row(LVDS_HSTX_CLOSE_ROW, line);
        } else {
            lvds_bake_inactive_row(false, line);
        }
    } else {
        lvds_bake_active_row((uint16_t)(v - LVDS_HSTX_V_INACTIVE), line);
    }
}

size_t rp2350_panel_bake_line(int y, uint8_t *dst)
{
    uint32_t *out;

    if (!dst || ((uintptr_t)dst & 3u) ||
        (unsigned)y >= (unsigned)LVDS_FRAME_HEIGHT ||
        lvds_range_overlaps_static_framebuffer(dst,
                                               RP2350_PANEL_BAKE_LINE_BYTES)) {
        return 0;
    }
    out = (uint32_t *)dst;
    out[0] = LVDS_HSTX_CMD_RAW | LVDS_HSTX_LINE_TRANSFERS;
    lvds_bake_active_row((uint16_t)y, out + 1);
    return RP2350_PANEL_BAKE_LINE_BYTES;
}

#endif /* LVDS_HEADLESS */

static void init_hstx(void);

static uint32_t lvds_hstx_line_addr_for_v(uint32_t v, bool *active_video, uint16_t *active_y)
{
    if (active_video) {
        *active_video = false;
    }
    if (active_y) {
        *active_y = 0xffffu;
    }

    if (v >= LVDS_HSTX_V_TOTAL) {
        return (uint32_t)(uintptr_t)g_inactive_line;
    }

    if ((v < LVDS_HSTX_V_FRONT_PORCH) ||
        ((v >= LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH) &&
        (v < LVDS_HSTX_V_INACTIVE))) {
        if (v == (LVDS_HSTX_V_INACTIVE - 1)) {
            /*
             * An active-encoded line inside the vertical blanking, one line
             * before the active region: the buffer carries DE, so a panel that
             * displays blanking lines shows this buffer's content above the
             * frame. Kept as it has always been; change it only with a panel
             * check - and 2026-10-04's two bench probes narrowed exactly this
             * line to the "close of the frame" fault (`README.md`, "Panel
             * Alignment"): its buffer is *stale*, `g_active_line[0]` holding
             * the last row computed into slot 0 (row 596 of a 600-row frame),
             * so four more *active* lines move that content onto a blank extra
             * line while four more *blanking* lines do not.
             *
             * The panel check was made: handing this line the inactive buffer -
             * true blanking, nothing of the picture anywhere in the vertical
             * blanking - changed nothing on the glass, so this line's content
             * was never what the panel showed. It stays as it was: the fault is
             * on the active area's own last lines, and the probe for that is
             * the marker band in `ncc`'s frame edges.
             */
            return (uint32_t)(uintptr_t)g_active_line[0];
        }
        return (uint32_t)(uintptr_t)g_inactive_line;
    }

    if ((v >= LVDS_HSTX_V_FRONT_PORCH) &&
        (v < LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH)) {
        return (uint32_t)(uintptr_t)g_inactive_line_vs;
    }

    uint16_t y = (uint16_t)(v - LVDS_HSTX_V_INACTIVE);
    if (active_video) {
        *active_video = true;
    }
    if (active_y) {
        *active_y = y;
    }
    return (uint32_t)(uintptr_t)g_active_line[y % LVDS_HSTX_ACTIVE_LINE_BUFFERS];
}

static void lvds_hstx_build_descriptor_ring(void)
{
    for (uint32_t i = 0; i < LVDS_HSTX_DESCRIPTOR_RING_LINES; i++) {
        if (i < LVDS_HSTX_V_TOTAL) {
            g_scanout_line_addr_ring[i] = lvds_hstx_line_addr_for_v(i, NULL, NULL);
        } else {
            /*
             * The DMA ring is intentionally larger than the panel frame so its
             * read pointer can wrap safely.  These tail descriptors are guard
             * space only; if core1 is late resetting the frame boundary, show a
             * quiet blank line instead of repeating active video.
             */
            g_scanout_line_addr_ring[i] = (uint32_t)(uintptr_t)g_inactive_line;
        }
    }
}

static void __no_inline_not_in_flash_func(lvds_hstx_descriptor_refill)(void)
{
    uintptr_t base = (uintptr_t)g_scanout_line_addr_ring;
    uintptr_t read = (uintptr_t)dma_hw->ch[LVDS_HSTX_DMACH_CTRL].read_addr;
    uint32_t index = (uint32_t)(((read - base) & ((LVDS_HSTX_DESCRIPTOR_RING_LINES * sizeof(uint32_t)) - 1u)) / sizeof(uint32_t));

    if (index >= LVDS_HSTX_V_TOTAL) {
        dma_hw->ch[LVDS_HSTX_DMACH_CTRL].read_addr = (uint32_t)base;
        g_descriptor_last_index = 0xffffffffu;
        index = 0;
    }

    if (index == g_descriptor_last_index) {
        tight_loop_contents();
        return;
    }

    g_descriptor_last_index = index;

    if (index < LVDS_HSTX_V_TOTAL) {
        /*
         * Prepare the line the ring reaches LVDS_HSTX_ACTIVE_LINE_BUFFERS - 1
         * lines later, wrapping into the next frame. Preparing from the current
         * descriptor's y instead, and stopping at the image edge, left the
         * first lines of every frame holding the tail of the previous one:
         * they are reached from the vertical blanking, where no line was
         * prepared at all.
         */
        uint32_t ahead = index + (LVDS_HSTX_ACTIVE_LINE_BUFFERS - 1u);
        bool active_video;
        uint16_t y;

        if (ahead >= LVDS_HSTX_V_TOTAL) {
            ahead -= LVDS_HSTX_V_TOTAL;
        }
        (void)lvds_hstx_line_addr_for_v(ahead, &active_video, &y);
        if (active_video) {
            compute_active_line(y, (uint8_t)(y % LVDS_HSTX_ACTIVE_LINE_BUFFERS));
        }
    }
}

static void __no_inline_not_in_flash_func(poll_dma_scanout)(void)
{
    // Core1/HSTX realtime path: keep permanent buffers static/aligned and do
    // not add large automatic locals here. Stack overflow can corrupt DMA
    // descriptors/control state silently before the main core notices.
    const uint32_t data_bit = 1u << LVDS_HSTX_DMACH_DATA;
    const uint32_t ctrl_bit = 1u << LVDS_HSTX_DMACH_CTRL;
    const uint32_t scanout_bits = data_bit | ctrl_bit;

    while (1) {
        dma_hw->intr = dma_hw->intr & scanout_bits;
        lvds_hstx_descriptor_refill();
    }
}

/* Take the scanout's two channels from the allocator. A false return means the
   board has no complete scanout, which the caller has to see. */
static bool lvds_claim_scanout_dma(void)
{
    int channels[2];
    const int count = 2;

    for (int i = 0; i < count; i++) {
        channels[i] = dma_claim_unused_channel(false);
        if (channels[i] < 0) {
            while (i > 0) {
                dma_channel_unclaim((uint)channels[--i]);
            }
            return false;
        }
    }
    g_dma_data_channel = (uint)channels[0];
    g_dma_ctrl_channel = (uint)channels[1];
    return true;
}

static void lvds_hstx_configure_pins(void)
{
    // Ascending LVDS pinout from the HAL config.
    static const int lane_to_output_bit[4] = {0, 2, 4, 6};
    static const uint8_t hstx_pins[8] = {
        LVDS_HSTX_D0P, LVDS_HSTX_D0M,
        LVDS_HSTX_D1P, LVDS_HSTX_D1M,
        LVDS_HSTX_D2P, LVDS_HSTX_D2M,
        LVDS_HSTX_CLKP, LVDS_HSTX_CLKM
    };

    for (uint lane = 0; lane < 4; lane++) {
        int bit = lane_to_output_bit[lane];
        uint32_t sel = (lane * 8 + 7) << HSTX_CTRL_BIT0_SEL_P_LSB |
                       (lane * 8 + 6) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit] = sel;
        hstx_ctrl_hw->bit[bit + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
    }
    for (uint i = 0; i < 8; i++) {
        gpio_set_function(hstx_pins[i], 0);
    }
}

static void init_hstx(void)
{
    const uint32_t data_bit = 1u << LVDS_HSTX_DMACH_DATA;
    const uint32_t ctrl_bit = 1u << LVDS_HSTX_DMACH_CTRL;
    const uint32_t scanout_bits = data_bit | ctrl_bit;

    g_descriptor_last_index = 0xffffffffu;
    for (uint8_t i = 0; i < LVDS_HSTX_ACTIVE_LINE_BUFFERS; i++) {
        compute_active_line(i, i);
    }
    lvds_hstx_build_descriptor_ring();

    irq_set_enabled(DMA_IRQ_1, false);
    dma_channel_abort(LVDS_HSTX_DMACH_DATA);
    dma_channel_abort(LVDS_HSTX_DMACH_CTRL);
    dma_hw->inte1 &= ~scanout_bits;
    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;

    /* Both channels were claimed on core0 by `lvds_claim_scanout_dma` before
       this core was started; nothing here takes or releases a claim. */

    hstx_ctrl_hw->csr = 0;
    *((volatile uint32_t *)(0x40010000u + 0x58u)) = ((uint32_t)LVDS_HSTX_CLOCK_DIV << 16u);
    hstx_ctrl_hw->csr =
        4u << HSTX_CTRL_CSR_CLKDIV_LSB |
        4u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        30u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    lvds_hstx_configure_pins();

    dma_channel_config c = dma_channel_get_default_config(LVDS_HSTX_DMACH_DATA);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_CTRL);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(LVDS_HSTX_DMACH_DATA,
                          &c,
                          &hstx_fifo_hw->fifo,
                          (const void *)(uintptr_t)g_scanout_line_addr_ring[0],
                          LVDS_HSTX_LINE_TRANSFERS,
                          false);

    c = dma_channel_get_default_config(LVDS_HSTX_DMACH_CTRL);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_DATA);
    /* Control DMA only rewrites the FIFO DMA read_addr. It must not consume
       HSTX pacing slots; only the data channel is paced by DREQ_HSTX. */
    channel_config_set_dreq(&c, DREQ_FORCE);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 12);
    dma_channel_configure(LVDS_HSTX_DMACH_CTRL,
                          &c,
                          &dma_hw->ch[LVDS_HSTX_DMACH_DATA].read_addr,
                          &g_scanout_line_addr_ring[1],
                          1,
                          false);

    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;
    dma_hw->inte1 &= ~scanout_bits;
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    dma_channel_start(LVDS_HSTX_DMACH_DATA);
}

#if LVDS_HEADLESS

#if LVDS_HEADLESS_SRAM_BARS

bool rp2350_panel_headless_sram_bars_start(void)
{
    const uint32_t data_bit = 1u << LVDS_HSTX_DMACH_DATA;
    const uint32_t ctrl_bit = 1u << LVDS_HSTX_DMACH_CTRL;
    const uint32_t scanout_bits = data_bit | ctrl_bit;
    uint32_t *active = g_headless_active_sram.data;
    uint32_t *blank = g_headless_blank_sram.data;
    uint32_t *vsync = g_headless_vsync_sram.data;
    dma_channel_config c;

    if (g_display_started ||
        !rp2350_panel_bake_line(0, (uint8_t *)active)) {
        return false;
    }
    lvds_bake_scanline(0, blank);
    lvds_bake_scanline(LVDS_HSTX_V_FRONT_PORCH, vsync);

    for (uint32_t v = 0; v < LVDS_HSTX_DESCRIPTOR_RING_LINES; v++) {
        const uint32_t *line = blank;

        if (v < LVDS_HSTX_V_TOTAL) {
            if ((v >= LVDS_HSTX_V_FRONT_PORCH) &&
                (v < LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH)) {
                line = vsync;
            } else if (v >= LVDS_HSTX_V_INACTIVE) {
                line = active;
            } else if (v == LVDS_HSTX_V_INACTIVE - 1u) {
                line = active;
            }
        }
        g_scanout_line_addr_ring[v] = (uint32_t)(uintptr_t)line;
    }

    irq_set_enabled(DMA_IRQ_1, false);
    dma_channel_abort(LVDS_HSTX_DMACH_DATA);
    dma_channel_abort(LVDS_HSTX_DMACH_CTRL);
    dma_hw->inte1 &= ~scanout_bits;
    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;

    hstx_ctrl_hw->csr = 0;
    *((volatile uint32_t *)(0x40010000u + 0x58u)) =
        ((uint32_t)LVDS_HSTX_CLOCK_DIV << 16u);
    hstx_ctrl_hw->expand_shift =
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        4u << HSTX_CTRL_CSR_CLKDIV_LSB |
        4u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        30u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;
    lvds_hstx_configure_pins();

    c = dma_channel_get_default_config(LVDS_HSTX_DMACH_DATA);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_CTRL);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(LVDS_HSTX_DMACH_DATA,
                          &c,
                          &hstx_fifo_hw->fifo,
                          (const void *)(uintptr_t)g_scanout_line_addr_ring[0],
                          RP2350_PANEL_BAKE_LINE_WORDS,
                          false);

    c = dma_channel_get_default_config(LVDS_HSTX_DMACH_CTRL);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_DATA);
    channel_config_set_dreq(&c, DREQ_FORCE);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 12);
    dma_channel_configure(LVDS_HSTX_DMACH_CTRL,
                          &c,
                          &dma_hw->ch[LVDS_HSTX_DMACH_DATA].read_addr,
                          &g_scanout_line_addr_ring[1],
                          1,
                          false);

    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;
    bus_ctrl_hw->priority =
        BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    __dmb();
    dma_channel_start(LVDS_HSTX_DMACH_DATA);
    g_display_started = true;
    LVDS_PORT_LOG("lvds: SRAM bar lines looping, core1 idle\n");
    return true;
}

#endif /* LVDS_HEADLESS_SRAM_BARS */

#if LVDS_HEADLESS_LOWRES

static void lvds_lowres_build_rows(void)
{
    for (uint32_t y = 0; y < LVDS_DRAW_HEIGHT; y++) {
        uint32_t *stream = g_lowres_rows.data[y];
        uint32_t *line = stream + 1;

        stream[0] = LVDS_HSTX_CMD_RAW | LVDS_HSTX_LINE_TRANSFERS;
        compute_line(LVDS_HSTX_INACTIVE, line, 0,
                     LVDS_HSTX_H_FRONT_TRANSFERS);
        compute_line(LVDS_HSTX_INACTIVE_HS, line,
                     LVDS_HSTX_H_FRONT_TRANSFERS,
                     LVDS_HSTX_H_FRONT_TRANSFERS +
                     LVDS_HSTX_H_SYNC_TRANSFERS);
        compute_line(LVDS_HSTX_INACTIVE, line,
                     LVDS_HSTX_H_FRONT_TRANSFERS +
                     LVDS_HSTX_H_SYNC_TRANSFERS,
                     LVDS_HSTX_H_INACTIVE_TRANSFERS);
        compute_lowres_active_line_to((uint16_t)y, line);
    }
}

bool rp2350_panel_headless_lowres_can_rebake(void)
{
    uintptr_t base = (uintptr_t)g_scanout_line_addr_ring;
    uintptr_t read;
    uint32_t index;

    if (!g_display_started) {
        return false;
    }
    read = (uintptr_t)dma_hw->ch[LVDS_HSTX_DMACH_CTRL].read_addr;
    index = (uint32_t)(((read - base) &
                        (LVDS_HSTX_DESCRIPTOR_RING_LINES * sizeof(uint32_t) - 1u)) /
                       sizeof(uint32_t));
    return index >= LVDS_HSTX_V_TOTAL;
}

void rp2350_panel_headless_lowres_rebake(void)
{
    if (g_display_started) {
        lvds_lowres_build_rows();
        __dmb();
    }
}

bool rp2350_panel_headless_lowres_start(void)
{
    const uint32_t data_bit = 1u << LVDS_HSTX_DMACH_DATA;
    const uint32_t ctrl_bit = 1u << LVDS_HSTX_DMACH_CTRL;
    const uint32_t scanout_bits = data_bit | ctrl_bit;
    uint32_t *blank = g_lowres_blank.data;
    uint32_t *vs_line = g_lowres_vs.data;
    dma_channel_config c;

    if (g_display_started) {
        return false;
    }

    lvds_bake_scanline(0, blank);
    lvds_bake_scanline(LVDS_HSTX_V_FRONT_PORCH, vs_line);
    lvds_lowres_build_rows();

    for (uint32_t v = 0; v < LVDS_HSTX_DESCRIPTOR_RING_LINES; v++) {
        bool in_frame = v < LVDS_HSTX_V_TOTAL;
        bool vsync = in_frame &&
                     v >= LVDS_HSTX_V_FRONT_PORCH &&
                     v < LVDS_HSTX_V_FRONT_PORCH + LVDS_HSTX_V_SYNC_WIDTH;
        const uint32_t *line = vsync ? vs_line : blank;

        if (in_frame && v >= LVDS_HSTX_V_INACTIVE) {
            uint32_t y = v - LVDS_HSTX_V_INACTIVE;
            if (y < LVDS_DRAW_HEIGHT * 5u) {
                line = g_lowres_rows.data[y / 5u];
            }
        }
        g_scanout_line_addr_ring[v] = (uint32_t)(uintptr_t)line;
    }

    irq_set_enabled(DMA_IRQ_1, false);
    dma_channel_abort(LVDS_HSTX_DMACH_DATA);
    dma_channel_abort(LVDS_HSTX_DMACH_CTRL);
    dma_hw->inte1 &= ~scanout_bits;
    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;

    hstx_ctrl_hw->csr = 0;
    *((volatile uint32_t *)(0x40010000u + 0x58u)) =
        ((uint32_t)LVDS_HSTX_CLOCK_DIV << 16u);
    hstx_ctrl_hw->expand_shift =
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        4u << HSTX_CTRL_CSR_CLKDIV_LSB |
        4u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        30u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;
    lvds_hstx_configure_pins();

    c = dma_channel_get_default_config(LVDS_HSTX_DMACH_DATA);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_CTRL);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(LVDS_HSTX_DMACH_DATA,
                          &c,
                          &hstx_fifo_hw->fifo,
                          (const void *)(uintptr_t)g_scanout_line_addr_ring[0],
                          RP2350_PANEL_BAKE_LINE_WORDS,
                          false);

    c = dma_channel_get_default_config(LVDS_HSTX_DMACH_CTRL);
    channel_config_set_chain_to(&c, LVDS_HSTX_DMACH_DATA);
    channel_config_set_dreq(&c, DREQ_FORCE);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 12);
    dma_channel_configure(LVDS_HSTX_DMACH_CTRL,
                          &c,
                          &dma_hw->ch[LVDS_HSTX_DMACH_DATA].read_addr,
                          &g_scanout_line_addr_ring[1],
                          1,
                          false);

    dma_hw->ints1 = scanout_bits;
    dma_hw->intr = scanout_bits;
    bus_ctrl_hw->priority =
        BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    __dmb();
    dma_channel_start(LVDS_HSTX_DMACH_DATA);
    g_display_started = true;
    LVDS_PORT_LOG("lvds: 200x120 4-colour full-scale stream, core1 idle\n");
    return true;
}

#endif /* LVDS_HEADLESS_LOWRES */

#endif /* LVDS_HEADLESS */

static void __no_inline_not_in_flash_func(core1_entry)(void)
{
    init_hstx();
    g_core1_ready = true;
    poll_dma_scanout();
}

static void configure_lvds_clocks(void)
{
#if LVDS_TARGET_USE_HSTX_PLL
    uint32_t hstx_hz = (uint32_t)LVDS_HSTX_PLL_KHZ * 1000u;
    uint32_t sys_hz = (uint32_t)LVDS_HSTX_SYS_CLOCK_KHZ * 1000u;

    set_sys_clock_pll(hstx_hz * 5u, 5, 1);
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    hstx_hz,
                    sys_hz);
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    hstx_hz,
                    hstx_hz);
#else
    set_sys_clock_khz(LVDS_HSTX_SYS_CLOCK_KHZ, false);
#endif
}

static bool target_ready_for_draw(void);

static inline void __attribute__((always_inline))
lvds_framebuffer_store(uint8_t *row, int x, uint8_t colour)
{
    uint32_t byte = (uint32_t)x / LVDS_PIXELS_PER_BYTE;
    uint32_t shift = ((uint32_t)x % LVDS_PIXELS_PER_BYTE) * LVDS_TARGET_BPP;
    uint8_t mask = (uint8_t)(LVDS_COLOUR_MASK << shift);

    row[byte] = (uint8_t)((row[byte] & (uint8_t)~mask) |
                          (uint8_t)((colour & LVDS_COLOUR_MASK) << shift));
}

static uint8_t lvds_framebuffer_pack(uint8_t colour)
{
    uint8_t packed = 0;

    colour &= LVDS_COLOUR_MASK;
    for (uint8_t i = 0; i < LVDS_PIXELS_PER_BYTE; i++) {
        packed |= (uint8_t)(colour << (i * LVDS_TARGET_BPP));
    }
    return packed;
}

/* One pixel of the source raster. In the normal build this is the panel; in
   the low-resolution probe it is the 200x120 indexed source. */
void lvds_target_pixel(int x, int y, lvds_color_t color)
{
    uint8_t c = (uint8_t)(color & LVDS_COLOUR_MASK);

    if (!target_ready_for_draw() ||
        (unsigned)x >= (unsigned)LVDS_DRAW_WIDTH ||
        (unsigned)y >= (unsigned)LVDS_DRAW_HEIGHT) {
        return;
    }
    lvds_framebuffer_store(g_draw_buffer + (uint32_t)y * LVDS_DRAW_STRIDE,
                           x, c);
    g_dirty = true;
}

static bool target_ready_for_draw(void)
{
    return g_draw_buffer != NULL;
}

static bool target_coord_sane(int v)
{
    return v >= -LVDS_HSTX_COORD_GUARD && v <= LVDS_HSTX_COORD_GUARD;
}

static bool target_rect_sane(int x, int y, int w, int h)
{
    if (!target_ready_for_draw() || w <= 0 || h <= 0)
        return false;
    if (!target_coord_sane(x) || !target_coord_sane(y) ||
        !target_coord_sane(w) || !target_coord_sane(h))
        return false;
    /* This is the glass, not the view: the core has already turned the
       rectangle and clipped it to the panel. */
    return x < LVDS_DRAW_WIDTH && y < LVDS_DRAW_HEIGHT &&
           x + w > 0 && y + h > 0;
}

bool lvds_target_init(void)
{
    if (!lvds_claim_scanout_dma()) {
        LVDS_PORT_LOG("lvds: no free DMA channel for the scanout\n");
        return false;
    }
#if LVDS_TARGET_USE_PSRAM
    /* The PSRAM has to be up before the draw buffer can live in it, and the
       machine's order - PSRAM first, then the panel's clocks - is the one that
       was measured. A board without PSRAM reports it unavailable and the draw
       buffer stays in SRAM. */
    (void)lvds_psram_init();
#endif
    palette_reset();
#if LVDS_TARGET_BPP == 1 || LVDS_TARGET_BPP == 2
    lvds_init_bit_expand();
#endif
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    g_draw_buffer = g_framebuffer;
    g_backbuffer_active = false;
    g_dirty = false;
#if LVDS_TARGET_USE_PSRAM
    if (lvds_psram_available()) {
        g_draw_buffer = (uint8_t *)lvds_psram_ptr(0);
        memset(g_draw_buffer, 0, LVDS_FRAME_BYTES);
        g_backbuffer_active = true;
    }
#endif
#if !LVDS_HEADLESS_LOWRES
    init_lines();
    for (uint8_t i = 0; i < LVDS_HSTX_ACTIVE_LINE_BUFFERS; i++) {
        compute_active_line(i, i);
    }
#endif
    configure_lvds_clocks();
    g_core1_ready = false;
    multicore_reset_core1();
#if !LVDS_HEADLESS
    lvds_hstx_launch_core1();
#endif
    LVDS_PORT_LOG("lvds: panel %dx%d, frame %dx%d, turn %d\n",
                  LVDS_WIDTH, LVDS_HEIGHT, LVDS_FRAME_WIDTH, LVDS_FRAME_HEIGHT,
                  LVDS_PANEL_TURN);
#if !LVDS_HEADLESS
#if LVDS_HSTX_CORE1_READY_TIMEOUT_MS > 0
    {
        uint32_t start_ms = LVDS_PORT_MILLIS();
        while (!g_core1_ready) {
            if ((uint32_t)(LVDS_PORT_MILLIS() - start_ms) >= (uint32_t)LVDS_HSTX_CORE1_READY_TIMEOUT_MS) {
                return false;
            }
            tight_loop_contents();
        }
    }
#else
    while (!g_core1_ready) {
        tight_loop_contents();
    }
#endif
    g_display_started = true;
    LVDS_PORT_LOG("lvds: scanout up, draw buffer %s\n",
                  g_backbuffer_active ? "psram" : "sram");
#else
    LVDS_PORT_LOG("lvds: core1 idle, waiting for a baked frame\n");
#endif
    lvds_target_clear(0);
    return true;
}

void lvds_target_clear(lvds_color_t color)
{
    uint8_t c = (uint8_t)(color & LVDS_COLOUR_MASK);
    if (!target_ready_for_draw()) {
        return;
    }
    memset(g_draw_buffer, lvds_framebuffer_pack(c), LVDS_FRAME_BYTES);
    g_dirty = true;
}

/* A row of the caller's own pixels: indices, in panel coordinates. The
   framebuffer packs 8 / LVDS_TARGET_BPP pixels per byte. */
void lvds_target_blit(int x, int y, int w, const uint8_t *indices)
{
    uint32_t row;
    int i = 0;

    if (!target_ready_for_draw() || !indices || w <= 0 ||
        (unsigned)y >= (unsigned)LVDS_DRAW_HEIGHT) {
        return;
    }
    if (x < 0) {
        i = -x;
        w -= i;
        x = 0;
    }
    if (x >= (int)LVDS_DRAW_WIDTH || w <= 0) {
        return;
    }
    if (x + w > (int)LVDS_DRAW_WIDTH) {
        w = (int)LVDS_DRAW_WIDTH - x;
    }
    if (w <= 0) {
        return;
    }

    row = (uint32_t)y * LVDS_DRAW_STRIDE;
    for (int n = 0; n < w; n++) {
        lvds_framebuffer_store(g_draw_buffer + row, x + n,
                               (uint8_t)(indices[i + n] & LVDS_COLOUR_MASK));
    }
    g_dirty = true;
}

void lvds_target_fill_rect(int x, int y, int w, int h, lvds_color_t color)
{
    int x2;
    int y2;
    uint8_t c = (uint8_t)(color & LVDS_COLOUR_MASK);
    uint8_t packed = lvds_framebuffer_pack(c);

    if (!target_rect_sane(x, y, w, h)) {
        return;
    }
    /* The rectangle arrives in **panel** coordinates: the core turned it and
       clipped it, and this is the one place a whole fill is written, so a
       screen clear is one pass and not one pixel at a time. */
    x2 = x + w;
    y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > LVDS_DRAW_WIDTH) x2 = LVDS_DRAW_WIDTH;
    if (y2 > LVDS_DRAW_HEIGHT) y2 = LVDS_DRAW_HEIGHT;
    if (x >= x2 || y >= y2) return;

    for (int yy = y; yy < y2; yy++) {
        uint8_t *row = g_draw_buffer + (uint32_t)yy * LVDS_DRAW_STRIDE;
        int left = x;
        int right = x2;

        while (left < right && (left % LVDS_PIXELS_PER_BYTE) != 0) {
            lvds_framebuffer_store(row, left, c);
            left++;
        }

        while (right > left && (right % LVDS_PIXELS_PER_BYTE) != 0) {
            right--;
            lvds_framebuffer_store(row, right, c);
        }

        if (left < right) {
            memset(row + left / LVDS_PIXELS_PER_BYTE, packed,
                   (size_t)(right - left) / LVDS_PIXELS_PER_BYTE);
        }
    }
    g_dirty = true;
}

void lvds_target_present(void)
{
    if (LVDS_PORT_IO_CRITICAL()) {
        return;
    }
    if (g_backbuffer_active && g_dirty) {
        memcpy(g_framebuffer, g_draw_buffer, LVDS_FRAME_BYTES);
        g_dirty = false;
    }
    (void)g_display_started;
}

lvds_color_t lvds_target_slot(uint16_t rgb565)
{
    return palette_lookup(rgb565);
}

/* Re-colour a slot already handed out: no new slot is taken, the LUT is
   rebuilt, and core1 picks the new colour up on the next line it prepares.
   This is what makes a palette switch instant and free of churn. */
void lvds_target_slot_set(lvds_color_t slot, uint16_t rgb565)
{
    palette_set((uint8_t)(slot & LVDS_COLOUR_MASK), rgb565);
}

bool rp2350_panel_backbuffer_in_psram(void)
{
    return g_backbuffer_active;
}
