#include "tray.h"

#include <cairo.h>
#include "icons.h"
#include <libayatana-appindicator/app-indicator.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>

#define ICON_PX 64

static AppIndicator *indicator = NULL;
static GtkWidget    *main_window = NULL;
static char         *icon_dir = NULL;
static int           icon_serial = 0;

static void draw_headset(cairo_t *cr)
{
    cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, 1.0);
    cairo_set_line_width(cr, 4.0);
    /* headband */
    cairo_arc(cr, 32, 30, 18, M_PI, 2 * M_PI);
    cairo_stroke(cr);
    /* earcups */
    cairo_rectangle(cr, 10, 28, 9, 16);
    cairo_rectangle(cr, 45, 28, 9, 16);
    cairo_fill(cr);
}

/* Rouge a 0 %, jaune a mi-course, vert a 100 % : la couleur seule suffit
   a lire l'etat sans dechiffrer les chiffres. */
static void battery_rgb(int pct, double *r, double *g, double *b)
{
    double t = CLAMP(pct, 0, 100) / 100.0;
    if (t < 0.5) {          /* rouge -> jaune */
        *r = 1.0;
        *g = t * 2.0;
    } else {                /* jaune -> vert */
        *r = (1.0 - t) * 2.0;
        *g = 1.0;
    }
    *b = 0.05;
}

static void draw_text(cairo_t *cr, const char *txt, double size, double cy,
                      double cr_, double cg_, double cb_)
{
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, txt, &ext);
    cairo_move_to(cr, 32 - ext.width / 2 - ext.x_bearing, cy);
    /* dark outline, so it stays legible on light and dark panels alike */
    cairo_text_path(cr, txt);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
    cairo_set_line_width(cr, 3.0);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, cr_, cg_, cb_);
    cairo_fill(cr);
}

static void draw_bolt(cairo_t *cr)
{
    cairo_set_source_rgb(cr, 0.20, 0.87, 0.30);
    cairo_move_to(cr, 35, 45);
    cairo_line_to(cr, 28, 54);
    cairo_line_to(cr, 33, 54);
    cairo_line_to(cr, 30, 62);
    cairo_line_to(cr, 39, 51);
    cairo_line_to(cr, 34, 51);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* Renders the current state and returns the PNG path (caller frees). The
   name changes every time: AppIndicator ignores an update to an identical
   path even when the file contents changed. */
static char *render_icon(bvp_status st)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    ICON_PX, ICON_PX);
    cairo_t *cr = cairo_create(s);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (!st.dongle_present) {
        bvp_draw_boat(cr, true);
    } else if (st.percent < 0) {
        draw_headset(cr);
    } else {
        char txt[16];
        snprintf(txt, sizeof(txt), "%d", CLAMP(st.percent, 0, 100));
        double r, g, b;
        battery_rgb(st.percent, &r, &g, &b);
        draw_headset(cr);
        if (st.state == BVP_BAT_CHARGING) {
            /* police reduite pour laisser la place a l'eclair */
            draw_text(cr, txt, 21, 42, r, g, b);
            draw_bolt(cr);
        } else {
            draw_text(cr, txt, 30, 58, r, g, b);
        }
    }
    cairo_destroy(cr);

    char *path = g_strdup_printf("%s/icon_%d.png", icon_dir, icon_serial++);
    cairo_surface_write_to_png(s, path);
    cairo_surface_destroy(s);
    return path;
}

static void on_show(GtkMenuItem *item, gpointer data)
{
    gtk_window_present(GTK_WINDOW(main_window));
}

static void on_quit(GtkMenuItem *item, gpointer data)
{
    GtkApplication *gapp = gtk_window_get_application(GTK_WINDOW(main_window));
    if (gapp)
        g_application_quit(G_APPLICATION(gapp));
    else
        gtk_main_quit();
}

void bvp_tray_init(GtkWidget *window)
{
    main_window = window;
    icon_dir = g_build_filename(g_get_user_runtime_dir(),
                                "blackbeard_void_pro", NULL);
    g_mkdir_with_parents(icon_dir, 0700);

    GtkWidget *menu = gtk_menu_new();
    GtkWidget *mi_show = gtk_menu_item_new_with_label("Open");
    GtkWidget *mi_quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(mi_show, "activate", G_CALLBACK(on_show), NULL);
    g_signal_connect(mi_quit, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_show);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_quit);
    gtk_widget_show_all(menu);

    indicator = app_indicator_new("blackbeard-void-pro", "audio-headset",
                                  APP_INDICATOR_CATEGORY_HARDWARE);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
}

void bvp_tray_update(bvp_status st)
{
    if (!indicator)
        return;

    /* Re-rendering on every poll would rewrite a file every 10 s for
       nothing: the icon only depends on presence, state and level. */
    static bool  have_last = false;
    static bool  last_present;
    static int   last_percent;
    static bvp_battery_state last_state;
    if (have_last && st.dongle_present == last_present &&
        st.percent == last_percent && st.state == last_state)
        return;
    have_last    = true;
    last_present = st.dongle_present;
    last_percent = st.percent;
    last_state   = st.state;

    char *path = render_icon(st);
    char *dir  = g_path_get_dirname(path);
    char *base = g_path_get_basename(path);
    if (g_str_has_suffix(base, ".png"))
        base[strlen(base) - 4] = '\0';

    GString *tip = g_string_new("Corsair VOID PRO \xe2\x80\x94 ");
    if (!st.dongle_present)
        g_string_append(tip, "dongle not detected");
    else if (st.percent < 0)
        g_string_append_printf(tip, "%s", bvp_state_label(st.state));
    else
        g_string_append_printf(tip, "%d%% (%s)", st.percent,
                               bvp_state_label(st.state));

    app_indicator_set_icon_theme_path(indicator, dir);
    app_indicator_set_icon_full(indicator, base, tip->str);
    app_indicator_set_title(indicator, tip->str);

    g_string_free(tip, TRUE);
    g_free(base);
    g_free(dir);

    /* keep only the most recent renders */
    if (icon_serial > 4) {
        char *old = g_strdup_printf("%s/icon_%d.png", icon_dir, icon_serial - 5);
        g_unlink(old);
        g_free(old);
    }
    g_free(path);
}
