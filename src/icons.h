/* Shared artwork. The boat is the application's identity: it appears as
   the window icon, the desktop-entry icon, and the tray icon when the
   dongle is absent. */
#ifndef BVP_ICONS_H
#define BVP_ICONS_H

#include <cairo.h>
#include <stdbool.h>

/* Draws the boat filling a 64x64 box. `dark` picks a light-on-dark or
   dark-on-light rendering. */
void bvp_draw_boat(cairo_t *cr, bool dark);

/* Writes the boat as a PNG of the given size. Used to publish the
   application icon into the user's icon theme. */
bool bvp_write_boat_png(const char *path, int size);

#endif
