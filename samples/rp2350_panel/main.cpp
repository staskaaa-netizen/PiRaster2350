/* The same picture, on the glass.

   The drawing below is the shared scene, unchanged: this program differs from
   the host samples only in the target it links. There is no RTOS here either -
   the framework's `main()` is a `while (1) { loop(); }`, and this sample adds
   nothing to it but a call to draw and a call to present.

   This one file is C++ because the board's stack is the Arduino core and the
   board's serial port is the framework's own `Serial`. Everything it calls -
   the renderer, the target, the scene - is C, and its headers are `extern "C"`.

   Build it with `pio run -e rp2350_panel` from the project root. */

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "lvds_renderer.h"
#include "rp2350_panel.h"
#include "scene.h"

/* A frame meter, off unless the build asks for it (the `rp2350_panel_fps`
   environment does). It is here because "can this run a game" is a question
   about milliseconds, and the only honest place to answer it is the panel: with
   the meter in, the loop runs flat out and prints what a whole frame - clear,
   the scene, present - costs. Leave it out of a build meant to be looked at. */
#ifndef LVDS_SAMPLE_FPS
#define LVDS_SAMPLE_FPS 0
#endif

#define SCENE_STEP_MS 125      /* the moving disc: eight steps a second */
#define SCENE_PALETTE_MS 3000  /* a palette bank every three seconds */
#define FRAME_MS 20            /* the loop's own pace, ~50 frames a second */

static uint32_t g_next_frame_ms;

#if LVDS_SAMPLE_FPS
#define FPS_FRAMES 60
static uint32_t g_fps_start_ms;
static uint32_t g_fps_frames;

static void fps_report(void)
{
    uint32_t now = millis();
    uint32_t span;

    if (g_fps_frames == 0) {
        g_fps_start_ms = now;
    }
    if (++g_fps_frames < FPS_FRAMES) {
        return;
    }
    /* Nobody listening, nothing to say: a write with no terminal attached can
       block on the CDC and take the USB device down with it - the panel goes
       on drawing, but the board leaves the bus. */
    if (!Serial) {
        g_fps_frames = 0;
        return;
    }
    span = now - g_fps_start_ms;
    if (span == 0) {
        span = 1;
    }
    Serial.printf("lvds: %lu frames in %lu ms - %lu fps, %lu ms a frame\n",
                  (unsigned long)FPS_FRAMES, (unsigned long)span,
                  (unsigned long)((uint32_t)FPS_FRAMES * 1000ul / span),
                  (unsigned long)(span / FPS_FRAMES));
    g_fps_frames = 0;
}
#endif

/* The backend's init lines, and anything this sample has to say, come out of
   the USB serial. Overriding the weak default in `rp2350_port.c` is the whole
   of the hook. */
extern "C" void lvds_port_log(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (Serial) {
        Serial.print(buf);
    }
}

void setup(void)
{
    Serial.begin(115200);

    /* One call brings the panel up: the PSRAM (when this build has it), the
       HSTX pacer, core1, the descriptor ring. The drawing side knows none of
       that. */
    if (!lvds_canvas_init()) {
        /* Nothing came up. An empty loop is an honest answer; a retry loop
           here would only hide which of the two did not start. */
        Serial.println("lvds: the panel did not come up");
        return;
    }
    Serial.printf("lvds: draw buffer in %s\n",
                  rp2350_panel_backbuffer_in_psram() ? "psram" : "sram");
}

void loop(void)
{
    uint32_t now = millis();

    if (!lvds_canvas_ready()) {
        return;
    }
#if !LVDS_SAMPLE_FPS
    /* Paced by the clock, not by the frame count: a demo whose speed depends
       on how fast the loop happens to run is a different demo on every build.
       The frame meter build skips the pace, because what it measures is the
       cost of a frame. */
    if ((int32_t)(now - g_next_frame_ms) < 0) {
        return;
    }
    g_next_frame_ms = now + FRAME_MS;
#endif

    /* The palette bank changes with the clock too. The slots are re-coloured
       in place, so the picture already on the glass changes with it, and the
       redraw that follows is only the scene moving on. */
    scene_palette_switch((int)(now / SCENE_PALETTE_MS));
    scene_draw((int)(now / SCENE_STEP_MS));
#if LVDS_SAMPLE_FPS
    fps_report();
#endif
    lvds_canvas_present();
}
