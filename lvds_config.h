#ifndef LVDS_CONFIG_H
#define LVDS_CONFIG_H

/* The one place a build says which panel it drives.

   Everything else reads the panel's macros through this file, so a board that
   is built with `-DLVDS_PANEL_HEADER=\"my_panel.h\"` and the directory it
   lives in on the include path needs no other change. */

#ifndef LVDS_PANEL_HEADER
#define LVDS_PANEL_HEADER "sharp_lq121s1lg44.h"
#endif

#include LVDS_PANEL_HEADER

#endif /* LVDS_CONFIG_H */
