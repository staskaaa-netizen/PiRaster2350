# Limits and caveats

What a consumer has to know before this is used somewhere new. Every item is
measured or observed on the bench, not assumed; the reasoning behind each one is
in the module's own notes (`uCNC/src/modules/lvds_renderer/README.md`).

## One panel

* **Sharp LQ121S1LG44, 800x600, single-channel LVDS**, driven by an RP2350's
  HSTX through a PicoLVDS-style 7x256 scanline LUT. It has been on that panel
  and no other. Another panel is another header and a target that can read it.
* Pins are GP12..GP19 (three data pairs and the clock pair).
* The packing emits one pixel clock per **7** serial transfer phases, so the
  pixel clock is `LVDS_SYS_CLOCK_KHZ / 1000 / LVDS_HSTX_CLOCK_DIV * 2 / 7`.
  The panel's 40 MHz nominal is not what it runs at here: it locks below that.
  Measured points: `280000/2` (~40.00 MHz, the machine's setting), `370000/3`
  (~35.24), `270000/3` (~25.71), `370000/4` (~26.43), `270000/4` (~19.29),
  `280000/3` (~26.67). **`420000/3` produced no video** and is a
  board/overclock boundary, not a panel requirement.
* When testing clocks, change the clock values only - never the scanout
  architecture in the same breath.
* The glass is mounted **turned a quarter** (`LVDS_PANEL_TURN 90`) and the
  operator reads 600x800. `270` and `0` are the other two ways round. A picture
  that comes out the wrong way up is that one number.

## The frame carries 604 active lines against a 600-line glass

**The four spare active lines at the close are load-bearing.** The bench put
five known colours on the frame's last five lines and one of them came out
**twice**: the close of the active area repeats a line, whatever content is on
it (2026-10-04). Four extra *blanking* lines changed nothing, so it is not the
frame's total, and it is not the clock.

Walking the active height down measured what is needed: **600** puts the repeat
on the picture, **602** shows an extra black line, **603** and **604** are
clean. The frame therefore keeps 604 - one line of margin over the three that
are needed - while the view, and every drawing, stays the glass's own 600x800
mapped onto the *first* 600 active lines.

The **root cause is still unknown**: the panel or the link repeats or eats a
line at the close of the active area. The tail absorbs it. Do not shorten the
frame, and do not treat the number as tunable.

## Sixteen colours, and no blending

* The framebuffer is **4bpp indexed**: sixteen colours, no anti-aliasing, no
  blending, no alpha. A colour that is not in the table comes out as the nearest
  colour that is.
* The table is **full**. A new colour means retiring one; the palette's names
  are the machine's own (`WHITE` is its stock, `WHITE_WARM` its ink).
* A slot is taken when a colour is first **used**, never when the table is
  parsed. That was a real fault: a picture that brought its own colours found
  the palette already filled and came out in the panel's greys (2026-09-27).
* A picture that brings colours of its own (a boot splash) must call
  `lvds_canvas_palette_reset()` when it is done, or the screen that follows is
  drawn in what it left behind.
* The **host target has no palette**: it hands the 5-6-5 word back and stores
  32bpp, so what it shows is the ideal picture, not the 4bpp one. A colour
  outside the palette differs between the screen and the glass, by design.

## Realtime memory

* Scanout memory is **static, aligned and in SRAM**: the framebuffer, the core1
  line buffers, the descriptor ring, the core1 stack, the HSTX/palette/LUT
  state. Nothing large and automatic goes on the core1 path.
* **No PSRAM scanout and no DMA present.** The last was measured at roughly
  7.3 ms for 240,000 bytes and still produced display garbage. `present` is a
  CPU copy from the PSRAM draw buffer into the SRAM framebuffer.
  `LVDS_TARGET_USE_PSRAM` is off by default; a board without PSRAM keeps the
  draw buffer in SRAM.
* **No health polling, no auto-recovery, no watchdog on the scanout, no retry
  loops, no chunked present.** A stalled HSTX path is a memory, ownership or
  timing fault: fix the fault. (The uCNC module's boot helper arms a hardware
  watchdog around bring-up; that helper is not part of this project.)
* Core1 prepares the line the ring reaches `ACTIVE_LINE_BUFFERS - 1` scanlines
  later, and that arithmetic **wraps into the next frame**. Preparing from the
  current active line instead left the previous frame's tail across the top of
  the picture - keep the wrap.
* The panel follows the active lines rather than counting from the sync: the
  porches in the panel header are measured values, not preferences.

## What is not here

Files, menus, G-code, tool tables, machine safety, images, and a second panel.
Images in particular are argued for in the module's README and are not in this
project yet.

## Bench-only

The close-of-frame tail, panel readability, the clock points, feed hold, Stop
during queued motion and SD cold-start mounting are things only the machine can
answer. No host sample proves any of them.
