#ifndef HOST_PANEL_H
#define HOST_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The panel on a desk: the same drawing code, the same view, the same turn -
   on a 32bpp frame in memory instead of an HSTX scanout, so the picture can be
   looked at without a board.

   This target has no palette: `lvds_target_slot()` hands the 5-6-5 word
   straight back, because there is nowhere to put a sixteen-colour table. The
   picture is therefore the *ideal* one - what the drawing asked for. The glass
   is the 4bpp one, where a colour that is not in the table comes out as the
   nearest colour that is; that difference is measured on the bench, not here
   (see `LIMITS.md`).

   The frame is the glass, 800x600, in panel coordinates: what the panel is
   sent. `host_panel_write_bmp(..., true)` writes the *view* instead - the
   picture the operator reads, 600x800 with the turn applied - which is the
   honest way to look at a turned panel on a landscape screen. */

/* Write the frame as a 24bpp BMP. `as_view` turns it back a quarter, the way
   the operator reads it. */
bool host_panel_write_bmp(const char *path, bool as_view);

/* One pixel of the panel frame, 0xRRGGBB, for checks. Panel coordinates. */
uint32_t host_panel_pixel(int x, int y);

int host_panel_width(void);
int host_panel_height(void);

/* How many times the drawing has said "this frame is done". */
unsigned host_panel_presents(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_PANEL_H */
