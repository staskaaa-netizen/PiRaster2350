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

## What is not here

Files, menus, G-code, tool tables, machine safety, images, and a second panel.
Images in particular are argued for in the module's README and are not in this
project yet.

## Bench-only

The close-of-frame tail, panel readability, the clock points, feed hold, Stop
during queued motion and SD cold-start mounting are things only the machine can
answer. No host sample proves any of them.
