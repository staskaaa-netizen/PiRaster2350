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

#ifndef LVDS_HEADLESS_SAMPLE
#define LVDS_HEADLESS_SAMPLE 0
#endif

#ifndef LVDS_SAMPLE_4COLOR
#define LVDS_SAMPLE_4COLOR 0
#endif

#ifndef LVDS_SAMPLE_1COLOR
#define LVDS_SAMPLE_1COLOR 0
#endif

#define FRAME_MS 20            /* the loop's own pace, ~50 frames a second */
#define LOWRES_FRAME_MS 100    /* the 37 Hz blank window can absorb 10 fps */

static uint32_t g_next_frame_ms;
#if LVDS_HEADLESS_LOWRES
static uint32_t g_next_lowres_ms;
#endif

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

/* The state line, said once for each terminal that attaches.

   `setup()` is usually too early for it: the framework drops CDC output until
   the host asserts DTR, so a monitor that is opened after the board started
   hears nothing at all. `loop()` therefore says the same thing again while the
   flag is clear, and clears the flag when the terminal goes away - once for
   each terminal, not once for each frame. */
static bool g_console_said;

static void console_announce(void)
{
    if (!Serial) {
        return;
    }
    if (!lvds_canvas_ready()) {
        Serial.println("lvds: the panel did not come up");
    } else {
        Serial.printf("lvds: draw buffer in %s",
                      rp2350_panel_backbuffer_in_psram() ? "psram" : "sram");
#if LVDS_HEADLESS_SAMPLE
#if LVDS_HEADLESS_LOWRES
        Serial.print(", 200x120 four-colour full-screen, core1 idle");
#else
        Serial.print(", headless SRAM line stream, core1 idle");
#endif
#endif
#if LVDS_SAMPLE_FPS
        Serial.print(", frame meter on, 115200 8N1");
#endif
#if LVDS_SAMPLE_4COLOR
        Serial.print(", native 800x600, four colours");
#endif
#if LVDS_SAMPLE_1COLOR
        Serial.print(", native 800x600, green mono 1bpp");
#endif
        Serial.println();
    }
    g_console_said = true;
}

#if LVDS_HEADLESS_SRAM_BARS
static void draw_colour_bars(void)
{
    static const lvds_palette_color_id_t bars[] = {
        LVDS_COL_WHITE, LVDS_COL_YELLOW, LVDS_COL_GREEN, LVDS_COL_BLUE,
        LVDS_COL_RED, LVDS_COL_TOOL_ORANGE, LVDS_COL_GRAY_128, LVDS_COL_BLACK
    };
    int w = lvds_canvas_width();
    int h = lvds_canvas_height();

    for (unsigned i = 0; i < sizeof(bars) / sizeof(bars[0]); i++) {
        int x0 = (int)((long)i * w / (long)(sizeof(bars) / sizeof(bars[0])));
        int x1 = (int)((long)(i + 1u) * w /
                       (long)(sizeof(bars) / sizeof(bars[0])));
        lvds_canvas_fill_rect(x0, 0, x1 - x0, h,
                              lvds_palette_color(bars[i]));
    }
}
#endif

void setup(void)
{
    Serial.begin(115200);

    /* One call brings the target up: PSRAM, palette, clocks and either the
       core1 descriptor path or, under `LVDS_HEADLESS`, nothing until a baked
       frame is ready. */
    if (!lvds_canvas_init()) {
        /* Nothing came up. An empty loop is an honest answer; a retry loop
           here would only hide which of the two did not start. */
        console_announce();
        return;
    }
#if LVDS_SAMPLE_4COLOR
    scene_palette_four_colours();
#endif
#if LVDS_SAMPLE_1COLOR
    scene_palette_green_mono();
#endif

#if LVDS_HEADLESS_SAMPLE
#if LVDS_HEADLESS_LOWRES
    scene_draw_lowres(1234u);
    lvds_canvas_present();
    if (!rp2350_panel_headless_lowres_start()) {
        if (Serial) {
            Serial.println("lvds: the 200x120 stream did not start");
        }
        return;
    }
    g_next_lowres_ms = millis();
#elif LVDS_HEADLESS_SRAM_BARS
    draw_colour_bars();
    lvds_canvas_present();

    if (!rp2350_panel_headless_sram_bars_start()) {
        if (Serial) {
            Serial.println("lvds: the SRAM bar stream did not start");
        }
        return;
    }
#else
#error Headless mode needs a working source raster
#endif
#endif

    console_announce();
}

void loop(void)
{
    uint32_t now;

    /* A terminal that arrives after the boot lines were dropped still gets the
       state of the board. */
    if (!Serial) {
        g_console_said = false;
    } else if (!g_console_said) {
        console_announce();
    }

    if (!lvds_canvas_ready()) {
        return;
    }
#if LVDS_HEADLESS_SAMPLE
#if LVDS_HEADLESS_LOWRES
    now = millis();
    if ((int32_t)(now - g_next_lowres_ms) >= 0 &&
        rp2350_panel_headless_lowres_can_rebake()) {
        scene_draw_lowres(now);
        rp2350_panel_headless_lowres_rebake();
        g_next_lowres_ms = now + LOWRES_FRAME_MS;
    }
#endif
    return;
#endif
    now = millis();
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

    /* The scene is a function of the clock, so it moves as often as the loop
       manages to draw and no faster; the palette bank follows the clock too.
       The slots are re-coloured in place, so the picture already on the glass
       changes with the switch, and the redraw that follows is only the scene
       moving on. */
#if !LVDS_SAMPLE_4COLOR && !LVDS_SAMPLE_1COLOR
    scene_palette_switch(scene_palette_phase(now));
#endif
    scene_draw(now);
#if LVDS_SAMPLE_FPS
    fps_report();
#endif
    lvds_canvas_present();
}
