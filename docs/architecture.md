# The architecture

This file explains what each part of PiRaster does with a frame. It follows a
pixel from a drawing call to the LVDS pairs. [boundaries.md](boundaries.md)
explains why the parts are split where they are.

## The parts

| part | files | owns | knows about |
| --- | --- | --- | --- |
| core | `core/` (13 files, ~1,130 lines) | colours, the view and its turn, the two calls that write a pixel, the primitives built on them, the palette names and values, the two fonts | nothing outside C. No SDK, no pins, no board, no threads. |
| seam | `core/lvds_target.h` | nine calls that a target implements | nothing |
| panel | `panels/sharp_lq121s1lg44.h` (62 lines) | geometry, timing, pins and clocks, as macros | nothing |
| target: host | `targets/host/` (2 files, ~250 lines) | a 32bpp frame in RAM and a BMP writer | the seam |
| target: rp2350 | `targets/rp2350/` (6 files) | HSTX, the pacer LUT, the DMA descriptor ring, core1 and the line preparation, the SRAM framebuffer, the guarded baked-frame experiment, PSRAM bring-up, the sixteen palette slots, four port hooks | the seam, the panel header, the Pico SDK |
| samples | `samples/` (~470 lines) | one demo picture, drawn by three programs | the core |

The project is smaller than 3,000 lines because of the split. The module that
PiRaster came from had one 950-line file. That file wrote pixels, drew text, ran
core1 and programmed DMA at the same time, and nothing in it could be tested
without the board. In PiRaster the hardware half is about one third of the tree.
The other two thirds build on a host.

## From a drawing to a byte

Above the seam, all drawing uses view coordinates. The view is 600x800 on the
machine, because the panel is mounted turned a quarter. This project draws
upright by default (800x600), because its sample panel is on a desk.

The turn is stated one time, in `core/lvds_view.h`. All readers use it: the
RP2350 target, the host target and the tools that write frames. If a picture is
upside down, correct that one number.

1. The caller draws with a core primitive. `core/lvds_canvas.c` holds the two
   calls that write a pixel: `lvds_canvas_pixel()` and
   `lvds_canvas_fill_rect()`. `core/lvds_draw.c` builds lines, rectangles,
   ellipses and text on top of them. A glyph is a set of filled rectangles, so
   text turns with the rest of the picture. The text code never writes a pixel
   itself.
2. The core maps the view coordinate to the glass. It uses the turn and
   `LVDS_PANEL_X/Y`. It drops all coordinates outside the glass. It never wraps
   a coordinate. A filled rectangle stays one call, because the turn maps a
   rectangle to a rectangle.
3. `lvds_target_pixel()` and `lvds_target_fill_rect()` are the only two calls in
   the project that write a pixel. On the RP2350 target they write one nibble
   into the 4bpp framebuffer and mark the picture dirty. On the host target they
   write one 32bpp word into the frame in RAM.

A client that already has a raster - an emulator, a picture pump, or a game with
its own dirty boxes - does not walk the canvas pixel by pixel.
`lvds_target_blit(x, y, w, indices)` takes one row of indices. The caller makes
the indices, clips them and gives panel coordinates. On the RP2350 target two
indices go into one byte, and the **left pixel is in the low nibble**. That is
the order in which the scanout LUT reads them back.

## From a byte to LVDS pairs

The scanout is the part that must stay boring. On this board it is:

```
  core (view 600x800)                            core/, panels/
        |  turn and bounds
        v
  4bpp indexed framebuffer, 800x604, in SRAM      241,600 B
        |  core1: read one line, expand 16 colours into 7 phases
        v
  four active line buffers (one scanline each)    14,784 B
        |  the DMA data channel feeds the HSTX FIFO
        v
  HSTX serialiser - 7 serial phases for each pixel clock
        |
        v
  Sharp LQ121S1LG44, 800x600 single-channel LVDS   GP12..GP19
```

The rules and the facts of this chain:

* **The two DMA channels come from the allocator.** The target claims them on
  core0 in `lvds_target_init()`, before core1 starts. One channel feeds the HSTX
  FIFO. The second channel moves the first one through a static ring of 1,024
  descriptors in SRAM. A client with DMA of its own - a USB stack, PSRAM, an
  audio path - can already hold channels 10 and 11. The target therefore takes
  the channels that the allocator gives it, and it fails if there is no free
  channel. See [../LIMITS.md](../LIMITS.md).
