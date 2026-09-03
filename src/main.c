/* blackbeard_void_pro -- drive the Corsair VOID PRO Wireless headset on
   Linux, without iCUE.

   The "Dolby" effect is not a command sent to the headset: it is
   processing Windows applies host-side. We measured it by deconvolving
   the real USB stream and replay it here by convolution (see work.md).
   Battery and lighting, on the other hand, do go over HID. */
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <locale.h>
#include <math.h>

#include "audio.h"
#include "config.h"
#include "desktop.h"
#include "icons.h"
#include "device.h"
#include "tray.h"

typedef struct {
    bvp_config  cfg;
    GtkWidget  *window;
    GtkWidget  *status_label;
    GtkWidget  *dolby_switch;
    GtkWidget  *mode_combo;
    GtkWidget  *save_btn;
    bool        dirty;
    GtkWidget  *eq_area;
    GtkWidget  *eq_scales[BVP_BANDS];
    GtkWidget  *eq_spins[BVP_BANDS];
    GtkWidget  *preamp_scale;
    GtkWidget  *color_btn;
    GtkWidget  *bright_scale;
    GtkWidget  *mic_scale;
    GtkWidget  *battery_label;
    bool        suppress;      /* ignore signals caused by our own updates */
    bool        last_present;
    guint       eq_timer;
    guint       led_timer;
    GtkWidget  *autostart_check;
    GtkWidget  *headless_check;
} App;

static App app;

/* ---------- audio ---------- */

static void apply_audio(void)
{
    if (app.cfg.dolby) {
        char *err = NULL;
        if (!bvp_audio_apply(&app.cfg, &err)) {
            gtk_label_set_text(GTK_LABEL(app.status_label),
                               err ? err : "audio chain failed");
            g_free(err);
        }
    } else {
        bvp_audio_stop();
    }
}

static void mark_dirty(void)
{
    app.dirty = true;
    if (app.save_btn)
        gtk_widget_set_sensitive(app.save_btn, TRUE);
}

static gboolean eq_timeout(gpointer data)
{
    (void)data;
    app.eq_timer = 0;
    apply_audio();          /* heard immediately; saved only on request */
    return G_SOURCE_REMOVE;
}

static void on_save(GtkButton *b, gpointer data)
{
    (void)b; (void)data;
    bvp_config_save(&app.cfg);
    app.dirty = false;
    gtk_widget_set_sensitive(app.save_btn, FALSE);
    gtk_label_set_text(GTK_LABEL(app.status_label), "Settings saved.");
}

/* Coalesce slider movement: reloading the chain on every pixel would
   cause audible dropouts. */
static void schedule_audio(void)
{
    if (app.eq_timer)
        g_source_remove(app.eq_timer);
    app.eq_timer = g_timeout_add(500, eq_timeout, NULL);
}

/* ---------- equaliser curve ---------- */

static gboolean on_eq_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
    int width  = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);
    const double pad = 8, span = 14.0;

    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.12);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    for (int db = -12; db <= 12; db += 6) {
        double y = pad + (span - db) / (2 * span) * (height - 2 * pad);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, db == 0 ? 0.55 : 0.22);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pad, y);
        cairo_line_to(cr, width - pad, y);
        cairo_stroke(cr);
    }

    double xs[BVP_BANDS], ys[BVP_BANDS];
    for (int i = 0; i < BVP_BANDS; i++) {
        xs[i] = pad + i * (width - 2 * pad) / (double)(BVP_BANDS - 1);
        ys[i] = pad + (span - app.cfg.eq[app.cfg.dolby_mode][i]) / (2 * span) * (height - 2 * pad);
    }

    cairo_set_source_rgba(cr, 0.98, 0.75, 0.05, 0.95);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, xs[0], ys[0]);
    for (int i = 1; i < BVP_BANDS; i++) {
        double mx = xs[i - 1] + (xs[i] - xs[i - 1]) / 2;
        cairo_curve_to(cr, mx, ys[i - 1], mx, ys[i], xs[i], ys[i]);
    }
    cairo_stroke(cr);

    for (int i = 0; i < BVP_BANDS; i++) {
        cairo_arc(cr, xs[i], ys[i], 3.2, 0, 2 * M_PI);
        cairo_fill(cr);
    }
    return FALSE;
}

