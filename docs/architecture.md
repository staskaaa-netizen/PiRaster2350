# The architecture, layer by layer

[boundaries.md](boundaries.md) says *why* the project is split where it is.
This file says what each layer does with a frame, in the order a pixel travels,
and which of its shapes are load-bearing. It is written against the RP2350
target because that is the one that drives glass; the host target is the same
picture with the scanout replaced by a frame in RAM.

## The layers, and what each one owns

| layer | files | owns | knows about |
| --- | --- | --- | --- |
| core | `core/` (13 files, ~1,130 lines) | colours, the view and its turn, the two calls that write a pixel and everything built on them, the palette's names and values, the two fonts | nothing outside C: no SDK, no pins, no board, no threads |
| seam | `core/lvds_target.h` | nine calls a target implements | nothing |
| panel | `panels/sharp_lq121s1lg44.h` (62 lines) | geometry, timing, pins and clocks, as macros | nothing |
| target: host | `targets/host/` (2 files, ~250 lines) | a 32bpp frame in RAM and a BMP writer | the seam |
| target: rp2350 | `targets/rp2350/` (6 files, ~1,020 lines) | HSTX, the pacer LUT, the DMA descriptor ring, core1 and line preparation, the SRAM framebuffer, PSRAM bring-up, the palette's sixteen slots, four port hooks | the seam, the panel header, the Pico SDK |
| samples | `samples/` (~470 lines) | one demo picture, drawn by three programs | the core |

The whole project is under 3,000 lines, and the split is why: the module this
came from had one 950-line file that wrote pixels, drew text, ran core1 and
programmed DMA at the same time, and nothing in it could be tested or reused
without the board. Here the hardware half is about a third of the tree and the
rest builds on a host.

## A drawing, from a view coordinate to a byte

Everything above the seam draws in **view** coordinates: 600x800 on the
machine's glass, because the panel is bolted turned a quarter, and 800x600
upright by default in this project (the sample builds upright). The turn is
stated exactly once, in `core/lvds_view.h`, and every reader goes through it -
the machine's target, the host target, the station that dumps frames. A picture
that comes out upside down is that one number.

1. A caller draws with the core's primitives: `lvds_canvas_pixel()`,
   `lvds_canvas_fill_rect()`, and on top of those the lines, rectangles,
   ellipses and **text** in `core/lvds_draw.c`. A glyph is filled rectangles, so
   text turns for free - the text code never touches a pixel itself.
2. The core maps the view coordinate onto the glass through `LVDS_PANEL_X/Y`
   and the turn, and drops anything outside the glass: out-of-bounds is
   discarded, never wrapped, and a filled rectangle stays one call because the
   turn maps a rectangle to a rectangle.
3. `lvds_target_pixel()` and `lvds_target_fill_rect()` are the **only two calls
   in the project that write a pixel**. On the RP2350 target they write a
   nibble into the 4bpp framebuffer and mark the picture dirty; on the host
   target they write a 32bpp word into the frame in RAM.

A client that already owns a raster - an emulator, a picture pump, a game with
its own dirty-box logic - does not have to walk the canvas a pixel at a time:
`lvds_target_blit(x, y, w, indices)` takes a row of indices the caller made, in
panel coordinates, already clipped. On the RP2350 target two indices go into a
byte, **left pixel in the low nibble**, which is the order the scanout's LUT
reads them back.

## A frame, from a byte to LVDS pairs

The scanout is the part that has to be boring. On this board it is:

```
  core (view 600x800)                     core/ + panels/
        |  turn, bounds
        v
  4bpp indexed framebuffer, 800x604, in SRAM        (241,600 B)
        |  core1: read a line, expand 16 colours -> 7 phases
        v
  four active line buffers (one scanline each)      (14,784 B)
        |  DMA data channel feeds the HSTX FIFO
        v
  HSTX serialiser - 7 serial phases per pixel clock -> three data pairs + clock
        |
        v
  Sharp LQ121S1LG44, 800x600 single-channel LVDS     (GP12..GP19)
```

and the parts of that picture that are easy to get wrong:

- **Two DMA channels, claimed from the allocator** on core0 in
  `lvds_target_init`, before core1 starts: one feeds the HSTX FIFO, the other
  advances the first through a **static ring of 1,024 descriptors in SRAM**. A
  client with DMA of its own - a USB stack, PSRAM, an audio path - can already
  hold 10 and 11, so the ring takes whatever the allocator has free and fails
  the target if there is nothing (see [../LIMITS.md](../LIMITS.md)).
- **Core1 prepares the line the ring reaches `ACTIVE_LINE_BUFFERS - 1`
  scanlines later**, and that arithmetic **wraps into the next frame**.
  Preparing from the current line instead left the previous frame's tail across
  the top of the picture.
- **The framebuffer is in SRAM and stays there.** The HSTX DMA reads it every
  line, so it cannot live in PSRAM; the pacer LUT, the ring, the line buffers
  and the core1 stack are static, aligned and live there too.
