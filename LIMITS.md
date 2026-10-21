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
* The machine's glass is mounted **turned a quarter** and the operator reads
  600x800; that is `LVDS_PANEL_TURN 90`. This project draws **upright by
  default** (`0`), and the turned mounting is a build option, `270` being the
  same quarter the other way. A picture that comes out the wrong way up is that
  one number.

## The feature: four spare active lines, for a fault that is open

The frame sends **604 active lines** to a 600-line glass. The four spare lines
are necessary, and they are part of the panel support.

* **The feature.** The panel repeats one line at the close of the active area.
  The four spare lines give that repeated line its own place, away from the
  picture. The view, and every drawing, stays the glass's own 600x800 on the
  *first* 600 active lines.
* **The known issue.** The root cause is not known. The panel or the link
  repeats or eats a line at the close of the active area, whatever the content
  is. The tail contains the fault. It does not explain the fault.

**Do not shorten the frame. Do not treat the number as tunable.**

The measurements (bench, 2026-10-04):

* Four extra *blanking* lines changed nothing on the glass. The frame total and
  the clock are therefore not the cause.
* The bench walked the active height down. **600** lines put the repeat on the
  picture, **602** lines showed one extra black line, and **603** and **604**
  lines were clean. The frame keeps 604, which is one line of margin over the
  three that are necessary.
* The record of the probes is in
  [docs/torture-history.md](docs/torture-history.md).

## Sixteen colours, and no blending

* The framebuffer is **4bpp indexed**: sixteen colours, no anti-aliasing, no
  blending, no alpha. A colour that is not in the table comes out as the nearest
  colour that is.
* The table is **full**. A new colour means retiring one; the palette's names
  are the machine's own (`WHITE` is its stock, `WHITE_WARM` its ink).
* A slot is taken when a colour is first **used**, never when the table is
  parsed. That was a real fault: a picture that brought its own colours found
  the palette already filled and came out in the panel's greys (2026-09-27).
* A name can be redefined while the picture runs, and the switch is free: the
  slot keeps its number and is re-coloured in place, so on the glass everything
  already drawn in that colour changes with it, on the next line core1
  prepares, with no redraw and no slot churn. On a target with no palette (the
  host one) the new colour shows only where it is drawn next.
* A picture that brings colours of its own (a boot splash) must call
  `lvds_canvas_palette_reset()` when it is done, or the screen that follows is
  drawn in what it left behind.
* The **host target has no palette**: it hands the 5-6-5 word back and stores
  32bpp, so what it shows is the ideal picture, not the 4bpp one. A colour
  outside the palette differs between the screen and the glass, by design.

## The scanout's two DMA channels

* The scanout's **data and control channels come from the DMA allocator**
  (`lvds_target_init`, on core0, before core1 starts), never from fixed
  numbers. A client with DMA of its own - a USB stack, PSRAM, an audio path -
  can already hold 10 and 11 by the time the panel comes up, and a fixed pair
  then either clobbers that driver's transfers or loses the scanout its own.
  Nothing claims or releases them again afterwards.
* If the allocator has no free channel, `lvds_target_init` **fails** rather
  than running a partial scanout. A client that uses most of the board's
  channels has to leave two for the panel.

## Realtime memory