/* ---------- UI callbacks ---------- */

static void on_dolby(GtkSwitch *sw, GParamSpec *ps, gpointer data)
{
    if (app.suppress) return;
    app.cfg.dolby = gtk_switch_get_active(sw);
    apply_audio();
}

static void on_mode(GtkComboBox *c, gpointer data)
{
    (void)data;
    if (app.suppress) return;
    app.cfg.dolby_mode = gtk_combo_box_get_active(c);

    /* each method keeps its own curve: show the one that belongs to the
       method we just switched to */
    app.suppress = true;
    for (int i = 0; i < BVP_BANDS; i++)
        gtk_range_set_value(GTK_RANGE(app.eq_scales[i]),
                            app.cfg.eq[app.cfg.dolby_mode][i]);
    gtk_range_set_value(GTK_RANGE(app.preamp_scale),
                        app.cfg.preamp[app.cfg.dolby_mode]);
    app.suppress = false;
    gtk_widget_queue_draw(app.eq_area);

    mark_dirty();
    apply_audio();
}

/* A plain entry bound to an adjustment. A spin button would do the same
   but its steppers cost a lot of width, and eleven of them side by side
   made the window half again as wide as it needed to be. */
static void entry_from_adj(GtkAdjustment *adj, GtkEntry *e)
{
    char buf[16];
    g_snprintf(buf, sizeof(buf), "%.1f", gtk_adjustment_get_value(adj));
    gtk_entry_set_text(e, buf);
}

static void on_adj_to_entry(GtkAdjustment *adj, gpointer data)
{
    entry_from_adj(adj, GTK_ENTRY(data));
}

/* Commit typed text: anything unparsable or out of range snaps back to a
   valid value rather than being silently accepted. */
static void commit_entry(GtkEntry *e, GtkAdjustment *adj)
{
    const char *txt = gtk_entry_get_text(e);
    char *end = NULL;
    double v = g_strtod(txt, &end);
    if (end == txt || (end && *g_strstrip(end) != '\0')) {
        entry_from_adj(adj, e);         /* not a number: restore */
        return;
    }
    v = CLAMP(v, gtk_adjustment_get_lower(adj), gtk_adjustment_get_upper(adj));
    gtk_adjustment_set_value(adj, v);
    entry_from_adj(adj, e);             /* show the clamped value */
}

static void on_entry_activate(GtkEntry *e, gpointer data)
{
    commit_entry(e, GTK_ADJUSTMENT(data));
}

static gboolean on_entry_focus_out(GtkWidget *w, GdkEvent *ev, gpointer data)
{
    (void)ev;
    commit_entry(GTK_ENTRY(w), GTK_ADJUSTMENT(data));
    return FALSE;
}

static GtkWidget *value_entry(GtkAdjustment *adj, const char *tip)
{
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(e), 4);
    gtk_entry_set_max_length(GTK_ENTRY(e), 6);
    gtk_entry_set_alignment(GTK_ENTRY(e), 0.5);
    gtk_widget_set_tooltip_text(e, tip);
    entry_from_adj(adj, GTK_ENTRY(e));
    g_signal_connect(adj, "value-changed", G_CALLBACK(on_adj_to_entry), e);
    g_signal_connect(e, "activate", G_CALLBACK(on_entry_activate), adj);
    g_signal_connect(e, "focus-out-event", G_CALLBACK(on_entry_focus_out), adj);
    return e;
}

static void on_band(GtkAdjustment *adj, gpointer data)
{
    if (app.suppress) return;
    int i = GPOINTER_TO_INT(data);
    app.cfg.eq[app.cfg.dolby_mode][i] = gtk_adjustment_get_value(adj);
    gtk_widget_queue_draw(app.eq_area);
    mark_dirty();
    schedule_audio();
}

