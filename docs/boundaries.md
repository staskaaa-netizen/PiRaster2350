# The boundaries, and why they are where they are

The project exists because one file - the module's `lvds_hstx.c`, 950 lines -
wrote pixels, drew text, ran core1 and programmed DMA. Nothing about it could be
reused or tested without the board, and the parts that were panel-specific and
the parts that were universal were mixed line by line.

The split below is by *ownership*, not by convenience. Each line describes
something that must not move without a reason that can be written down here.

## `core/` - the drawing, and nothing else

Owns: a colour, the view and its turn, the two writing primitives, the
primitives built on them, the palette's names and values, and the fonts.

The palette's *names and values* are the core's; the *slots* are the target's.
That split is what makes a live switch possible: redefining a name
(`lvds_palette_define()`) makes the core re-colour the slot it already holds
(`lvds_target_slot_set()`), so nothing is redrawn and no slot is wasted. A core
that re-took slots on every switch would fill the table up in sixteen moves.

Does not own: any hardware. It compiles for a host, for the boards (it is
`-Wall -Wextra` clean under an ARM toolchain) and for anything that can write
two primitives. It has no threads, no globals that grow, and no I/O.

Guarantees it gives a caller:

* out-of-bounds coordinates are dropped, never wrapped;
* a filled rectangle is one call, however big, because the turn maps a
  rectangle to a rectangle;
* a colour asked for by name is the same colour on every target.

Guarantees it does *not* give:

* that a colour reaches the glass exactly - the glass is 4bpp indexed, so a
  colour outside the palette becomes its nearest neighbour. A host target with
  no palette shows the ideal picture, and that difference is real.

## `core/lvds_target.h` - the seam

Nine calls. They were chosen so that the smallest interesting target is a few
dozen lines (see `targets/host/host_panel.c`) and the biggest one
(`targets/rp2350/rp2350_panel.c`) still does not know what a view is:

| call | who it belongs to | what it must not become |
| --- | --- | --- |
| `lvds_target_init` | the board | a place to retry or recover |
| `lvds_target_pixel` / `_fill_rect` | the framebuffer | anything with clipping to do |
| `lvds_target_clear` | the framebuffer | a loop over pixels |
| `lvds_target_blit` | the framebuffer | a place where a picture the caller already indexed is quantised again |
| `lvds_target_present` | the scanout | a queue, a thread or a DMA path |
| `lvds_target_slot` | the palette | a colour-quantiser above the target |
| `lvds_target_slot_set` | the palette | a new slot taken for a colour that has one |
| `lvds_target_palette_reset` | the palette | a no-op that a caller relies on |

If a target ever needs to know the view, or the core ever needs to know a
channel number, the seam is in the wrong place - fix the seam, not the caller.

## `panels/` - the numbers

A panel is data: geometry, timing, pins, clock. The frame's own height
(`LVDS_PANEL_ACTIVE_HEIGHT`) lives here too, because it is a fact about the
glass - the four spare active lines the close of the frame needs - and not a
decision the renderer may make.

## `targets/rp2350/` - the machine

Owns: HSTX, the pacer's LUT, the DMA descriptor ring, core1 and its line
preparation, the SRAM framebuffer, PSRAM bring-up, the palette's slots, and the
four port hooks.

The realtime rules are the module's and are unchanged: static aligned buffers,
nothing large and automatic on the core1 path, no PSRAM scanout, no DMA present,
no health polling and no auto-recovery. If the scanout stops, it is a fault.

The port (`rp2350_port.h`) is deliberately four things: a millisecond clock for
the core1-up timeout, a delay for PSRAM bring-up, a log function with a weak
no-op default, and a "do not spend time on a present now" hook (the machine's
own reason is an SD card read). None of them is in the scanout path.

The headless bake is still target-owned. `rp2350_panel_bake_line()` calls the
same active-line expansion that core1 calls; it does not create a second
encoder or move palette knowledge above the seam. The compile guard selects the
SRAM-resident probe and freezes the palette into its three line streams. No
core or sample code learns an HSTX opcode, channel number or descriptor layout.

That path is not a second general-purpose backend. It can reuse a few static
lines for a pattern probe, or expand a 200x120 four-colour source into 120
SRAM-resident output rows and repeat them through DMA. There is no recovery
state. The full-frame PSRAM version was measured as failure and stays retired;
both working modes keep every scanout word in SRAM.

Framebuffer packing is also target-owned. The core asks for an indexed colour;
the target packs it at four bits per pixel for sixteen colours or two bits per
pixel for four colours. The normal 2bpp path uses a target-local expansion
table before the same 7x256 LUT, so the core does not learn about packing or
scanline timing.

## `samples/` - one picture, the sample programs

`samples/common/scene.c` is the demo. Every sample draws it and does nothing
else; that is the whole point of the layout, and it is why the samples are
short. A sample that grows its own drawing code is a sign the core is missing
something - add it to the core instead.

## What is deliberately not here

* Files, menus, G-code, tool tables, machine safety: those were never the
  renderer's, and they stayed in uCNC.
* Images: the module's README argues that an image (its runs, its size, its
  colours, the palette slots it takes) belongs in the renderer. It is not in
  this project yet, and the boot splash that wanted it is an application, not a
  target.
* A second panel: adding one is the test of this split - a new header, and a
  target that can read it.
