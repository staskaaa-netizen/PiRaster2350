# Torture tests, retired workarounds and measurements

This file is a record. Each path in it was switched off after a measurement.
None of these paths is a supported configuration, and none of them belongs in
release code.

The rule of the project: an experiment lives on a branch. You measure it on the
glass and you write the result in this file. You remove the experiment before
the change is complete. The uCNC module that carries this renderer has the same
warnings in its own README, under "Historical Warnings" and "Torture History".

## The test harness

`LVDS_HSTX_TORTURE_TEST` was a compile-time test harness for the RP2350 scanout.
It is **removed**.

The harness repeated these operations:

* selected program runs;
* snapshot churn;
* framebuffer present;
* the historical line-repeat injection;
* stack diagnostics;
* canary diagnostics.

The harness sent short summaries on the serial port. It found the memory and
descriptor faults that it was written for. After that it became a configuration
surface, and that is the danger: a test knob that a release build can turn on is
a way to schedule a fault instead of repairing it. The same is true of the
line-repeat watchdog, the fault injection hooks, the canaries, the overlap
checks and the scanline slack/timing instrumentation. All of them were release
build code at some time. All of them are gone.

## Retired workarounds

| what was tried | why it is not here |
| --- | --- |
| PSRAM scanout, and PSRAM front/back buffers | The HSTX DMA reads the framebuffer for every line. The path was slow, and it lost sync. The scanout uses SRAM, and the framebuffer must stay in SRAM. |
| DMA present from the PSRAM draw buffer into the SRAM framebuffer | It measured about **7.3 ms for 240,000 bytes**, and it put garbage on the glass. HSTX DMA priority and a paced present DMA did not help. `present()` is a CPU copy. |
| Chunked framebuffer present | This was a pacing experiment for older memory layouts. The current backend uses one copy. |
| Ping/pong DMA scanout handoff, and CPU-rearm scanout | The static descriptor ring replaced both. The release recovery code went with them. |
| Drawing straight into the scanout framebuffer as the normal path | The panel reads the picture while it is half drawn. A screen clear for each frame reads as a flicker. The build option `LVDS_TARGET_USE_PSRAM=0` keeps this fast path for a still panel. It is never the default. |
| HSTX live health polling and auto-recovery | A stopped scanout is a memory, ownership or timing fault. Polling turns a fault into a schedule. Fix the fault. |
| A line-repeat watchdog, and line-repeat fault injection | These are diagnostics, not configuration. Four spare active lines answer the line-repeat question, not a watchdog. |
| Canary, overlap and torture diagnostics in release builds | An instrument in release code can become a substitute for the architecture. It must not. |
| Scanline slack/timing log instrumentation in release builds | It used scanout time to report how little scanout time was available. |
| Scratch X/Y placement tricks for live HSTX state | Scratch X/Y is not the place for permanent realtime state. The line buffers and the ring are static and aligned in the main SRAM. |
| Renderer-owned generated G-code caches, and G-code generation inside the renderer | This broke the boundary. The renderer owns pixels, not programs. PiRaster has no knowledge of a program. |

## The faults behind those decisions

Each fault below cost bench time, and each one is the reason for a line in the
table above.

### A stopped scanout while the machine kept running

Old logs showed uCNC and the motion running while the display scanout was
stopped. The cause was a stalled HSTX DMA/control path, not a crash of the
machine core. This fault is the reason that the descriptor ring replaced the old
ping/pong handoff, and the reason that the release recovery path was removed
instead of repaired.

### The tail of the previous frame across the top of the picture

Core1 prepared the line of the current descriptor. It did not prepare the line
that the ring reaches after `ACTIVE_LINE_BUFFERS - 1` scanlines, and nothing was
prepared from the vertical blanking. The first lines of each frame therefore
carried the last lines of the previous frame. The wrap into the next frame is
the fix. Do not remove it.

### A picture with its own colours came out in greys

The code took palette slots when it parsed a colour table. It did not take them
when a colour was first used. A splash with its own palette therefore found a
full table. Slots are taken on first use now, and a picture gives its slots back
when it is done.

### The close of the frame repeated one line (bench, 2026-10-04)

This fault is still open. The steps below are the record of the measurements:

1. The bench asked whether the fault was in our timing or in the panel. It ran
   two probes. **Four extra blanking lines** changed nothing on the glass.
   **Four extra active lines** made the last lines of the picture correct. The
   fault is therefore at the close of the active area. It is not the frame
   total, and it is not the clock.
2. The first suspect was the one line that is not blanking, immediately before
   the active area. That line hands out an active-encoded and stale line buffer.
   The bench made it true blanking, and **the glass did not change**. The line
   went back as it was.
3. The probe that settled it: five **known** colours, one colour for each of the
   last five scanlines, with a break every 32 rows so that a repeat is visible.
   The bench read back *"and red is twice"*. Our own content, with one line
   shown twice. The panel or the link repeats a line at the close of the active
   area, whatever the content is.
4. The bench walked the active height down to find the quantity that is needed.
   **600** lines put the repeat on the picture. **602** lines showed one extra
   black line. **603** and **604** lines were clean.
5. The frame keeps **604** active lines. That is one line of margin over the
   three that are necessary. The four spare lines are the blank tail of the
   frame.

The root cause is still not known. The tail is containment, and it is written
down as a feature of the panel support. **Do not shorten the frame.** The
measurements are also in [../LIMITS.md](../LIMITS.md).

### An upload that left the board with no port and no picture

The meter build disappeared from the USB bus. The first diagnosis was a blocked
CDC write that took the device down. That diagnosis was wrong. On this bench, an
upload leaves the application down until somebody presses **RESET**. The print
guard stayed in the code, for its own reason: a write that nobody reads has
nothing to do.

## What is verified where

Software-verified, and reproducible on any machine with the toolchains:

* `pio run -e rp2350_panel` builds and links the firmware sample. It uses about
  57% of the 512 KB RAM and less than 1% of the 16 MB flash. The meter
  environments that extend this sample also build.
* The portable half - the core, the host target and the two host samples -
  compiles `-Wall -Wextra` clean with the ARM toolchain. That compile proves
  that the core holds no hardware.
* The frame checks of the host target are the pixel-level check of the project:
  `host_snapshot` exits with a non-zero code when a pixel is not where the scene
  put it. These checks have **not** been run on this bench machine. Its MinGW
  `gcc` cannot run its own `cc1` (prerequisite DLLs are absent), so no host
  build works there at all. The samples compile, and `make run` runs them.

Bench-only, in the hands of the operator. No software suite here proves any of
these items:

* the readability of the panel, and the close-of-frame tail;
* the clock points. The measured list is in [../LIMITS.md](../LIMITS.md). The
  point `420000 / div 3` produced **no video**. That is a boundary of this
  board, not a requirement of the panel;
* the two frame costs that the documents quote: **3 ms** single buffered and
  **33 ms** through the present copy, which is about 283 fps in the first case.
  These values come from the meter builds on the glass, not from a host sample.

## How to run an experiment that belongs in this file

1. Make a branch. Never run the experiment on the line that the machine uses.
2. Add the harness behind a switch that cannot stay on. Give it a serial
   summary that says what it saw.
3. Measure on the glass.
4. Write the number and the conclusion in this file. Write a negative result
   too: "the panel check came back negative" is what stops the next reader from
   trying the same path again.
5. Remove the harness before the change is complete.