static void on_preamp(GtkAdjustment *adj, gpointer data)
{
    if (app.suppress) return;
    app.cfg.preamp[app.cfg.dolby_mode] = gtk_adjustment_get_value(adj);
    mark_dirty();
    schedule_audio();
}

static void set_curve(const double *v)
{
    app.suppress = true;
    for (int i = 0; i < BVP_BANDS; i++) {
        app.cfg.eq[app.cfg.dolby_mode][i] = v[i];
        gtk_range_set_value(GTK_RANGE(app.eq_scales[i]), v[i]);
    }
    app.suppress = false;
    gtk_widget_queue_draw(app.eq_area);
    mark_dirty();
    schedule_audio();
}

static void on_flat(GtkButton *b, gpointer data)
{
    static const double flat[BVP_BANDS] = { 0 };
    set_curve(flat);
}

static void push_led(void);

/* Discard everything changed since the last save and put the whole
   interface back to what is on disk. */
static void on_load(GtkButton *b, gpointer data)
{
    (void)b; (void)data;
    bvp_config_load(&app.cfg);

    app.suppress = true;
    gtk_switch_set_active(GTK_SWITCH(app.dolby_switch), app.cfg.dolby);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.mode_combo), app.cfg.dolby_mode);
    for (int i = 0; i < BVP_BANDS; i++)
        gtk_range_set_value(GTK_RANGE(app.eq_scales[i]),
                            app.cfg.eq[app.cfg.dolby_mode][i]);
    gtk_range_set_value(GTK_RANGE(app.preamp_scale),
                        app.cfg.preamp[app.cfg.dolby_mode]);
    GdkRGBA c = { app.cfg.r / 255.0, app.cfg.g / 255.0, app.cfg.b / 255.0, 1.0 };
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(app.color_btn), &c);
    gtk_range_set_value(GTK_RANGE(app.bright_scale), app.cfg.brightness);
    gtk_range_set_value(GTK_RANGE(app.mic_scale), app.cfg.mic_gain);
    app.suppress = false;

    gtk_widget_queue_draw(app.eq_area);
    bvp_audio_set_mic_gain(app.cfg.mic_gain);
    push_led();
    apply_audio();

    app.dirty = false;
    gtk_widget_set_sensitive(app.save_btn, FALSE);
    gtk_label_set_text(GTK_LABEL(app.status_label), "Saved settings restored.");
}

/* Replaying the lighting sequence takes about 0.8 s and blocks, so slider
   and colour changes are coalesced instead of firing on every step. */
static gboolean led_timeout(gpointer data)
{
    (void)data;
    app.led_timer = 0;
    bvp_device_set_lighting(app.cfg.r, app.cfg.g, app.cfg.b, app.cfg.brightness);
    mark_dirty();
    return G_SOURCE_REMOVE;
}

static void push_led(void)
{
    if (app.led_timer)
        g_source_remove(app.led_timer);
    app.led_timer = g_timeout_add(400, led_timeout, NULL);
}

static void on_color(GtkColorButton *btn, gpointer data)
{
    if (app.suppress) return;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    app.cfg.r = (uint8_t)(CLAMP(c.red,   0, 1) * 255);
    app.cfg.g = (uint8_t)(CLAMP(c.green, 0, 1) * 255);
    app.cfg.b = (uint8_t)(CLAMP(c.blue,  0, 1) * 255);
    push_led();
}

static void on_mic(GtkRange *r, gpointer data)
{
    (void)data;
    if (app.suppress) return;
    app.cfg.mic_gain = (uint8_t)gtk_range_get_value(r);
    bvp_audio_set_mic_gain(app.cfg.mic_gain);
    mark_dirty();
}

static void on_autostart(GtkToggleButton *b, gpointer data)
{
    (void)data;
    bvp_desktop_set_autostart(gtk_toggle_button_get_active(b), app.cfg.headless);
}

static void on_headless(GtkToggleButton *b, gpointer data)
{
    (void)data;
    app.cfg.headless = gtk_toggle_button_get_active(b);
    mark_dirty();
    /* keep the autostart entry in sync so the choice actually applies at
       the next login */
    if (bvp_desktop_autostart_enabled())
        bvp_desktop_set_autostart(true, app.cfg.headless);
}

