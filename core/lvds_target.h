#ifndef LVDS_TARGET_H
#define LVDS_TARGET_H

#include <stdbool.h>

#include "lvds_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The seam. Everything above this file is device independent; everything below
   it is one panel on one board. A target implements these calls and nothing
   else, and the core never asks what it is talking to:

     - `lvds_target_init` sets the panel up and returns false if it cannot (no
       clock lock, no PSRAM, no descriptor ring - whatever the target's own
       reasons are). Called once, by `lvds_canvas_init()`.
     - `lvds_target_pixel` writes one pixel of the panel, already inside
       `0 <= x < LVDS_WIDTH`, `0 <= y < LVDS_HEIGHT` and already turned: the
       core does the view mapping and the bounds, the target only writes. This
       and `lvds_target_fill_rect` are the **only two calls that write a
       pixel**.
     - `lvds_target_fill_rect` writes an axis-aligned rectangle of the panel,
       already clipped to the glass.
     - `lvds_target_clear` fills the whole glass with one colour.
     - `lvds_target_present` makes the drawn picture the one the scanout reads.
       For a memory target that is bookkeeping; for the RP2350 it is the copy
       from the PSRAM draw buffer into the SRAM framebuffer, and it is
       deliberately a CPU copy (see `LIMITS.md`).
     - `lvds_target_slot` turns a 5-6-5 colour into whatever the target's
       drawing primitives take. An indexed target takes a palette slot here and
       owns the palette (sixteen slots, taken on first use); a 32bpp target
       hands the word back. This is the one call in the seam that is asked for
       its *result* rather than told what to draw.
     - `lvds_target_palette_reset` gives the palette back: after it, the next
       colour asked for takes the first slot again. A picture that brings
       colours of its own - a boot splash - calls it when it is done, or the
       screen that follows is drawn with what was left over. A target with no
       palette of its own resets nothing.

   There is no target-side knowledge of the view, the turn, the palette ids or
   the drawing primitives. There is no core-side knowledge of DMA, HSTX, PSRAM,
   or of whether the picture ends up on glass or in a file. */

bool lvds_target_init(void);
void lvds_target_pixel(int x, int y, lvds_color_t color);
void lvds_target_fill_rect(int x, int y, int w, int h, lvds_color_t color);
void lvds_target_clear(lvds_color_t color);
void lvds_target_present(void);
lvds_color_t lvds_target_slot(uint16_t rgb565);
void lvds_target_palette_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* LVDS_TARGET_H */
