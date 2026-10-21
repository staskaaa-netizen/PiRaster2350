# PiRaster

**PiRaster** is the RP2350/LVDS pixel backend that was written inside uCNC, on
its own: panel timing and scanout, a 4bpp indexed framebuffer, a sixteen-colour
palette, and the drawing primitives every screen is built from. It drives one
panel - a **Sharp LQ121S1LG44**, 800x600, single-channel LVDS, mounted turned a
quarter - and it has been on that panel and no other. The repository is
`PiRaster2350`, after the chip it drives.

It **began as a fork of [PicoLVDS](https://github.com/ceteras/PicoLVDS)**, and
then turned into something much different: what survives from that project is a
handful of its ideas - the HSTX pacer LUT and the way a scanline is packed among
them - while the scanout, the SRAM framebuffer and its palette, the drawing core,
the fonts and the target seam underneath all of it are this project's own. Those
ideas are PicoLVDS's, and the credit for them is that project's.

It was extracted from
`uCNC/src/modules/lvds_renderer` (see *Where this came from* below). The drawing
code, the palette and the fonts are the same code; the hardware half stopped
including `cnc.h` and calls four port hooks instead.

## What you can do with it

Use PiRaster for a **simple panel with a fast and stable picture**:

* **Text where you want it.** `lvds_draw_text(x, y, ...)` writes a string at any
  view coordinate, in three sizes - 6x8 condensed, 8x14, and 8x14 doubled - and
  in any colour of the current palette. Each glyph carries its own background
  colour, because a glyph is filled rectangles. `lvds_draw_text_clip()` writes
  the same text in a fixed number of columns, so the rows of a screen line up
  without the caller measuring anything.
* **A big, cheap screen.** 800x600 is a large work area for text and boxes, and
  a panel of this class costs little. The upright view holds 133 columns by 75
  rows of the 6x8 face, 100 by 42 of the 8x14, and 50 by 21 of the doubled one.
* an operator screen for a machine: text, lines, rectangles and filled
  rectangles in sixteen colours;
* **Motion when you want it.** Lines, rectangles and ellipses animate as well as
  they draw. The sample scene moves a wave, a disc and a gauge from the clock.
  The operator panel is the main job of this path, and motion is the extra.
* a state that you show with a colour, because you can change a palette slot
  while the picture runs;
* a picture that a client makes itself - an emulator, a game, a picture pump -
  because `lvds_target_blit()` takes one row of indices at a time;
* the same picture on a desk, because the host target writes BMP frames and the
  project checks the pixels.

## What you cannot do with it

PiRaster is not a modern display path. Read the limits before you start:

* **Sixteen colours.** The frame uses 4 bits for each pixel. There is no
  blending, no alpha, no anti-aliasing and no image scaling.
* **No video and no photographs.** The path has no decoder and no scaler.
* **One panel and one board.** The geometry, the timing and the clocks are
  compiled in for a Sharp LQ121S1LG44 on an RP2350.
* **No compositor.** There are no layers and no rotation of arbitrary content.

For a modern panel, use its own DSI, eDP or HDMI path.
[docs/when-to-use.md](docs/when-to-use.md) gives the full comparison.

## Features

* **A stable picture.** The default build draws into a PSRAM backbuffer, then
  copies the frame into the scanout buffer. The scanout never reads a
  half-drawn picture.
* **A live palette.** You can re-colour a slot while the picture runs. Everything
  that already uses that colour changes on the next prepared line. You do not
  redraw the screen, and you do not lose a slot.
* **The turn in one place.** The build states the panel mounting one time, in
  `core/lvds_view.h`. Text, lines, rectangles and ellipses then turn with it.
* **A blit call for a client raster.** A client that makes its own rows of
  palette indices draws at row speed, not at pixel speed.
* **A host target.** The same drawing code writes the same picture into a frame
  in RAM, so you can check a layout without a board.
* **Four spare active lines.** The frame carries 604 active lines, not 600. The
  panel shows one line twice at the close of the active area. The four spare
  lines give that repeated line its own place, away from the picture. This is a
  feature of the panel support; the fault behind it is a known issue.
* **Static realtime memory.** The scanout memory is static, aligned and in SRAM.
  The scanout path has no allocator.

## Known issues

* **The panel repeats one line at the close of the active area.** The root cause
  is not known. The four spare active lines hold the repeated line away from the
  picture. **Do not shorten the frame.** The measurements are in
  [LIMITS.md](LIMITS.md). The record is in
  [docs/torture-history.md](docs/torture-history.md).
* **One clock point gives no video.** `420000 / div 3` produces no picture. This
  is a limit of this board. It is not a requirement of the panel.
* **A full redraw costs 33 ms** in the default (double buffered) build. The
  single buffered build costs 3 ms, but the picture tears.
* **The host samples do not run on this bench machine.** Its MinGW gcc cannot
  run its own cc1. The samples compile, and `make run` runs them on a machine
  with a working compiler.

## License

PiRaster is free software under the **GNU General Public License, version 3**
(see [LICENSE](LICENSE)). PiRaster began as a fork of
[PicoLVDS](https://github.com/ceteras/PicoLVDS). That project is also GPL-3.0,
and the ideas that PiRaster keeps from it stay under the same license.

## The boundaries

```
  core/                    device independent. No SDK, no OS, no threads.
    lvds_color.h           a colour: 5-6-5, or a palette slot once a target has one
    lvds_view.h            the view <-> panel turn, stated once
    lvds_canvas.{h,c}      the surface: the only two calls that write, + guards
    lvds_draw.{h,c}        lines, rectangles, ellipses, text (built on those two)
    lvds_palette.{h,c}     the sixteen named colours
    lvds_font*.{h,c}       the two bitmap faces
    lvds_target.h          the seam: what a target implements
  panels/
    sharp_lq121s1lg44.h    geometry, timing, pins and clocks, as data
  targets/
    host/                  the panel on a desk: a 32bpp frame in RAM + a BMP writer
    rp2350/                the panel itself: HSTX, DMA, core1, PSRAM, the port
  samples/
    common/scene.{h,c}     the one picture every sample draws, and the palette banks
    host_loop/             a while loop that draws and writes frames
    host_snapshot/         one frame, checked pixel by pixel - a sample that is a test
    rp2350_panel/          the same picture, on the glass
  lvds_config.h            which panel this build drives
```

Two rules hold the split together:

* **Everything above `lvds_target.h` is portable.** It never mentions DMA, HSTX,
  PSRAM, a pin, or whether the picture ends up on glass or in a file.
* **Everything below it is one panel on one board.** A target writes pixels,
  presents, and owns the palette. It never mentions the view, the turn, the
  palette's names or the drawing primitives.

The documents behind it:

* [docs/architecture.md](docs/architecture.md) - what each layer does with a
  frame, the scanout chain step by step, and the numbers it runs at;
* [docs/boundaries.md](docs/boundaries.md) - who owns what, and why the split is
  where it is;
* [docs/when-to-use.md](docs/when-to-use.md) - what it is good for, what it is
  not, and what to use instead for a modern panel;
* [docs/torture-history.md](docs/torture-history.md) - the stress harness, the
  retired workarounds, and the faults and measurements behind them;
* [LIMITS.md](LIMITS.md) - the hardware limits a consumer has to know before
  using it somewhere new, and what still needs the bench.
* [hardware/](hardware/README.md) - the adapter: its schematic, the KiCad
  project, the fabrication files, and the eight HSTX pins this build drives.

The panel is drawn **upright by default**. A glass bolted turned a quarter -
the machine's own mounting - is one build flag (`-DLVDS_PANEL_TURN=90`, see
`platformio.ini`), and the layout follows it.

**The palette is live.** A colour's name can be redefined while the picture
runs (`lvds_palette_define()`): on the glass the slot it already holds is
re-coloured in place, so everything drawn in that colour changes with it on the
next scanline, with no redraw and no slot churn. `scene_palette_switch()` in the
samples walks four banks to show it.

## Hardware

| what | this build |
| --- | --- |
| glass | Sharp LQ121S1LG44, 800x600, single-channel LVDS: three data pairs and one clock pair |
| board | Waveshare RP2350-PiZero (`-D PICO_BOARD=\"waveshare_rp2350_pizero\"`) |
| HSTX pairs | D0+ D0- D1+ D1- D2+ D2- CLK+ CLK- = GP12, GP13, GP14, GP15, GP16, GP17, GP18, GP19 |
| carrier | the PicoLVDS LVDS adapter, carried in [hardware/](hardware/README.md) |

**There is no LVDS transmitter chip.** The RP2350's HSTX serialiser drives the
panel's four LVDS pairs from those eight pins, in the seven-phase packing that
the pacer LUT produces. That is the idea PiRaster took from PicoLVDS, and it is
the reason this display path is a header of numbers plus one target.

The design of the adapter - schematic, KiCad project and fabrication package -
lives in [hardware/](hardware/README.md), where the project this one came from
keeps it. The eight pins are stated for the firmware in
`panels/sharp_lq121s1lg44.h`, which is the only place the code reads them.

## Build and run

**The host samples need a C compiler and make, nothing else.**

```console
make            # builds both host samples
make run        # builds them, draws three frames, checks one and writes BMPs
```

`samples/host_snapshot/` exits non-zero when a pixel is not where the scene put
it, so `make run` is also a check. The BMPs are written the way the operator
reads the glass: the frame is 800x600 landscape, the picture is 600x800 and
turned, and `host_panel_write_bmp(path, true)` turns it back for you.

**The firmware sample is a PlatformIO project** (`pio run -e rp2350_panel`),
because that is the stack the panel was brought up on; the UF2 lands in
`.pio/build/rp2350_panel/firmware.uf2`. `samples/rp2350_panel/main.cpp` is
Arduino style: `setup()` brings the panel up, and `loop()` draws the scene and
presents it. The framework's own `main()` is `while (1) { loop(); }`, so the
sample adds nothing to it but those calls. The loop paces itself with `millis()`
(`FRAME_MS`, about 50 frames a second), and the drawing itself is a function of
that clock, so the demo behaves the same on every build.

`pio run -e rp2350_panel_fps -t upload` is the same sample with its **frame
meter** in: the loop runs flat out and every sixty frames it prints the
milliseconds a whole frame - clear, scene, present - takes. That is how the
project answers "is this fast enough for a game", on the panel, rather than
guessing from datasheets.

Two meters, because the answer depends on where the draw buffer lives:
`rp2350_panel_fps` uses the double-buffered PSRAM path (the default, and the
machine's), and `rp2350_panel_sram_fps` draws straight into the scanout buffer
for the fast, tearing comparison. A serial terminal must assert DTR
(`pio device monitor` does; a bare port read may not) or the board's output is
dropped.

**The console is the USB serial port, 115200 8N1.** The port appears when the
application runs, and on this bench the application does not run until RESET is
pressed (below). `pio device list` shows its number, and the number can change
between uploads.

```console
pio device list                        # find the port (USB VID:PID 2E8A:...)
pio device monitor -p COM13 -b 115200  # read it
```

The framework drops CDC output until the host asserts DTR, and
`pio device monitor` asserts it. A monitor that is opened after the board has
started would therefore miss the boot lines, so `console_announce()` in the
sample says the state again for each terminal that attaches. `lvds: draw buffer
in psram` is the line to look for; `lvds: the panel did not come up` replaces it
when the bring-up failed.

**A build that prints checks that someone is listening.** The framework's
`Serial` write has nothing to do when no host is attached, so the sample asks
`Serial` (the connected flag) before writing, in both the frame meter and the
port's log hook. What an unwatched write does to this board was *not*
established: a meter build disappearing from the USB bus turned out to be the
reset below, not the write. The announce line follows the same rule: it is
written only while the connected flag is set.

**Flashing on this board: press RESET after the upload.** `pio run -t upload`
does its job - it resets the board into BOOTSEL, writes the image and verifies
it - but the tool's own reboot into the application often does not bring it up
here, and the board sits with no port until the RESET button is pressed. The
board can also drop off the bus with no upload involved at all. The cause is not
established; the measurements, the candidate cause and the recovery checklist
are in [docs/torture-history.md](docs/torture-history.md). One thing is
established: no watchdog inside the firmware can cover that state, because the
firmware is not running in it.

**The default is double buffered on purpose.** Drawing straight into the buffer
the scanout is reading is several times faster, but the panel is looking at the
picture while it is half redrawn - a screen clear per frame reads as a flicker -
which is the lesson LeanCam already learned. Single buffering is an explicit
choice, never the default.

The samples have no RTOS, no tasks, no callbacks and no state machines: draw,
present, repeat. `samples/common/scene.c` is the whole demo, and it is the same
file for the desk and for the glass.

### Native packed-colour builds

`rp2350_panel_4color` runs the original 800x600 PiRaster scene through the
normal core1 and 7x256 LUT scanout, but packs four pixels into each framebuffer
byte:

```console
pio run -e rp2350_panel_4color -t upload
```

All named scene colours map onto black, white, green and blue. The framebuffer
is 120,800 B instead of 241,600 B, and the complete build uses 177,900 B of
RAM with 345,320 B of heap left. The operator saw the native 800x600
four-colour scene run stably on the glass.

Core1 still produces the same seven-phase LVDS words. A 256-entry expansion
table turns each packed 2bpp byte into four nibble indices before the existing
colour LUT. This keeps the unpack work short enough for the scanline deadline.

The green monochrome variant packs eight pixels per byte:

```console
pio run -e rp2350_panel_1bpp -t upload
```

It maps the scene onto black and one green foreground. The framebuffer is
60,400 B and the complete build uses 117,996 B of RAM with 405,224 B of heap
left. A 256-entry table expands each byte directly to all eight nibble indices.
The operator saw the native 800x600 green scene on the glass after the board's
usual post-upload reset.

### CPU-less Command Expander bench

The SRAM-resident line-stream probe works on the glass. It proves that the
HSTX Command Expander can carry the existing raw LVDS words without core1
preparing scanlines.

```console
pio run -e rp2350_panel_headless_sram_bars -t upload
```

The probe bakes three command streams in SRAM: one active colour-bar line, one
blank line, and one VSYNC line. The DMA descriptor ring reuses them for the
frame. The ring has 1,024 entries, so its 354 guard lines lengthen vertical
blanking and lower refresh to about 37 Hz. This is a mechanism probe, not a
panel-timing configuration.

The same mechanism now carries a full-screen, four-colour source:

```console
pio run -e rp2350_panel_headless_200x120 -t upload
```

The source is **200x120**, stored in 12,000 B of SRAM. Every source row is
expanded once through the palette and 7x256 LUT, then nearest-neighbour scaled
4x horizontally and 5x vertically to fill 800x600. Each scaled row is stored as
one complete 925-word scanline, so the 120 rows use 444,000 B. One HSTX-paced
data channel and one descriptor channel loop the rows; core1 remains idle. The
build uses 488,300 B of static RAM, leaving 33,576 B of heap.

Core0 may redraw and rebuild the rows only after descriptor 669. The 354 guard
lines plus the start of vertical blanking give about 9.3 ms, enough for the
four-colour scene at 10 fps. Core1 remains idle. The operator saw the scene
animate on the glass without tearing.

The palette is part of the bake. A colour change does not reach a running
headless stream; the picture must be baked again. The live palette is therefore
lost in this mode.

**`RAW_REPEAT` does not make an LVDS colour line tiny.** A constant-colour
LVDS line still produces seven distinct 32-bit phase windows, and the command
expander repeats one word sequentially. Reordering those seven phases into
seven repeated runs would change the stream. The probe therefore uses complete
`RAW` lines. `RAW_REPEAT` is only correct where a run really consists of
consecutive equal raw words.

The full-frame PSRAM variant is retired:

* its first colour-bar build produced no video;
* a second build produced blinking, never a static frame;
* the working SRAM probe isolates the failure to the PSRAM full-frame loop,
  not the raw LVDS command words.

The scene snapshot was therefore not attempted. The normal core1 path remains
the only supported full-frame scanout. The rollback baseline is the uCNC NCC
image:

```console
pio run -e RP2350-LEANCAM-LVDS-NCC -t upload
```

## What has been verified, and what has not

Verified here:

* `pio run -e rp2350_panel` builds the firmware sample and links it
  (RAM 56.8%, flash 0.4% of 16 MB, `firmware.uf2` generated).
* The normal build is unchanged at RAM 298,156 B and flash 84,824 B after the
  bake API was extracted from the same active-line routine.
* The native four-colour 2bpp build links at RAM 177,900 B and flash 84,760 B.
  The operator saw its 800x600 scene run stably.
* The green 1bpp build links at RAM 117,996 B and flash 84,704 B. The operator
  saw its native 800x600 scene on the glass.
* `rp2350_panel_headless_sram_bars` builds and links at RAM 295,392 B and flash
  70,384 B.
* `rp2350_panel_headless_200x120` builds and links at RAM 488,300 B and flash
  82,992 B.
* The headless ELF has no `core1_entry` or `poll_dma_scanout` symbol.
* The operator saw the SRAM-resident colour bars on the panel with core1 idle.
  This is hardware-verified: the raw Command Expander path and the SRAM line
  reuse work.
* The operator saw the 200x120 four-colour scene full-screen and animating on
  the panel. This hardware-verifies the integer-scaled SRAM line mode and the
  blank-window row rebuild.
* The portable half - core, host target and both host samples - compiles
  warning-free under `-Wall -Wextra` with the toolchain's ARM compiler.

Not verified here:

* **The full-frame PSRAM scanout remains negative.** The 2,479,000-byte loop
  produced no video, then blinking; it never produced a static frame. It stays
  retired.
* **The native-resolution scene comparison was not attempted.** The PSRAM
  scene path failed, and a complete native-resolution scene does not fit the
  SRAM-resident line mechanism. The 200x120 mode is a separate low-resolution
  probe.
* **The host samples have not been run on this machine.** Its MinGW `gcc` cannot
  run its own `cc1` (missing prerequisite DLLs), so any host build fails before
  compiling a line. `make run` is one command away on a machine whose compiler
  works; the check inside `host_snapshot` is what makes that run meaningful.
* The normal demo and the SRAM-resident bars have been on the glass. The
  frame-close tail and the full list of clock points still need their own bench
  checks, listed in `LIMITS.md`.

## Where this came from

`uCNC/src/modules/lvds_renderer` is the same renderer inside uCNC, and the
same directory - module and library, one copy - was published to the
`uCNC-modules` repository. This project is that code rearranged around a target
seam, with the four uCNC calls (`mcu_millis`, `cnc_delay_ms`, `proto_info`,
`cnc_is_file_io_critical`) replaced by `targets/rp2350/rp2350_port.h`.

**The two copies can drift.** They are the same code today; when this project
becomes the owner, the module should be generated from it or replaced by it
rather than edited beside it. That decision has not been made yet.