* Scanout memory is **static, aligned and in SRAM**: the framebuffer, the core1
  line buffers, the descriptor ring, the core1 stack, the HSTX/palette/LUT
  state. Nothing large and automatic goes on the core1 path.

  What that reserves, on the RP2350's 512 KB - the build reports 297,704 B in
  use, and this is where it goes:

  | what | bytes | optional? |
  | --- | --- | --- |
  | 4bpp scanout framebuffer, 800x604 | 241,600 | no - the scanout reads it every line, and it must be SRAM |
  | the four spare active lines (the panel's close) | 1,600 | no - see above |
  | four active line buffers (one scanline, 7 phases) | 14,784 | no |
  | two inactive line buffers (blanking, and vsync) | 7,392 | no |
  | descriptor ring, 1024 entries | 4,096 | no |
  | core1 stack | 8,196 | no |
  | palette + colour LUT (7 phases x 256) | 7,217 | no |
  | the program's own data (USB stack and the rest) | ~14,000 | - |

  So about 285 KB is the scanout path itself, and **the framebuffer is 81% of
  it**: 4bpp is already the floor for this path (1bpp would be 60 KB, and two
  colours). A second, *drawing* buffer is the only part that is optional, and
  it is the expensive one: in PSRAM it costs another 241 KB of PSRAM **and** a
  240 KB copy in `present()` every frame - 33 ms a frame against 3 ms measured
  on the panel.

  **That second buffer is not optional in practice.** Drawing straight into the
  scanout buffer is fast but the panel reads the picture while it is being
  half-redrawn, and a screen clear per frame reads as a flicker; the bench saw
  exactly that. The double buffer (draw into PSRAM, present a whole frame) is
  what makes the video stable - the same reason LeanCam put its buffers in
  PSRAM - so it is this project's default, and single buffering is an explicit
  choice for a still panel, a measurement, or a board with no PSRAM.

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

## Native packed-colour depths

`LVDS_TARGET_BPP=2` is supported by the normal core1/LUT path. The framebuffer
then holds four pixels per byte:

* framebuffer, 800x604: **120,800 B**, down from 241,600 B at 4bpp;
* complete native build: **177,900 B** RAM and **84,760 B** flash;
* heap remaining: **345,320 B**;
* the 2bpp build maps every scene colour onto four palette values;
* the operator saw the native 800x600 scene run stably.

Core1 converts two packed bytes into eight nibble indices before the existing
7x256 LUT. That conversion uses a 256-entry expansion table. The first version
used an eight-iteration loop per group and was unstable; the table restored the
scanline timing margin. The default target remains 4bpp and sixteen colours.

`LVDS_TARGET_BPP=1` reduces the same path to black and one green foreground.
Eight pixels share each byte:

* framebuffer, 800x604: **60,400 B**;
* complete native build: **117,996 B** RAM and **84,704 B** flash;
* heap remaining: **405,224 B**;
* a 256-entry table expands one byte directly into eight nibble indices before
  the same 7x256 LUT;
* the operator saw the native 800x600 green scene after the board's usual
  post-upload reset.

## Headless Command Expander - SRAM only

`LVDS_HEADLESS_SRAM_BARS` is the working CPU-less probe. It bakes one active
colour-bar line, one blank line and one VSYNC line in SRAM, then reuses them
through the DMA descriptor ring. Core1 is not launched.

* One `RAW` command carries each complete 924-word scanline. The command
  expander's 12-bit count holds the whole line.
* The operator saw the colour bars on the panel with core1 idle. The raw LVDS
  words and the SRAM line reuse therefore work on the glass.
* The 1,024-entry descriptor ring adds 354 guard lines after the 670-line
  timing sequence. Refresh is about 37 Hz. This is a mechanism probe, not a
  panel-timing mode.
* Palette changes invalidate the bake; live palette is not available.
* `RAW_REPEAT` repeats one 32-bit word, not a scanline or a seven-word LVDS
  phase sequence. It cannot reduce a constant-colour LVDS line to one repeated
  word without changing the serial phase.

`LVDS_HEADLESS_LOWRES` extends the working mode to a four-colour **200x120**
source, scaled 4x horizontally and 5x vertically to fill the 800x600 panel.

* The indexed source is 12,000 B. Each of the 120 scaled rows is stored as a
  complete 925-word scanline, using 444,000 B. The complete build uses
  488,300 B of the 512 KB SRAM.
* One HSTX-paced data channel feeds complete lines. One control channel
  advances the 1,024-entry descriptor ring; core1 remains idle.
* The operator saw the full-screen scene and then its animation on the glass.
  This verifies the source-to-LVDS expansion, integer scaling and blank-window
  rebuild.
* The descriptor ring still adds 354 guard lines, so refresh is about 37 Hz.
  This is a probe mode, not a normal 60 Hz panel mode.
* Animation is driven by core0 at 10 fps. It redraws and rebuilds only after
  descriptor 669, using the approximately 9.3 ms guard window. Core1 never
  becomes the scanout producer.
* Four colours reduce source storage only. They do not reduce the number of
  raw LVDS words transmitted for the scaled raster.
* Only 33,576 B of main-SRAM heap remain. Core stacks live in the separate
  Scratch X/Y memories and are not part of that heap figure.

The full-frame PSRAM loop is retired. Its 2,479,000-byte stream produced no
video on the first attempt and blinking on the second; it was never static.
The working SRAM probe isolates that failure to the PSRAM full-frame loop, not
the raw Command Expander words. A native-resolution scene was not attempted
because one does not fit this SRAM-resident mechanism. The rollback baseline is:

```console
pio run -e RP2350-LEANCAM-LVDS-NCC -t upload
```

## What is not here

Files, menus, G-code, tool tables, machine safety, images, and a second panel.
Images in particular are argued for in the module's README and are not in this
project yet.

## Bench-only

The SRAM Command Expander bars and the animated 200x120 full-screen scene have
been seen on the panel. The full-frame PSRAM variant failed, and the
native-resolution scene comparison was not attempted. The close-of-frame tail,
panel readability at normal refresh, the clock points, feed hold, Stop during
queued motion and SD cold-start mounting remain bench items. No host sample
proves any of them.