* **Core1 prepares one line ahead.** It prepares the line that the ring reaches
  after `ACTIVE_LINE_BUFFERS - 1` scanlines. That arithmetic wraps into the next
  frame. If core1 prepares from the current line, the tail of the previous frame
  shows across the top of the picture.
* **The framebuffer stays in SRAM.** The HSTX DMA reads it for every line, so it
  cannot live in PSRAM. The pacer LUT, the ring, the line buffers and the core1
  stack are static and aligned in SRAM as well.
* **The frame sends 604 active lines to a 600-line glass.** The four spare lines
  are necessary. The panel repeats one line at the close of the active area,
  whatever the content is. The four spare lines give that repeated line its own
  place. If you shorten the frame, the repeat lands on the picture. This is a
  feature of the panel support for a fault that is still open. (Bench,
  2026-10-04. The record is in [torture-history.md](torture-history.md).)
* **Three numbers set the clock.** The pixel clock is
  `LVDS_SYS_CLOCK_KHZ / 1000 / LVDS_HSTX_CLOCK_DIV * 2 / 7`. It is 40.00 MHz at
  the machine's `280000 / div 2`. The panel also locks below its 40 MHz nominal.
  The point `420000 / div 3` gave no video. That is a limit of this board, not a
  requirement of the panel.
* **The panel follows the active lines.** It does not count from the sync. The
  porches in the panel header are measured values. A different back porch does
  not move the picture on the glass.

### Native packed-colour modes

The normal core1 path accepts either 4bpp or 2bpp indexed pixels. At 2bpp:

```
  800x604 source, 4 pixels per byte      120,800 B
        |  core1 reads two bytes
        v
  256-entry expansion table
        |  eight 4-bit indices, as before
        v
  existing 7x256 colour LUT
        |  existing seven-phase LVDS words
        v
  normal DMA scanout
```

Only the framebuffer packing and unpack step change. The palette LUT, line
preparation, descriptor ring and core1 ownership remain the same. A 2bpp
native build uses 177,900 B of RAM, leaving 345,320 B of heap.

The expansion table is load-bearing for timing. A per-byte shift loop made the
display unstable; two table lookups per eight pixels restored the realtime
margin.

At 1bpp, one byte carries eight pixels and the expansion table produces all
eight nibble indices in one lookup. The same scene is limited to black and one
green foreground. The framebuffer is 60,400 B and the complete native build
uses 117,996 B of RAM with 405,224 B of heap left. No scanout component or
seven-phase packing changes.

### The headless SRAM bench path

`LVDS_HEADLESS_SRAM_BARS` leaves the normal path unchanged and adds a frozen
pattern probe below the same palette and LUT:

```
  4bpp SRAM framebuffer                        241,600 B
        |  core0, once: the same active-line expansion core1 calls
        v
  command + 924 raw HSTX words
  active line, blank line, VSYNC line in SRAM   11,100 B
        |  1,024 descriptor entries, core1 idle
        v
  HSTX Command Expander <- one HSTX-paced DMA channel
                             ^ one descriptor-advance channel
```

`rp2350_panel_bake_line()` writes one `RAW` command followed by the exact words
the normal path prepares. The colour-bar probe reuses one such active line
for every active row and keeps blank/VSYNC lines beside it. Core1 is not
launched, so there is no preparation deadline or polling loop to hide a timing
fault.

The Command Expander count is 12 bits. A 924-word LVDS line fits in one command.
`RAW_REPEAT` is not a scanline compressor: it repeats one 32-bit transfer,
while an LVDS line cycles through seven distinct phase words.

The mechanism has been seen working on the glass. The 1,024-entry ring adds 354
guard lines, so its roughly 37 Hz refresh makes it a probe rather than a panel
timing mode. The full 670-line frame in PSRAM is retired: it produced no video
and then blinking. Its palette is frozen into any bake, so there is no live
palette on this path.

### The 200x120 full-screen mode

`LVDS_HEADLESS_LOWRES` reuses the same LUT and SRAM line transport with a
smaller source:

```
  200x120 4bpp source in SRAM                     12,000 B
        |  repeat each pixel 4x horizontally
        v
  one complete 925-word scanline per source row
        |  store all 120 rows                       444,000 B
        |  repeat each row 5x vertically in the descriptor ring
        v
  one data channel + one descriptor channel
        v
  HSTX Command Expander, core1 idle
```

The source stays indexed. A startup pass repeats each source pixel four times
into an eight-pixel LUT group, so the existing seven-phase expansion produces a
full 800-pixel row. There are only 120 unique active rows; the descriptor ring
selects each row five times to reach 600 visible lines. Keeping each row as one
complete command avoids a phase discontinuity at a command boundary.

