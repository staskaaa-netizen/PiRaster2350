# When to use PiRaster, and when not to

Read this section before you put PiRaster on a panel. The first part gives the
limits of this display path. The second part gives the job that the path fits.

## The limits of this display path

PiRaster is not a modern display path.

* **The link** is single-channel LVDS. It sends an 800x600 picture on three data
  pairs and one clock pair.
* **The picture** uses 4 bits for each pixel. The panel shows sixteen colours.
  There is no blending and no alpha.
* **The refresh** comes from the pixel clock and the porches in the panel
  header. It is about 57 Hz. Nothing adapts it.
* **There is no video path.** There is no decoder, no scaler and no image
  format. Text is a set of bitmap rectangles.
* **There is one panel and one board.** The geometry, the timing and the clocks
  of the Sharp LQ121S1LG44 are compiled in. The RP2350 target is the only target
  that drives a panel.

A modern display uses MIPI DSI, eDP or HDMI. Those links send 16 to 24 bits for
each pixel, and a hardware pipeline builds the picture. Use one of them for
video, for photographs, for a phone-like UI, or for a dashboard with millions of
colours.

## The job that this path fits

An operator panel on a machine needs different properties:

* **A stable picture.** The picture must not tear and must not flicker. An
  operator stops trusting a screen that flickers while it is drawn.
* **A small quantity of ink.** A header, a program pane, a status line and a key
  pad are a few thousand pixels. They change a few times each second.
* **No cost to the machine.** The scanout uses core1 and two DMA channels. Its
  memory is static, and it has no allocator. Core0 stays free for motion and for
  the protocol.
* **An immediate response.** There is no compositor, no driver queue and no GPU
  submission. The drawing writes the framebuffer that the scanout reads.
* **Low cost and easy supply.** An 800x600 LVDS panel and an RP2350 are cheap
  parts. The renderer is less than 3,000 lines and uses no third-party display
  stack.

PiRaster is for a **simple HMI with a fast and stable picture**, on a panel that
is already on the machine.

## Do

* Draw an operator screen with a fixed layout: a header, panes, a status line
  and a key pad.
* Draw text, lines, rectangles and filled rectangles.
* Show a state with a colour. Call `lvds_palette_define()` to change a slot
  while the picture runs. Everything that uses that colour changes on the next
  prepared line. You do not redraw the screen.
* Give a client's raster to `lvds_target_blit()`, one clipped row at a time. An
  emulator, a picture pump or a game with its own dirty boxes then draws at row
  speed.
* Check a layout on a desk. The host target writes the same picture into a frame
  in RAM and writes BMP files.
* Use the single buffered build for a still panel or for a measurement. It costs
  3 ms for a frame. It accepts tearing.

## Do not

* Do not draw photographs, video, gradients or anti-aliased text. A colour that
  is not in the table becomes the nearest colour that is.
* Do not use more than sixteen colours at the same time. The table is full by
  design. A picture that takes slots must give them back with
  `lvds_canvas_palette_reset()`.
* Do not expect layers, alpha, scaling or rotation of the content. Only the
  panel mounting turns, and one build flag sets that.
* Do not add a second panel or a second board to the same build. A second panel
  is a second header and a target that reads it.
* Do not draw straight into the scanout framebuffer for an operator screen. That
  is the fast path, and it tears. The default build gives the stable picture.
* Do not add recovery machinery to the scanout. No health polling, no watchdog,
  no auto-recovery, no retry loop and no chunked present. A stopped scanout is a
  fault. Fix the fault. See [torture-history.md](torture-history.md).
* Do not use PiRaster for a modern display. Use the DSI, eDP or HDMI path of
  that panel.

## Choose in one table

| the job | PiRaster | use this instead |
| --- | --- | --- |
| An operator screen for a machine: text and boxes, no flicker | Yes. Use the double buffered build. | - |
| A still picture or a timing measurement on the glass | Yes. Use the single buffered build. | - |
| A raster that another program makes (an emulator, a game, a picture pump) | Yes. Use the blit call. | - |
| The same screen without a board | Yes. Use the host target and its BMP files. | - |
| Photographs, video, a UI with millions of colours | No. | The DSI, eDP or HDMI path of the panel. |
| Two panels, or a different MCU | Not yet. | A second header and a second target. |

## One quantity that is not measured

The cost of a drawn frame is measured: 33 ms in the default build, and 3 ms in
the single buffered build. The end-to-end latency of the machine's own UI - a
key press to the pixel on the glass - is **not** measured. That latency depends
on the application: when it decides to redraw, and how much of the screen it
draws.

One fact is known without a measurement. Nothing queues between the drawing and
the glass: the drawing writes the framebuffer that the scanout reads. The
latency is therefore the cost of the drawing plus one frame of the scanout.