static void on_bright(GtkRange *r, gpointer data)
{
    if (app.suppress) return;
    app.cfg.brightness = (uint8_t)gtk_range_get_value(r);
    push_led();
}

/* ---------- periodic polling, hot-plug ---------- */

static gboolean poll_device(gpointer data)
{
    bvp_status st = bvp_device_poll();

    /* One failed poll is not proof the dongle is gone: hidapi can miss a
       reply while the device is busy. Only trust an absence confirmed
       twice in a row, otherwise the chain gets torn down and rebuilt for
       nothing. */
    static int missing = 0;
    if (!st.dongle_present) {
        if (++missing < 2)
            return G_SOURCE_CONTINUE;
    } else {
        missing = 0;
    }

    GString *s = g_string_new(NULL);
    if (!st.dongle_present) {
        g_string_append(s, "Dongle not detected \xe2\x80\x94 plug in the USB receiver");
    } else if (st.percent < 0) {
        g_string_append_printf(s, "Dongle detected \xe2\x80\x94 %s",
                               bvp_state_label(st.state));
    } else {
        g_string_append_printf(s, "Dongle detected \xe2\x80\x94 headset at %d%% (%s)",
                               st.percent, bvp_state_label(st.state));
    }
    gtk_label_set_text(GTK_LABEL(app.status_label), s->str);
    g_string_free(s, TRUE);

    gtk_widget_set_sensitive(app.color_btn,   st.dongle_present);
    gtk_widget_set_sensitive(app.bright_scale, st.dongle_present);

    /* on re-plug the sink is recreated, so re-apply lighting and
       rebuild the audio chain */
    if (st.dongle_present && !app.last_present) {
        push_led();
        if (app.cfg.dolby)
            apply_audio();
    }
    app.last_present = st.dongle_present;

    bvp_tray_update(st);
    return G_SOURCE_CONTINUE;
}

/* ---------- window construction ---------- */

static GtkWidget *labelled(const char *markup, GtkAlign align)
{
    GtkWidget *l = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    gtk_widget_set_halign(l, align);
    /* Without wrapping, the explanatory sentences alone decide how wide the
       window is, and it ends up far wider than the controls need. */
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 58);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    return l;
}

/* The startup labels are the widest text in the right-hand column, and
   that column also holds the lighting and microphone sliders: a notch of
   type smaller there keeps them from setting the window width. */
static GtkWidget *small_check(const char *text)
{
    GtkWidget *b = gtk_check_button_new_with_label(text);
    char *m = g_markup_printf_escaped("<span size='x-small'>%s</span>", text);
    gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))), m);
    g_free(m);
    return b;
}

