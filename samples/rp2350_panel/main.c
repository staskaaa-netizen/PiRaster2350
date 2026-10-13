/* The same picture, on the glass.

   The drawing below is the shared scene, unchanged: this program differs from
   the host samples only in the target it links. There is no RTOS here either -
   the framework's `main()` is a `while (1) { loop(); }`, and this sample adds
   nothing to it but a call to draw and a call to present.

   Build it with `pio run -e rp2350_panel` from the project root. */

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "lvds_renderer.h"
#include "rp2350_panel.h"
#include "scene.h"

/* The backend's few init lines come out of the SDK's stdio: override the weak
   default in `rp2350_port.c` and they are printed instead of dropped. On this
   board that is the USB serial port the framework's own stdout uses. */
void lvds_port_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static int g_frame;

void setup(void)
{
    /* One call brings the panel up: the PSRAM (when this build has it), the
       HSTX pacer, core1, the descriptor ring. The drawing side knows none of
       that. */
    if (!lvds_canvas_init()) {
        /* Nothing came up. An empty loop is an honest answer; a retry loop
           here would only hide which of the two did not start. */
        printf("lvds: the panel did not come up\n");
        return;
    }
    printf("lvds: draw buffer in %s\n",
           rp2350_panel_backbuffer_in_psram() ? "psram" : "sram");
}

void loop(void)
{
    if (!lvds_canvas_ready()) {
        return;
    }
    scene_draw(g_frame++);
    lvds_canvas_present();
    delay(20);
}