- **The frame carries 604 active lines against a 600-line glass.** The tail is
  not slack: the panel repeats a line at the close of the active area, whatever
  is on it, and the spare lines absorb the repeat. Shorten the frame and the
  repeat lands on the picture (bench, 2026-10-04 - the story and the probes are
  in [torture-history.md](torture-history.md)).
- **The scanout clock is set by three numbers, and only those three**: the
  pixel clock is `LVDS_SYS_CLOCK_KHZ / 1000 / LVDS_HSTX_CLOCK_DIV * 2 / 7`,
  which is 40.00 MHz at the machine's `280000 / div 2`. The panel locks below
  its 40 MHz nominal too, and `420000 / div 3` produced no video at all - a
  board/overclock boundary, not a panel requirement.
- **The panel follows the active lines**, not the sync: the porches in the
  panel header are measured values (moving the back porch does not move the
  picture on the glass), so they are data, not preferences.

## The palette, and why a colour switch is free

The core owns the palette's **names and values**; the target owns the
**slots**. Sixteen slots, four bits, and a slot is taken when a colour is first
*used* - not when a palette table is parsed, which is what once turned a picture
that brought its own colours into a screen of greys.

A colour can be redefined while the picture runs. The core re-colours the slot
it already holds (`lvds_target_slot_set()`); on the RP2350 target core1 rebuilds
the LUT, so the next line it prepares carries the new colour and **everything
already drawn in that colour changes with it** - no redraw, no slot churn, no
flash. A target with no palette (the host) shows the new colour only where it is
drawn next; that difference is honest, and it is why the host picture is the
ideal one.

## Present, and the price of a stable picture

`lvds_target_present()` makes the drawn picture the one the scanout reads. On
the host it is bookkeeping. On the RP2350 it is a **CPU copy out of the PSRAM
draw buffer into the SRAM scanout framebuffer** - 240,000 bytes - and it is
deliberately not a DMA channel and not a chunked scheme; both were tried and
retired (see [torture-history.md](torture-history.md)).

That copy is the whole trade of the default build:

| build | what it draws into | a frame costs | the glass |
| --- | --- | --- | --- |
| double buffered (default, `LVDS_TARGET_USE_PSRAM=1`) | a PSRAM backbuffer, then the present copy | ~33 ms measured | stable: the scanout never reads a half-drawn picture |
| single buffered (`LVDS_TARGET_USE_PSRAM=0`) | the scanout framebuffer itself | ~3 ms measured (~283 fps) | fast, and torn: a screen clear per frame reads as a flicker |

Drawing straight into the scanout buffer is the right answer for a still panel,
a measurement, or a board with no PSRAM. It is never the default, because a
panel that is being redrawn under the scanout is exactly the flicker the
machine's own UI cannot have.

## The numbers

Measured on the bench, or arithmetic from the panel header where marked:

| what | value |
| --- | --- |
| glass | 800x600, single-channel LVDS, turned a quarter on the machine (view 600x800) |
| frame sent | 604 active lines (600 for the picture + the load-bearing tail), plus the porches in the panel header |
| pixel clock | 40.00 MHz at `280000 / div 2`; the measured points and the one that produced no video are in [../LIMITS.md](../LIMITS.md) |
| refresh | ~57 Hz by arithmetic from the header (1056 pixel-clocks a line, 670 lines a frame at 40 MHz); the bench measured the *drawing's* cost, not the refresh |
| colour | 4bpp indexed, sixteen slots, no blending, no alpha |
| a drawn frame | 3 ms single buffered, 33 ms through the present copy |
| scanout memory | ~285 KB of the RP2350's 512 KB SRAM, of which the framebuffer is 241,600 B (81%); the table is in [../LIMITS.md](../LIMITS.md) |

## What must not move

These are the module's own realtime rules, and they are the reason the scanout
has never needed a recovery path:

- scanout sourced from SRAM; permanent realtime buffers static and aligned;
- no large automatic locals and nothing new and live on the core1 path, and
  nothing new in Scratch X/Y;
- no PSRAM scanout, no PSRAM front/back scanout, no DMA present;
- no health polling, no auto-recovery, no retry loop, no watchdog on the
  scanout, no chunked present;
- one statement of the turn, in the core, and one statement of the frame's
  length, in the panel header.

If the scanout stops, it is a memory, ownership or timing fault: fix the fault.

## Where a second panel, or a second board, plugs in

A panel is a header: geometry, timing, pins, clocks. A board is a target: the
seam's nine calls, and nothing above them. That is the test of the split, and it
has been run once already - the host target implements the same seam into a
frame in RAM, and the project's own frame checks draw the same scene through it.

## What is still not known

- **Why the panel repeats a line at the close of the active area.** The four
  spare active lines are containment, not a root cause: the probes say it is
  the close of the active area and not the frame's total, the blanking, the
  content or the clock, and that is as far as the bench has taken it.
- **What an unread serial write does to this board.** The sample guards its
  prints with the framework's connected flag; the guard was never shown to be
  the thing that wedged the meter build (a missing RESET press was).
