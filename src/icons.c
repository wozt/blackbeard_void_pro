#include "icons.h"

#include <glib.h>

void bvp_draw_boat(cairo_t *cr, bool dark)
{
    if (dark)
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.95);
    else
        cairo_set_source_rgb(cr, 0.08, 0.10, 0.14);

    cairo_set_line_width(cr, 3.2);

    /* hull */
    cairo_move_to(cr, 8, 44);
    cairo_line_to(cr, 56, 44);
    cairo_line_to(cr, 47, 55);
    cairo_line_to(cr, 17, 55);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* mast */
    cairo_move_to(cr, 32, 10);
    cairo_line_to(cr, 32, 41);
    cairo_stroke(cr);

    /* mainsail */
    cairo_move_to(cr, 34, 13);
    cairo_line_to(cr, 51, 40);
    cairo_line_to(cr, 34, 40);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* jib */
    cairo_move_to(cr, 29, 16);
    cairo_line_to(cr, 29, 40);
    cairo_line_to(cr, 14, 40);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* Rounded square, so the launcher icon reads on both light and dark
   backgrounds -- the bare silhouette is only legible on one of them. */
static void rounded_rect(cairo_t *cr, double x, double y,
                         double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,          G_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI / 2,   G_PI);
    cairo_arc(cr, x + r,     y + r,     r, G_PI,       3 * G_PI / 2);
    cairo_close_path(cr);
}

bool bvp_write_boat_png(const char *path, int size)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    size, size);
    cairo_t *cr = cairo_create(s);
    cairo_scale(cr, size / 64.0, size / 64.0);

    cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, 64);
    cairo_pattern_add_color_stop_rgb(bg, 0.0, 0.10, 0.16, 0.28);
    cairo_pattern_add_color_stop_rgb(bg, 1.0, 0.05, 0.08, 0.15);
    cairo_set_source(cr, bg);
    rounded_rect(cr, 2, 2, 60, 60, 12);
    cairo_fill(cr);
    cairo_pattern_destroy(bg);

    /* waterline, to make it read as a boat rather than an arrow */
    cairo_set_source_rgba(cr, 0.30, 0.62, 0.85, 0.55);
    cairo_rectangle(cr, 6, 52, 52, 3);
    cairo_fill(cr);

    bvp_draw_boat(cr, true);
    cairo_destroy(cr);
    bool ok = cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}
