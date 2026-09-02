/* Desktop integration: publishes the application icon into the user's
   icon theme, a menu entry, and optionally an autostart entry. All under
   ~/.local and ~/.config, so no root is needed. */
#ifndef BVP_DESKTOP_H
#define BVP_DESKTOP_H

#include <stdbool.h>

#define BVP_APP_ID "blackbeard-void-pro"

/* Writes the icon and the menu entry if they are missing. Cheap enough to
   call on every start. */
void bvp_desktop_install(void);

bool bvp_desktop_autostart_enabled(void);
void bvp_desktop_set_autostart(bool enabled, bool headless);

/* Absolute path of the published PNG icon (owned by the caller). */
char *bvp_desktop_icon_path(void);

#endif