static void build_ui(void)
{
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Blackbeard VOID PRO");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 560, 420);
    char *icon = bvp_desktop_icon_path();
    if (g_file_test(icon, G_FILE_TEST_EXISTS)) {
        GError *ie = NULL;
        GdkPixbuf *pb = gdk_pixbuf_new_from_file(icon, &ie);
        if (pb) {
            gtk_window_set_default_icon(pb);
            gtk_window_set_icon(GTK_WINDOW(app.window), pb);
            g_object_unref(pb);
        } else {
            g_warning("window icon: %s", ie ? ie->message : "unknown error");
            g_clear_error(&ie);
        }
    }
    g_free(icon);
    g_signal_connect(app.window, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { font-size: 90%; }"
        "entry { min-height: 0; min-width: 0; padding: 1px 2px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);
    gtk_container_add(GTK_CONTAINER(app.window), outer);

    app.status_label = labelled("<b>Looking for the dongle\xe2\x80\xa6</b>",
                                GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(outer), app.status_label, FALSE, FALSE, 0);

    /* Four quadrants, separated by rules:
         1  method            |  2  startup
         3  Dolby + equaliser |  4  lighting and microphone            */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_box_pack_start(GTK_BOX(outer), grid, TRUE, TRUE, 0);

    GtkWidget *z1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *z2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *z3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *z4 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_grid_attach(GTK_GRID(grid), z1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
                    1, 0, 1, 3);
    gtk_grid_attach(GTK_GRID(grid), z2, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                    0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                    2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), z3, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), z4, 2, 2, 1, 1);
    gtk_widget_set_hexpand(z3, TRUE);

    /* ---- 1 : which method ---- */
    gtk_box_pack_start(GTK_BOX(z1),
                       labelled("<b>Method</b> (Dolby Surround Emulation)",
                                GTK_ALIGN_START), FALSE, FALSE, 0);
    app.mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.mode_combo),
        "Convolution 1.0");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.mode_combo),
        "Convolution 2.0");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app.mode_combo), app.cfg.dolby_mode);
    gtk_widget_set_tooltip_text(app.mode_combo,
        "Both replay impulse responses measured on the hardware.\n"
        "1.0 comes from a single measurement.\n"
        "2.0 averages several sweeps, drops the deconvolution regularisation "
        "now that the noise floor is lower, keeps only the swept "
        "20 Hz - 20 kHz band, and stops truncating the reverb tail.");
    g_signal_connect(app.mode_combo, "changed", G_CALLBACK(on_mode), NULL);
    gtk_box_pack_start(GTK_BOX(z1), app.mode_combo, FALSE, FALSE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    app.save_btn = gtk_button_new_with_label("Save");
    gtk_widget_set_sensitive(app.save_btn, FALSE);
    gtk_widget_set_tooltip_text(app.save_btn, "Write the current settings to disk.");
    g_signal_connect(app.save_btn, "clicked", G_CALLBACK(on_save), NULL);
    gtk_box_pack_start(GTK_BOX(btns), app.save_btn, FALSE, FALSE, 0);

    GtkWidget *load_btn = gtk_button_new_with_label("Load");
    gtk_widget_set_tooltip_text(load_btn,
        "Put every control back to the last saved settings, discarding "
        "changes made since.");
    g_signal_connect(load_btn, "clicked", G_CALLBACK(on_load), NULL);
    gtk_box_pack_start(GTK_BOX(btns), load_btn, FALSE, FALSE, 0);

    GtkWidget *flat_btn = gtk_button_new_with_label("Flat EQ");
    gtk_widget_set_tooltip_text(flat_btn, "Set every equaliser band to 0 dB.");
    g_signal_connect(flat_btn, "clicked", G_CALLBACK(on_flat), NULL);
    gtk_box_pack_start(GTK_BOX(btns), flat_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(z1), btns, FALSE, FALSE, 2);

    /* ---- 2 : startup ---- */
    gtk_box_pack_start(GTK_BOX(z2),
                       labelled("<b>Startup</b>", GTK_ALIGN_START), FALSE, FALSE, 0);
    app.autostart_check = small_check("Start at login");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.autostart_check),
                                 bvp_desktop_autostart_enabled());
    g_signal_connect(app.autostart_check, "toggled",
                     G_CALLBACK(on_autostart), NULL);
    gtk_box_pack_start(GTK_BOX(z2), app.autostart_check, FALSE, FALSE, 0);

    app.headless_check = small_check("Headless");
    gtk_widget_set_tooltip_text(app.headless_check,
        "Start hidden. The tray icon is still there; use its Open entry to "
        "bring the window back.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.headless_check),
                                 app.cfg.headless);
    g_signal_connect(app.headless_check, "toggled",
                     G_CALLBACK(on_headless), NULL);
    gtk_box_pack_start(GTK_BOX(z2), app.headless_check, FALSE, FALSE, 0);


    /* ---- 3 : Dolby switch, curve, equaliser, preamp ---- */
    GtkWidget *drow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(drow),
                       labelled("<b>Dolby</b>", GTK_ALIGN_START), TRUE, TRUE, 0);
    app.dolby_switch = gtk_switch_new();
    gtk_widget_set_tooltip_text(app.dolby_switch,
        "Virtual surround: reproduces, by convolution, the processing "
        "Windows applies host-side.");
    gtk_widget_set_valign(app.dolby_switch, GTK_ALIGN_CENTER);
    g_signal_connect(app.dolby_switch, "notify::active",
                     G_CALLBACK(on_dolby), NULL);
    gtk_box_pack_start(GTK_BOX(drow), app.dolby_switch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(z3), drow, FALSE, FALSE, 0);

    app.eq_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.eq_area, -1, 80);
    g_signal_connect(app.eq_area, "draw", G_CALLBACK(on_eq_draw), NULL);
    gtk_box_pack_start(GTK_BOX(z3), app.eq_area, FALSE, FALSE, 0);

    GtkWidget *bands = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(bands), TRUE);
    for (int i = 0; i < BVP_BANDS; i++) {
        GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkAdjustment *adj = gtk_adjustment_new(
            app.cfg.eq[app.cfg.dolby_mode][i], -12.0, 12.0, 0.5, 1.0, 0.0);
        char tip[160];
        g_snprintf(tip, sizeof(tip),
                   "%d Hz \xe2\x80\x94 gain in dB, between -12 and +12.\n"
                   "Type a value or use the slider; anything outside the "
                   "range is clamped.", bvp_band_freq[i]);
        GtkWidget *ent = value_entry(adj, tip);
        app.eq_spins[i] = ent;
        gtk_box_pack_start(GTK_BOX(col), ent, FALSE, FALSE, 0);

        GtkWidget *sc = gtk_scale_new(GTK_ORIENTATION_VERTICAL, adj);
        gtk_range_set_inverted(GTK_RANGE(sc), TRUE);
        gtk_scale_set_draw_value(GTK_SCALE(sc), FALSE);
        gtk_widget_set_size_request(sc, -1, 95);
        gtk_widget_set_tooltip_text(sc, tip);
        g_signal_connect(adj, "value-changed", G_CALLBACK(on_band),
                         GINT_TO_POINTER(i));
        app.eq_scales[i] = sc;
        gtk_box_pack_start(GTK_BOX(col), sc, TRUE, TRUE, 0);

        char lbl[16];
        int f = bvp_band_freq[i];
        if (f >= 1000) g_snprintf(lbl, sizeof(lbl), "%dk", f / 1000);
        else           g_snprintf(lbl, sizeof(lbl), "%d", f);
        gtk_box_pack_start(GTK_BOX(col), gtk_label_new(lbl), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bands), col, TRUE, TRUE, 0);
    }

    gtk_box_pack_start(GTK_BOX(bands),
                       gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);

    GtkWidget *pcol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkAdjustment *padj = gtk_adjustment_new(
        app.cfg.preamp[app.cfg.dolby_mode], -20.0, 12.0, 0.5, 1.0, 0.0);
    const char *ptip =
        "Overall gain in dB, between -20 and +12. Type a value or drag the "
        "slider; anything outside the range is clamped.";
    gtk_box_pack_start(GTK_BOX(pcol), value_entry(padj, ptip), FALSE, FALSE, 0);
    app.preamp_scale = gtk_scale_new(GTK_ORIENTATION_VERTICAL, padj);
    gtk_range_set_inverted(GTK_RANGE(app.preamp_scale), TRUE);
    gtk_scale_set_draw_value(GTK_SCALE(app.preamp_scale), FALSE);
    gtk_widget_set_size_request(app.preamp_scale, -1, 95);
    gtk_widget_set_tooltip_text(app.preamp_scale, ptip);
    gtk_box_pack_start(GTK_BOX(pcol), app.preamp_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pcol), gtk_label_new("Pre"), FALSE, FALSE, 0);
    g_signal_connect(padj, "value-changed", G_CALLBACK(on_preamp), NULL);
    gtk_box_pack_start(GTK_BOX(bands), pcol, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(z3), bands, FALSE, FALSE, 0);

    /* ---- 4 : lighting and microphone, same vertical idiom ----
       A grid rather than two boxes: the caption, the icon and the slider
       line up row by row, so both columns read at the same heights as the
       equaliser next to them. */
    GtkWidget *side = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(side), 6);
    gtk_grid_set_row_spacing(GTK_GRID(side), 2);
    /* hug the left edge: the column is as wide as the startup checkboxes
       above it, and centring would leave the two bars floating in it */
    gtk_widget_set_halign(side, GTK_ALIGN_START);

    GtkWidget *led_lbl = labelled("<b>LED</b>", GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(side), led_lbl, 0, 0, 1, 1);

    GdkRGBA c = { app.cfg.r / 255.0, app.cfg.g / 255.0, app.cfg.b / 255.0, 1.0 };
    app.color_btn = gtk_color_button_new_with_rgba(&c);
    gtk_widget_set_tooltip_text(app.color_btn, "Colour of the headset lighting.");
    gtk_widget_set_halign(app.color_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(app.color_btn, 30, 20);
    g_signal_connect(app.color_btn, "color-set", G_CALLBACK(on_color), NULL);
    gtk_grid_attach(GTK_GRID(side), app.color_btn, 0, 1, 1, 1);

    app.bright_scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL,
                                                0, 255, 1);
    gtk_range_set_inverted(GTK_RANGE(app.bright_scale), TRUE);
    gtk_scale_set_draw_value(GTK_SCALE(app.bright_scale), FALSE);
    gtk_widget_set_vexpand(app.bright_scale, TRUE);
    gtk_range_set_value(GTK_RANGE(app.bright_scale), app.cfg.brightness);
    gtk_widget_set_tooltip_text(app.bright_scale,
                                "Lighting brightness, 0 to 255.");
    g_signal_connect(app.bright_scale, "value-changed",
                     G_CALLBACK(on_bright), NULL);
    gtk_grid_attach(GTK_GRID(side), app.bright_scale, 0, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(side),
                    gtk_separator_new(GTK_ORIENTATION_VERTICAL), 1, 0, 1, 3);

    GtkWidget *mic_icon = gtk_image_new_from_icon_name(
        "audio-input-microphone-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text(mic_icon, "Headset microphone.");
    gtk_widget_set_valign(mic_icon, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(side), mic_icon, 2, 0, 1, 1);

    app.mic_scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 100, 1);
    gtk_range_set_inverted(GTK_RANGE(app.mic_scale), TRUE);
    gtk_scale_set_draw_value(GTK_SCALE(app.mic_scale), FALSE);
    gtk_widget_set_vexpand(app.mic_scale, TRUE);
    gtk_range_set_value(GTK_RANGE(app.mic_scale), app.cfg.mic_gain);
    gtk_widget_set_tooltip_text(app.mic_scale,
        "Hardware capture gain of the headset, 0 to 100%.\n"
        "The microphone is mono in hardware: its USB descriptor declares a "
        "single channel, so Windows receives the same stream.");
    g_signal_connect(app.mic_scale, "value-changed", G_CALLBACK(on_mic), NULL);
    gtk_grid_attach(GTK_GRID(side), app.mic_scale, 2, 1, 1, 2);

    gtk_box_pack_start(GTK_BOX(z4), side, TRUE, TRUE, 0);
}

