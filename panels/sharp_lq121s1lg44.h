#ifndef LVDS_PANEL_SHARP_LQ121S1LG44_H
#define LVDS_PANEL_SHARP_LQ121S1LG44_H

/* The panel this project was written for, and the only one it has been on:
   a Sharp LQ121S1LG44, 800x600, single-channel LVDS, driven by an RP2350's
   HSTX through a PicoLVDS-style 7x256 scanline LUT.

   A panel header is data, not code: geometry, timing, pins and the clock
   numbers. Anything that reads them - the core's view mapping, the RP2350
   target's pacer - goes through the macros, so a second panel is a second
   header and nothing else.

   All of it is measured on the bench; the reasoning is in `LIMITS.md`. */

#define LVDS_PANEL_NAME "Sharp LQ121S1LG44"

/* The glass, and the frame that is sent to it.

   The frame carries four more **active** lines than the glass shows, and that
   tail is load-bearing: the panel repeats a line at the close of the active
   area, whatever content is on it (bench, 2026-10-04). The bench walked the
   active height down - 600 puts the repeat on the picture, 602 shows an extra
   black line, 603 and 604 are clean - so the frame keeps one line of margin
   over the three that are needed. The view, and so every screen, stays the
   glass's own 600x800 on the *first* 600 active lines.

   The root cause is still open. Do not shorten the frame. */
#define LVDS_WIDTH 800
#define LVDS_HEIGHT 600
#ifndef LVDS_PANEL_ACTIVE_HEIGHT
#define LVDS_PANEL_ACTIVE_HEIGHT 604
#endif

/* How the glass is bolted: 90 = its top edge ends up on the operator's right,
   so the picture is turned back a quarter and the operator reads 600x800.
   270 is the other way round, 0 an unturned panel. This is the one number that
   decides what "up" means for everything drawn above the core. */
#ifndef LVDS_PANEL_TURN
#define LVDS_PANEL_TURN 90
#endif

/* The scanout clock. The panel's nominal 40 MHz is what the machine runs at
   (280 MHz / div 2); it locks below that too. `420000/3` produced no video and
   is a board boundary, not a panel requirement - see `LIMITS.md` for the
   points that were measured. */
#define LVDS_SYS_CLOCK_KHZ 280000
#define LVDS_HSTX_PLL_KHZ 280000
#define LVDS_HSTX_CLOCK_DIV 2
#define LVDS_TARGET_USE_HSTX_PLL 0

/* The HSTX serial lanes, in the order the pacer drives them: data pair 0..2
   and the clock pair. */
#define LVDS_D0P 12
#define LVDS_D0M 13
#define LVDS_D1P 14
#define LVDS_D1M 15
#define LVDS_D2P 16
#define LVDS_D2M 17
#define LVDS_CLKP 18
#define LVDS_CLKM 19

/* Panel timing, in glass lines and pixels. The panel follows the active lines
   rather than counting from the sync - moving the back porch does not move the
   picture - so these are the values that were measured, not a preference. */
#define LVDS_H_FRONT_PORCH 40
#define LVDS_H_SYNC_WIDTH 128
#define LVDS_H_BACK_PORCH 88
#define LVDS_V_FRONT_PORCH 1
#define LVDS_V_SYNC_WIDTH 4
#define LVDS_V_BACK_PORCH 61

#endif /* LVDS_PANEL_SHARP_LQ121S1LG44_H */
