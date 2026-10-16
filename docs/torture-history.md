# The torture harness, the retired workarounds, and what they measured

This file is history, and it exists for one reason: every path below was
switched off for a *measured* reason, and a reader who wants to switch one back
on has to know what it cost the first time. None of them is a supported
configuration, and none of them belongs in release code.

The rule the project holds to: an experiment is a branch, a measurement on the
glass, and a paragraph here - and it comes out of the tree again before the
change is called done. The uCNC module this renderer lives in carries the same
warnings under "Historical Warnings" and "Torture History" in its own README;
this is the project's own record of the same ground.

## The harness itself

`LVDS_HSTX_TORTURE_TEST` was a compile-time stress harness for the RP2350
scanout, and it is **removed**. While it existed it repeatedly exercised: a
selection of program runs, snapshot churn, framebuffer present, the historical
line-repeat injection, stack diagnostics and canary diagnostics, emitting
compact serial summaries while it ran.

It found what it was there to find - the memory and descriptor class of fault -
and then it stopped being a diagnostic and started being a configuration
surface. That is the failure mode this project is careful about: a torture knob
that can be turned on in a release build is a way to hide a realtime fault
behind a schedule. The same applies to the line-repeat watchdog, the fault
injection hooks, the canaries, the overlap checks, and the scanline
slack/timing instrumentation - all of them were release-build code at some
point, and all of them are gone.

## Retired workarounds, and why

| what was tried | why it is not here |
| --- | --- |
| PSRAM scanout, and PSRAM front/back buffers | the HSTX DMA reads the framebuffer every line: it was slow and it lost sync. Scanout is SRAM, and the framebuffer must stay there |
| DMA present from the PSRAM draw buffer into the SRAM framebuffer | measured at roughly **7.3 ms for 240,000 bytes**, and it produced display garbage even with HSTX DMA priority and the present DMA paced. `present()` is a CPU copy |
| chunked framebuffer present | a pacing experiment for earlier memory/scanout layouts; the strict-memory backend uses one normal copy |
| ping/pong DMA scanout handoff, and CPU-rearm scanout | replaced by the static descriptor ring. The old handoff is also where the release recovery code came from, and that went with it |
| drawing straight into the scanout framebuffer as the normal path | the panel reads the picture while it is being half redrawn: a screen clear per frame reads as a flicker. It survives as an explicit single-buffered build, never as a default |
| HSTX live health polling and auto-recovery | a stalled scanout is a memory, ownership or timing fault. Polling it turns a fault into a schedule; fix the fault |
| a line-repeat watchdog, and line-repeat fault injection | diagnostics, not configuration - and the line-repeat question is answered by four spare active lines, not by a watchdog |
| canary, overlap and torture diagnostics in release builds | same reason: an instrument left in release code is a thing that can be relied on instead of the architecture |
| scanline slack/timing log instrumentation in release builds | it cost scanout time to say how little scanout time there was |
| Scratch X/Y placement tricks for live HSTX state | the banked memory is not the place for permanent realtime state; the line buffers and ring are static and aligned in the main SRAM |
| renderer-owned generated G-code caches, and G-code generation inside the renderer | a boundary violation: the renderer owns pixels, not programs. It is why this project has no idea what a program is |

## The faults behind those decisions

Each of these cost real bench time, and each one is why something above is
retired:

- **A frozen scanout while the machine kept running.** Old logs showed uCNC and
  motion continuing while the display scanout was stopped. That class was a
  stalled HSTX DMA/control path - not a whole-core crash - and it is the reason
  the descriptor ring replaced the old ping/pong handoff, and the reason the
  release recovery path was removed rather than repaired.
- **The previous frame's tail across the top of the picture.** Core1 prepared
  from the current line instead of the line the ring reaches
  `ACTIVE_LINE_BUFFERS - 1` scanlines later, and nothing was prepared from the
  vertical blanking, so the first lines of every frame carried the last lines of
  the frame before. The wrap into the next frame is the fix, and it is load
  bearing.
- **A picture's own colours came out in greys.** Palette slots were being taken
  when a colour table was *parsed* rather than when a colour was first *used*,
  so a splash that brought its own palette found the table already full. Slots
  are taken on first use now, and a picture that takes slots gives them back.
- **The close of the frame repeated a line** (bench, 2026-10-04). This one is
  worth the whole story, because it is the shape a display fault takes:
  - the bench asked whether it was our timing or the panel's own closure, and
    ran two probes: **four extra blanking lines** changed **nothing** on the
    glass; **four extra active lines** put the picture's last lines **right**.
    So it is neither the frame's total nor the clock, but the close of the
    active area;
  - the first suspect was the one non-blanking line before the active area,
    which hands out an active-encoded, stale line buffer. Making it true
    blanking **changed nothing** on the glass, so that line went back as it was;
  - the probe that settled it: five **known** colours, one per line, on the
    frame's last five scanlines, with a break every 32 rows so a repeat would be
    visible. The bench read back *"and red is twice"* - our own content, one
    line of it shown twice. The panel or the link repeats a line at the close of
    the active area, whatever is on it;
  - walking the active height down measured how much room it needs: **600** puts
    the repeat on the picture, **602** shows an extra black line, **603** and
    **604** are clean. The frame keeps 604 - one line of margin over the three
    that are needed - and the extra lines are the frame's blank tail;
  - **the root cause is still unknown.** The tail is containment. Do not shorten
    the frame, and do not treat the number as tunable.
- **An upload that left the board with no port and no picture.** The meter build
  disappearing from the USB bus was read as a blocked CDC write taking the
  device down. It was not: on this bench an upload leaves the application down
  until **RESET** is pressed. The print guard stayed (a write nobody reads has
  nothing to do), but the diagnosis was the RESET, not the guard.

## What is verified where

Software-verified, and repeatable on any machine with the toolchains:

- `pio run -e rp2350_panel` builds and links the firmware sample
  (RAM ~57% of 512 KB, flash under 1% of 16 MB), as do the meter environments
  that extend it;
- the portable half - core, host target, both host samples - compiles
  `-Wall -Wextra` clean under the ARM toolchain, which is what proves the core
  has no hardware in it;
- the host target's frame checks are the project's own pixel-level check
  (`host_snapshot` exits non-zero when a pixel is not where the scene put it).
  They have **not been run on this bench machine**: its MinGW `gcc` cannot run
  its own `cc1` (missing prerequisite DLLs), so no host build works here at all.
  They compile, and `make run` is what runs them.

Bench-only, in the operator's hands - no software suite here proves any of it:

- panel readability, and the close-of-frame tail;
- the clock points (the measured list is in [../LIMITS.md](../LIMITS.md)); the
  one build that produced **no video** (`420000 / 3`) is a board/overclock
  boundary rather than a panel requirement;
- the two frame costs quoted throughout the docs - **3 ms** single buffered and
  **33 ms** through the present copy, about 283 fps in the first case - which
  come from the meter builds on the glass, not from a host sample.

## Running an experiment that belongs in this file

1. Branch. Never do it on the line the machine runs.
2. Add the harness behind a switch that cannot be left on by accident, and
   give it a serial summary that says what it saw.
3. Measure on the glass, not on a stopwatch in your head.
4. Write the number and the conclusion down here - including a negative result,
   because "the panel check came back negative" is what stops the next reader
   re-trying it.
5. Take the harness out before the change is called done.
