/* One frame, checked and written: the sample that is also a check.

   It draws the shared scene once, asks the host target what actually landed
   on the frame, and fails with a non-zero status if it is not what the scene
   asked for. That is a regression test for the two primitives everything else
   is built on, and it needs nothing but a C compiler. */

#include <stdio.h>

#include "host_panel.h"
#include "lvds_renderer.h"
#include "scene.h"

static int failures;

static void expect(const char *what, uint32_t got, uint32_t want)
{
    if (got != want) {
        printf("snapshot: FAIL %s: got 0x%06lx want 0x%06lx\n", what,
               (unsigned long)got, (unsigned long)want);
        failures++;
    } else {
        printf("snapshot: ok   %s\n", what);
    }
}

static uint32_t panel_at_view(int vx, int vy)
{
    return host_panel_pixel(LVDS_PANEL_X(vx, vy), LVDS_PANEL_Y(vx, vy));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "snapshot.bmp";

    if (!lvds_canvas_init()) {
        fprintf(stderr, "snapshot: the target did not come up\n");
        return 1;
    }
    scene_draw(0);
    lvds_canvas_present();

    /* Sixteen colours and no blending: what the scene asked for is what the
       host target holds, so a check can ask for the same colour again. */
    expect("background",
           panel_at_view(lvds_canvas_width() / 2, lvds_canvas_height() - 8),
           lvds_rgb888(lvds_palette_color(SCENE_BACKGROUND_COLOR)));
    expect("the view's own edge", panel_at_view(0, 0),
           lvds_rgb888(lvds_palette_color(SCENE_BORDER_COLOR)));
    expect("the border's other corner",
           panel_at_view(lvds_canvas_width() - 1, lvds_canvas_height() - 1),
           lvds_rgb888(lvds_palette_color(SCENE_BORDER_COLOR)));
    expect("a swatch, through the turn",
           panel_at_view(SCENE_SWATCH_X + 4, SCENE_SWATCH_Y + 4),
           lvds_rgb888(lvds_palette_color(LVDS_COL_GRAY_192)));
    expect("the moving disc, on frame 0",
           panel_at_view(SCENE_MOVING_BAR_X, SCENE_MOVING_BAR_Y),
           lvds_rgb888(lvds_palette_color(LVDS_COL_GREEN)));

    if (!host_panel_write_bmp(path, true)) {
        fprintf(stderr, "snapshot: cannot write %s\n", path);
        return 1;
    }
    printf("snapshot: %d failure(s), frame written to %s\n", failures, path);
    return failures ? 1 : 0;
}
