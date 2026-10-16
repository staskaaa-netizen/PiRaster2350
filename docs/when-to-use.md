# What PiRaster is good for, and what it is not

## The uncomfortable part first

This is old display technology, and it is worth saying so plainly.

- **The link** is single-channel LVDS: a parallel RGB stream carried on three
  data pairs and a clock pair, at 800x600. The modern world went to MIPI DSI,
  eDP and HDMI, which carry 16 to 24 bits per pixel over one or two
  differential pairs and are driven by a hardware pipeline rather than by a CPU.
- **The picture** is 4bpp indexed: sixteen colours, no blending, no alpha, no
  anti-aliasing, no scaling, no layers. Sixteen colours in four bits per pixel
  is an EGA-era picture. A modern panel shows 16.7 million colours and expects
  its pipeline to composite them.
- **The refresh** is fixed by a clock and a set of porches: something like 57 Hz
  by arithmetic from the panel header, at a 40 MHz pixel clock. Nothing here
  adapts it; there is no variable refresh, no frame pacing, no vsync interrupt
  to wait on.
- **There is no video path at all.** No decoder, no scaler, no image format
  support, no font rasteriser. Text is bitmap rectangles.
- **It is one panel on one board.** The geometry, timing, pins and clocks of the
  Sharp LQ121S1LG44 are compiled in, and the only target that drives glass is
  the RP2350's HSTX.

So if the job is a modern display - video, photographs, a phone-like UI, a
16-million-colour dashboard - this is not the thing, and no amount of tuning
will make it the thing. The panel's own path (DSI, eDP, HDMI) is the thing.

## Why it can still be the right answer

An operator panel on a machine is not a phone. What it actually asks for is
almost the opposite of what a modern display stack optimises:

- **the picture must never tear and never flicker.** A machine's status screen
  that flickers while it is redrawn is a screen an operator stops trusting. The
  default build pays 33 ms a frame - a full-frame copy out of a PSRAM
  backbuffer - purely so the scanout never reads a half-drawn picture;
- **the UI is text and rectangles.** A header, a program pane, a status line, a
  key pad: a few thousand pixels of ink that change a few times a second at
  most. Sixteen colours is not a limitation for that; it is a palette that fits
  in a nibble and a table that fits in a cache line;
- **the display must not borrow the machine's realtime time.** The scanout is
  core1 plus two DMA channels, with static memory and no allocator; core0 is
  free to run motion, the protocol and the UI. Nothing about drawing a screen
  can stall a move;
- **the response has to be immediate and predictable.** There is no compositor,
  no driver queue, no GPU submission and no window system: whoever draws is the
  last writer of the framebuffer, and the cost of a redraw is a number that was
  measured (3 ms single buffered, 33 ms for the stable path), not a number that
  depends on what else the system is doing;
- **and it has to be cheap and available.** An 800x600 LVDS panel and an RP2350
  are surplus-priced parts, and the whole renderer is under 3,000 lines with no
  third-party display stack to keep current.

That is the fit: a **simple, high-responsiveness HMI** - a machine's own
screen, on a panel that is already bolted to the machine.

## Do

- A fixed-layout operator screen: header, panes, status line, key pad - text,
  boxes, lines, filled rectangles, a handful of named colours.
- **State by colour.** The palette is live: `lvds_palette_define()` re-colours a
  slot in place, so everything already drawn in that colour changes with it on
  the next prepared line. A status that switches colour is one call, not a
  redraw.
- **A client's own raster, a row at a time.** `lvds_target_blit()` takes
  clipped rows of palette indices; an emulator, a picture pump or a game with
  its own dirty boxes draws at row speed instead of pixel speed.
- **The same picture on a desk.** The host target implements the same seam into
  a 32bpp frame in RAM and writes BMPs in the operator's orientation, so a
  layout or a redraw can be looked at, and checked pixel by pixel, without a
  board.
- **A picture that must keep scanning while the CPU is busy**, because the
  scanout does not depend on the drawing code running.
- **A still panel, or a measurement**, with the single-buffered build: 3 ms a
  frame, and tearing accepted as the price.

## Do not

- **Photographs, video, gradients, anti-aliased type.** Sixteen colours, no
  blending; a colour that is not in the table comes out as the nearest colour
  that is.
- **More than sixteen colours at once.** The table is full by design. A picture
  that brings colours of its own takes slots and must give them back
  (`lvds_canvas_palette_reset()`), or the screen after it is drawn in what was
  left behind.
- **A compositor's habits**: layers, alpha, scaling, arbitrary content
  rotation. The *panel's* mounting is a build flag; nothing else turns.
- **A second panel in the same build, or another board without a target.** A
  second panel is a second header *and* a target that can read it; the RP2350
  target is written against this board's HSTX, DMA and PSRAM.
- **Drawing straight into the scanout framebuffer for a UI.** It is the fast,
  tearing path and it is never the default for a reason: a screen clear per
  frame reads as a flicker on the glass.
- **Recovery machinery on the scanout** - health polling, watchdogs,
  auto-recovery, retry loops, chunked present. Every one of them was tried and
  retired with a measurement; a stalled scanout is a fault to fix, not to hide
  (see [torture-history.md](torture-history.md)).
- **A modern display's job.** If the panel is DSI, eDP or HDMI with 16 to 24
  bits per pixel, use that panel's own path.

## Choosing in one table

| the job | this project | what to use instead |
| --- | --- | --- |
| a machine's operator screen, text and boxes, must not flicker | yes - double buffered | - |
| a still picture or a timing measurement on the glass | yes - single buffered | - |
| a raster someone else already made (emulator, game, picture pump) | yes - the blit seam | - |
| the same screen checked without a board | yes - the host target and its BMPs | - |
| photographs, video, 16-million-colour UI | no | the panel's DSI/eDP/HDMI path |
| two panels, or another MCU | not yet | a second header and a second target - that is the split's own test |

## One thing that is not measured

The cost of a drawn frame is measured (3 ms and 33 ms, on the panel). What has
*not* been measured here is the end-to-end latency of the machine's own UI -
a key press to the pixel on the glass - because that depends on the application
above the renderer (when it decides to redraw, and how much of the screen it
touches). What can be said without measuring it is that nothing between the
drawing and the glass queues: the drawing writes the framebuffer the scanout
reads, so the latency is the drawing's own cost plus one frame the scanout needs
to come round.
