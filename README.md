# lvds-renderer

The RP2350/LVDS pixel backend that was written inside uCNC, on its own: panel
timing and scanout, a 4bpp indexed framebuffer, a sixteen-colour palette, and
the drawing primitives every screen is built from. It drives one panel - a
**Sharp LQ121S1LG44**, 800x600, single-channel LVDS, mounted turned a quarter -
and it has been on that panel and no other.

It was extracted from
`uCNC/src/modules/lvds_renderer` (see *Where this came from* below). The drawing
code, the palette and the fonts are the same code; the hardware half stopped
including `cnc.h` and calls four port hooks instead.

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

The full statement of who owns what is in [docs/boundaries.md](docs/boundaries.md).
What the hardware limits are - and they are not negotiable - is in
[LIMITS.md](LIMITS.md).

The panel is drawn **upright by default**. A glass bolted turned a quarter -
the machine's own mounting - is one build flag (`-DLVDS_PANEL_TURN=90`, see
`platformio.ini`), and the layout follows it.

**The palette is live.** A colour's name can be redefined while the picture
runs (`lvds_palette_define()`): on the glass the slot it already holds is
re-coloured in place, so everything drawn in that colour changes with it on the
next scanline, with no redraw and no slot churn. `scene_palette_switch()` in the
samples walks four banks to show it.

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
`.pio/build/rp2350_panel/firmware.uf2`. It prints four lines on the USB serial
when it comes up and then draws the same scene in a `while` loop.

`pio run -e rp2350_panel_fps -t upload` is the same sample with its **frame
meter** in: the loop runs flat out and every sixty frames it prints the
milliseconds a whole frame - clear, scene, present - takes. That is how the
project answers "is this fast enough for a game", on the panel, rather than
guessing from datasheets.

Two meters, because the answer depends on where the draw buffer lives:
`rp2350_panel_fps` draws into SRAM (the small, fast target), and
`rp2350_panel_psram_fps` adds the PSRAM backbuffer the machine uses, so the
print shows what the copy in `present()` costs. A serial terminal must assert
DTR (`pio device monitor` does; a bare port read may not) or the board's output
is dropped.

The samples have no RTOS, no tasks, no callbacks and no state machines: draw,
present, repeat. `samples/common/scene.c` is the whole demo, and it is the same
file for the desk and for the glass.

## What has been verified, and what has not

Verified here:

* `pio run -e rp2350_panel` builds the firmware sample and links it
  (RAM 56.8%, flash 0.4% of 16 MB, `firmware.uf2` generated).
* The portable half - core, host target and both host samples - compiles
  warning-free under `-Wall -Wextra` with the toolchain's ARM compiler.

Not verified here:

* **The host samples have not been run on this machine.** Its MinGW `gcc` cannot
  run its own `cc1` (missing prerequisite DLLs), so any host build fails before
  compiling a line. `make run` is one command away on a machine whose compiler
  works; the check inside `host_snapshot` is what makes that run meaningful.
* **Nothing has been on the glass from this project.** The panel has only ever
  been driven by the uCNC build. The scanout code is byte-identical to the
  module's, but the assembled project - its clocks, its PSRAM bring-up, the
  removed boot helper - has not been flashed.
* The frame-close tail, the clock points, panel readability: bench items, listed
  in `LIMITS.md`.

## Where this came from

`uCNC/src/modules/lvds_renderer` is the same renderer inside uCNC, and the
same directory - module and library, one copy - was published to the
`uCNC-modules` repository. This project is that code rearranged around a target
seam, with the four uCNC calls (`mcu_millis`, `cnc_delay_ms`, `proto_info`,
`cnc_is_file_io_critical`) replaced by `targets/rp2350/rp2350_port.h`.

**The two copies can drift.** They are the same code today; when this project
becomes the owner, the module should be generated from it or replaced by it
rather than edited beside it. That decision has not been made yet.
