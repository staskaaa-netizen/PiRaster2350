#ifndef RP2350_PANEL_H
#define RP2350_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvds_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RP2350 target's own extras. The drawing side never needs this file: the
   seam is `lvds_target.h`. What is here is for a program that wants to say
   what the board brought up.

   `LVDS_TARGET_USE_PSRAM` is off by default - the smallest target is SRAM
   only. The machine's own build turns it on, and then the draw buffer is the
   first byte of PSRAM and `lvds_target_present()` copies it into the SRAM
   framebuffer the scanout reads. */

#ifndef LVDS_TARGET_USE_PSRAM
#define LVDS_TARGET_USE_PSRAM 0
#endif

/* The target's indexed framebuffer depth: 1bpp for green mono, 2bpp for four
   colours, or 4bpp for the normal sixteen-colour path. */
#ifndef LVDS_TARGET_BPP
#define LVDS_TARGET_BPP 4
#endif

/* True when the draw buffer is in PSRAM rather than in SRAM. */
bool rp2350_panel_backbuffer_in_psram(void);

/* The frame the scanout is sending: the glass plus the spare active lines the
   panel's close needs. Named here so a sample can say it out loud. */
#define RP2350_PANEL_FRAME_WIDTH LVDS_WIDTH
#define RP2350_PANEL_FRAME_HEIGHT LVDS_PANEL_ACTIVE_HEIGHT

#ifndef LVDS_HEADLESS
#define LVDS_HEADLESS 0
#endif

#ifndef LVDS_HEADLESS_SRAM_BARS
#define LVDS_HEADLESS_SRAM_BARS 0
#endif

#ifndef LVDS_HEADLESS_LOWRES
#define LVDS_HEADLESS_LOWRES 0
#endif

#if LVDS_HEADLESS

/* A baked scanline is one RAW command followed by the exact words core1
   would have prepared. The HSTX command count is 12 bits, so the whole
   LVDS line fits in one command. */
#define RP2350_PANEL_LINE_TRANSFERS \
    ((7u * (LVDS_WIDTH + LVDS_H_FRONT_PORCH + LVDS_H_SYNC_WIDTH + \
            LVDS_H_BACK_PORCH)) / 8u)
#define RP2350_PANEL_BAKE_LINE_WORDS (1u + RP2350_PANEL_LINE_TRANSFERS)
#define RP2350_PANEL_BAKE_LINE_BYTES (RP2350_PANEL_BAKE_LINE_WORDS * sizeof(uint32_t))

/* Bake active frame row `y` into a complete HSTX command stream. `dst` must
   hold `RP2350_PANEL_BAKE_LINE_BYTES`; the return value is the byte count, or
   zero when y or the destination is invalid. The palette LUT is part of the
   baked words: any palette change requires a new bake. */
size_t rp2350_panel_bake_line(int y, uint8_t *dst);

#if LVDS_HEADLESS_SRAM_BARS
/* The isolated Command Expander test: bake one colour-bar active line and the
   two blanking lines into SRAM, then reuse them through a 1,024-entry DMA
   descriptor ring. The 354 guard lines lengthen vertical blanking, so this is
   a mechanism probe, not a panel-timing configuration. */
bool rp2350_panel_headless_sram_bars_start(void);
#endif

#if LVDS_HEADLESS_LOWRES
#define RP2350_PANEL_LOWRES_WIDTH 200
#define RP2350_PANEL_LOWRES_HEIGHT 120

/* Full-screen nearest-neighbour 4x/5x test. The source remains indexed in
   SRAM; 120 pre-expanded output rows are selected by a segmented DMA ring. */
bool rp2350_panel_headless_lowres_start(void);

/* True only in the 354-line guard window after the active frame. Core0 may
   redraw the 200x120 source and then call `_rebake()` without racing the DMA
   while it reads active rows. */
bool rp2350_panel_headless_lowres_can_rebake(void);
void rp2350_panel_headless_lowres_rebake(void);
#endif

#endif /* LVDS_HEADLESS */

#ifdef __cplusplus
}
#endif

#endif /* RP2350_PANEL_H */