/* Leaving the filter chain behind would show up as a ghost sink, so quit
   through the same path whether the user closes the window, picks Quit in
   the tray, or sends us a signal. */
static gboolean on_term_signal(gpointer data)
{
    GApplication *gapp = data;
    bvp_audio_stop();
    g_application_quit(gapp);
    return G_SOURCE_REMOVE;
}

/* Single instance: GtkApplication holds the name on the session bus, so a
   second launch does not start a duplicate -- it activates the one already
   running, which simply presents its window. */
static bool arg_headless = false;
static bool ui_built = false;

static void on_activate(GtkApplication *gapp, gpointer data)
{
    (void)data;
    if (ui_built) {                 /* second launch: bring the window up */
        gtk_window_present(GTK_WINDOW(app.window));
        return;
    }
    ui_built = true;

    build_ui();
    gtk_application_add_window(gapp, GTK_WINDOW(app.window));
    bvp_tray_init(app.window);

    app.suppress = true;
    gtk_switch_set_active(GTK_SWITCH(app.dolby_switch), app.cfg.dolby);
    int mic = bvp_audio_get_mic_gain();     /* trust the device over the file */
    if (mic >= 0) {
        app.cfg.mic_gain = (uint8_t)mic;
        gtk_range_set_value(GTK_RANGE(app.mic_scale), mic);
    }
    app.suppress = false;

    /* show_all first even in headless mode: otherwise the children stay
       hidden and "Open" from the tray would present an empty window */
    gtk_widget_show_all(app.window);
    if (arg_headless)
        gtk_widget_hide(app.window);

    if (bvp_device_open()) {
        push_led();
        /* Mark the dongle as already seen: otherwise the first poll reads
           it as a fresh plug-in and re-runs the whole setup, tearing down
           the audio chain we are about to build and reloading it while
           PipeWire still holds the node name ("Could not load module"). */
        app.last_present = true;
    }
    if (app.cfg.dolby)
        apply_audio();

    poll_device(NULL);
    /* battery level and charge state refresh */
    g_timeout_add_seconds(10, poll_device, NULL);
}

