/* Icône de zone de notification.

   Three states, drawn on the fly and written out as PNG (the AppIndicator
   API only takes a file path, not a pixbuf):
     - dongle absent    : a sailing boat
     - headset present  : a headset with the battery percentage
     - charging         : same plus a green bolt, percentage shrunk so it
                          still fits */
#ifndef BVP_TRAY_H
#define BVP_TRAY_H

#include <gtk/gtk.h>
#include "device.h"

void bvp_tray_init(GtkWidget *window);
void bvp_tray_update(bvp_status st);

#endif
