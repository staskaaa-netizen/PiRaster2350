/* The smallest program that shows the whole thing.

   There is no RTOS, no thread, no interrupt and no callback here: a while
   loop that draws a frame, says it is done and writes what the glass would
   have shown. That is the shape every sample in this project has, including
   the firmware one.

   Build and run it with `make host_loop`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_panel.h"
#include "lvds_renderer.h"
#include "scene.h"

int main(int argc, char **argv)
{
    const char *path = "frame.bmp";
    int frames = 3;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            path = argv[++i];
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [-n frames] [-o out.bmp]\n", argv[0]);
            return 2;
        }
    }

    /* One call brings the target up. From here on there is nothing
       panel-specific in this program. */
    if (!lvds_canvas_init()) {
        fprintf(stderr, "host_loop: the target did not come up\n");
        return 1;
    }
    printf("host_loop: view %dx%d, panel %dx%d\n",
           lvds_canvas_width(), lvds_canvas_height(),
           host_panel_width(), host_panel_height());

    for (int frame = 0; frame < frames; frame++) {
        /* The scene is drawn from a moment in time, so a frame here is a
           hundred milliseconds later than the last one. */
        uint32_t now_ms = (uint32_t)frame * 100u;

        scene_palette_switch(scene_palette_phase(now_ms));
        scene_draw(now_ms);
        lvds_canvas_present();
        if (!host_panel_write_bmp(path, true)) {
            fprintf(stderr, "host_loop: cannot write %s\n", path);
            return 1;
        }
        printf("host_loop: frame %d (t %lu ms), palette %d, written to %s\n",
               frame, (unsigned long)now_ms, scene_palette_phase(now_ms), path);
    }
    printf("host_loop: %u present(s)\n", host_panel_presents());
    return 0;
}