int main(int argc, char **argv)
{
    /* WM_CLASS must match the .desktop file name, otherwise desktops that
       resolve the icon through the desktop entry fall back to a generic
       one. Has to be set before gtk_init(). */
    g_set_prgname(BVP_APP_ID);

    gtk_init(&argc, &argv);

    /* gtk_init() applies the user locale, which in fr_FR makes printf
       write "0,00" for numbers. PipeWire's SPA parser only accepts a dot,
       so the filter graph would be rejected with "Could not load module".
       The UI is English-only, so forcing the numeric locale costs nothing. */
    setlocale(LC_NUMERIC, "C");

    bvp_config_load(&app.cfg);
    bvp_desktop_install();
    bvp_audio_cleanup_stale();

    arg_headless = app.cfg.headless;
    for (int i = 1; i < argc; i++)
        if (!g_strcmp0(argv[i], "--headless"))
            arg_headless = true;

    /* Locate the measured impulse responses. Checked in order: next to the
       binary (running from the build tree), one level up (binary in a
       bin/ subdirectory), then the installed data directory. */
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *bin = exe ? g_path_get_dirname(exe) : g_strdup(".");
    char *candidates[] = {
        g_build_filename(bin, "filters", NULL),
        g_build_filename(bin, "..", "filters", NULL),
        g_build_filename(bin, "..", "share", "blackbeard_void_pro", "filters", NULL),
        g_build_filename(g_get_user_data_dir(), "blackbeard_void_pro", "filters", NULL),
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        /* Accept either layout: the generations in v1/ and v2/, or the
           older flat one. Probing only for a flat ir_LL.wav silently left
           the filter directory unset once the files moved into v1/. */
        char *probe = g_build_filename(candidates[i], "v1", "ir_LL.wav", NULL);
        bool found = g_file_test(probe, G_FILE_TEST_EXISTS);
        g_free(probe);
        if (!found) {
            probe = g_build_filename(candidates[i], "ir_LL.wav", NULL);
            found = g_file_test(probe, G_FILE_TEST_EXISTS);
            g_free(probe);
        }
        if (found)
            bvp_audio_set_filter_dir(candidates[i]);
    }
    for (int i = 0; candidates[i]; i++)
        g_free(candidates[i]);
    g_free(bin);
    g_free(exe);

    GtkApplication *gapp =
        gtk_application_new("dev.wozt.blackbeard-void-pro",
                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gapp, "activate", G_CALLBACK(on_activate), NULL);
    g_unix_signal_add(SIGINT,  on_term_signal, gapp);
    g_unix_signal_add(SIGTERM, on_term_signal, gapp);
    g_unix_signal_add(SIGHUP,  on_term_signal, gapp);
    /* argv is consumed above; hand g_application_run an empty one so it
       does not choke on --headless */
    int status = g_application_run(G_APPLICATION(gapp), 0, NULL);
    g_object_unref(gapp);

    bvp_audio_stop();
    bvp_device_close();
    /* Only the instance that actually owns the UI may write the settings:
       a second launch exits through here too, and would otherwise save the
       state it read at startup over whatever the running instance has
       changed since. */
    if (ui_built)
        bvp_config_save(&app.cfg);
    return status;
}
