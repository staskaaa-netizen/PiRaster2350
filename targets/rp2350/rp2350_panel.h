#ifndef RP2350_PANEL_H
#define RP2350_PANEL_H

#include <stdbool.h>

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

/* True when the draw buffer is in PSRAM rather than in SRAM. */
bool rp2350_panel_backbuffer_in_psram(void);

/* The frame the scanout is sending: the glass plus the spare active lines the
   panel's close needs. Named here so a sample can say it out loud. */
#define RP2350_PANEL_FRAME_WIDTH LVDS_WIDTH
#define RP2350_PANEL_FRAME_HEIGHT LVDS_PANEL_ACTIVE_HEIGHT

#ifdef __cplusplus
}
#endif

#endif /* RP2350_PANEL_H */