The operator saw the four-colour scene full-screen on the panel. The mode uses
488,300 B of static RAM and has 33,576 B of heap left. Its 1,024-line ring
keeps the same roughly 37 Hz probe refresh.

For animation, core0 waits until the descriptor pointer has passed the active
frame. It then redraws the 200x120 source and rebuilds all 120 complete rows in
the approximately 9.3 ms guard window before scanout wraps. DMA never stops,
and core1 remains idle. The scene runs at 10 fps.

## The palette

The core owns the palette **names and values**. The target owns the **slots**.
There are sixteen slots. A slot is taken when a colour is first **used**, not
when a table is parsed. The second rule came from a fault: a picture with its
own colours once came out in greys, because the slots were gone before it drew.

You can change a colour while the picture runs. The core re-colours the slot
that it already holds (`lvds_target_slot_set()`). On the RP2350 target core1
rebuilds the LUT, so the next prepared line carries the new colour and
everything that already uses that colour changes with it. You do not redraw the
screen and you do not lose a slot. The host target has no palette, so a new
colour appears only where it is drawn next.

## Present, and the price of a stable picture

`lvds_target_present()` makes the drawn picture the picture that the scanout
reads. On the host target it is bookkeeping. On the RP2350 target it is a **CPU
copy** from the PSRAM draw buffer into the SRAM scanout framebuffer. The copy is
240,000 bytes. It is not a DMA channel and it is not a chunked scheme. Both were
tried, and both are removed (see [torture-history.md](torture-history.md)).

That copy is the trade of the default build:

| build | draw target | cost of a frame | picture |
| --- | --- | --- | --- |
| double buffered (default, `LVDS_TARGET_USE_PSRAM=1`) | a PSRAM backbuffer, then the present copy | about 33 ms (measured) | stable: the scanout never reads a half-drawn picture |
| single buffered (`LVDS_TARGET_USE_PSRAM=0`) | the scanout framebuffer | about 3 ms (measured, about 283 fps) | fast, and torn: a screen clear for each frame reads as a flicker |

The single buffered build is correct for a still panel, for a measurement, or
for a board without PSRAM. It is never the default. A panel that is redrawn
while the scanout reads it gives the flicker that a machine UI must not have.

## The numbers

| quantity | value |
| --- | --- |
| glass | 800x600 single-channel LVDS, turned a quarter on the machine (view 600x800) |
| frame | 604 active lines: 600 for the picture and the necessary tail, plus the porches of the panel header |
| pixel clock | 40.00 MHz at `280000 / div 2`. The measured points, and the point that gave no video, are in [../LIMITS.md](../LIMITS.md). |
| refresh | about 57 Hz. This value is arithmetic from the header: 1,056 pixel-clocks for a line, 670 lines for a frame, at 40 MHz. The bench measured the cost of the drawing, not the refresh. |
| colour | 4bpp indexed, sixteen slots, no blending, no alpha |
| a drawn frame | 3 ms single buffered, 33 ms through the present copy |
| scanout memory | about 285 KB of the RP2350's 512 KB SRAM. The framebuffer is 241,600 B, which is 81% of it. The table is in [../LIMITS.md](../LIMITS.md). |

## Rules that must not change

These rules come from the module. They are the reason that the scanout needs no
recovery path:

* The scanout reads from SRAM.
* Permanent realtime buffers are static and aligned.
* No large automatic variables go on the core1 path.
* No new live data goes in Scratch X/Y.
* No PSRAM scanout, no PSRAM front/back buffers and no DMA present.
* No health polling, no auto-recovery, no retry loop, no watchdog on the scanout
  and no chunked present.
* The turn is stated one time, in the core. The frame length is stated one time,
  in the panel header.

If the scanout stops, it is a memory, ownership or timing fault. Fix the fault.

## A second panel or a second board

A panel is a header: geometry, timing, pins and clocks. A board is a target: the
nine calls of the seam, and nothing above them. The split has been tested one
time already: the host target implements the same seam into a frame in RAM, and
the project checks its pixels.

## Open questions

* **Why does the panel repeat a line at the close of the active area?** The four
  spare active lines contain the repeat. They do not explain it. The probes show
  that the cause is the close of the active area, and not the frame total, the
  blanking, the content or the clock.
* **What does an unread serial write do to this board?** The sample guards its
  prints with the connected flag of the framework. That guard was never proven
  to be the cause of the meter-build failure. A missing RESET press was the
  cause.
