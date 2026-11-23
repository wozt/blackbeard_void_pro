#ifndef BVP_WOZT_UI_H
#define BVP_WOZT_UI_H

#include <gtk/gtk.h>

typedef struct WoztUI WoztUI;

/* Creates a borderless, movable and eight-way resizable GTK3 window shell. */
WoztUI *wozt_ui_new(GtkWidget *window, const char *icon_path);
GtkWidget *wozt_ui_body(WoztUI *ui);
void wozt_ui_on_language_change(WoztUI *ui, void (*callback)(gpointer), gpointer user_data);
const char *wozt_ui_tr(WoztUI *ui, const char *english);
void wozt_ui_retranslate(WoztUI *ui);

#endif
