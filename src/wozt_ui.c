/* WOZT GTK Standard integration for Blackbeard VOID PRO.
 * Window chrome/theme/language work stays on the GTK thread and never
 * touches the headset, audio processing or application configurations. */
#include "wozt_ui.h"

#include <json-glib/json-glib.h>
#include <string.h>

#define WOZT_ID "blackbeard_void_pro"
#define WOZT_COLORS 11

static const char *color_keys[WOZT_COLORS] = {
    "background", "panel", "entry", "border", "text", "muted",
    "accent", "button", "hover", "active", "negative"
};

typedef struct {
    gchar *id;
    gchar *name;
    gchar *path;
} Resource;

struct WoztUI {
    GtkWidget *window;
    GtkWidget *root;
    GtkWidget *overlay;
    GtkWidget *resize_zones[8];
    guint resize_count;
    GtkWidget *body;
    GtkWidget *theme_combo;
    GtkWidget *language_combo;
    GtkWidget *maximize_btn;
    GtkCssProvider *css;
    GPtrArray *themes;
    GPtrArray *languages;
    GHashTable *translations;
    gchar *theme_id;
    gchar *language_id;
    void (*language_cb)(gpointer);
    gpointer language_data;
    gboolean updating;
};

static void resource_free(gpointer p)
{
    Resource *r = p;
    if (!r) return;
    g_free(r->id); g_free(r->name); g_free(r->path); g_free(r);
}

static gchar *settings_path(void)
{
    return g_build_filename(g_get_user_config_dir(), WOZT_ID, "ui.ini", NULL);
}

static void save_settings(WoztUI *ui)
{
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "appearance", "theme", ui->theme_id);
    g_key_file_set_string(key, "appearance", "language", ui->language_id);
    gsize size = 0;
    gchar *data = g_key_file_to_data(key, &size, NULL);
    gchar *dir = g_build_filename(g_get_user_config_dir(), WOZT_ID, NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar *path = settings_path();
    if (!g_file_set_contents(path, data, (gssize)size, NULL))
        g_warning("WOZT UI: could not save preferences at %s", path);
    g_free(path); g_free(dir); g_free(data); g_key_file_unref(key);
}

static void load_settings(WoztUI *ui)
{
    GKeyFile *key = g_key_file_new();
    gchar *path = settings_path();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        gchar *theme = g_key_file_get_string(key, "appearance", "theme", NULL);
        gchar *language = g_key_file_get_string(key, "appearance", "language", NULL);
        if (theme && *theme) { g_free(ui->theme_id); ui->theme_id = g_strdup(theme); }
        if (language && *language) { g_free(ui->language_id); ui->language_id = g_strdup(language); }
        g_free(theme); g_free(language);
    }
    g_free(path);
    g_key_file_unref(key);
}

static gboolean valid_id(const char *s)
{
    if (!s || !*s || strlen(s) > 64) return FALSE;
    for (const char *p = s; *p; p++)
        if (!g_ascii_islower(*p) && !g_ascii_isdigit(*p) && *p != '-' && *p != '_')
            return FALSE;
    return TRUE;
}

static gchar *bundled_dir(const gchar *kind)
{
    gchar *exe = g_file_read_link("/proc/self/exe", NULL);
    gchar *bin = exe ? g_path_get_dirname(exe) : g_get_current_dir();
    gchar *paths[] = {
        g_build_filename(bin, "data", kind, NULL),
        g_build_filename(bin, "..", "data", kind, NULL),
        g_build_filename(bin, "..", "share", WOZT_ID, kind, NULL),
        g_build_filename(bin, "..", "usr", "share", WOZT_ID, kind, NULL),
        g_build_filename(g_get_user_data_dir(), WOZT_ID, kind, NULL),
        g_build_filename("/usr/local/share", WOZT_ID, kind, NULL),
        g_build_filename("/usr/share", WOZT_ID, kind, NULL)
    };
    gchar *found = NULL;
    const char *appdir = g_getenv("APPDIR");
    if (appdir) {
        gchar *candidate = g_build_filename(appdir, "usr", "share", WOZT_ID, kind, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_DIR)) found = candidate;
        else g_free(candidate);
    }
    for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
        if (!found && g_file_test(paths[i], G_FILE_TEST_IS_DIR))
            found = g_strdup(paths[i]);
        g_free(paths[i]);
    }
    g_free(exe); g_free(bin);
    return found;
}

static JsonObject *parse_object(const char *path, JsonParser **owner)
{
    *owner = json_parser_new();
    GError *error = NULL;
    if (!json_parser_load_from_file(*owner, path, &error)) {
        g_warning("WOZT UI JSON %s: %s", path, error ? error->message : "parse error");
        g_clear_error(&error);
        g_object_unref(*owner); *owner = NULL; return NULL;
    }
    JsonNode *root = json_parser_get_root(*owner);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("WOZT UI JSON %s: object expected", path);
        g_object_unref(*owner); *owner = NULL; return NULL;
    }
    return json_node_get_object(root);
}

static const char *json_string(JsonObject *o, const char *name)
{
    if (!o || !json_object_has_member(o, name)) return NULL;
    JsonNode *n = json_object_get_member(o, name);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING
        ? json_node_get_string(n) : NULL;
}

static gint sort_resource(gconstpointer a, gconstpointer b)
{
    const Resource *x = *(Resource * const *)a;
    const Resource *y = *(Resource * const *)b;
    return g_strcmp0(x->path, y->path);
}

static gboolean palette_valid(JsonObject *obj)
{
    if (!json_object_has_member(obj, "colors")) return FALSE;
    JsonNode *node = json_object_get_member(obj, "colors");
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) return FALSE;
    JsonObject *colors = json_node_get_object(node);
    for (int i = 0; i < WOZT_COLORS; i++) {
        const char *value = json_string(colors, color_keys[i]);
        if (!value || !g_regex_match_simple("^#[0-9A-Fa-f]{6}$", value, 0, 0))
            return FALSE;
    }
    return TRUE;
}

static void collect_resources(GPtrArray *list, const gchar *dir, const gchar *kind)
{
    if (!dir) return;
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d) return;
    const gchar *name;
    while ((name = g_dir_read_name(d))) {
        if (!g_str_has_suffix(name, ".json")) continue;
        gchar *path = g_build_filename(dir, name, NULL);
        JsonParser *parser = NULL;
        JsonObject *obj = parse_object(path, &parser);
        if (obj) {
            const char *id = json_string(obj, "id");
            const char *display = json_string(obj, "name");
            if (valid_id(id) && display && *display &&
                (g_strcmp0(kind, "themes") != 0 || palette_valid(obj))) {
                /* Last valid source wins: ~/.config may override bundled JSON. */
                for (guint i = 0; i < list->len; i++) {
                    Resource *old = g_ptr_array_index(list, i);
                    if (g_strcmp0(old->id, id) == 0) {
                        g_ptr_array_remove_index(list, i);
                        break;
                    }
                }
                Resource *r = g_new0(Resource, 1);
                r->id = g_strdup(id); r->name = g_strdup(display); r->path = g_strdup(path);
                g_ptr_array_add(list, r);
            }
            g_object_unref(parser);
        }
        g_free(path);
    }
    g_dir_close(d);
}

static GPtrArray *find_resources(const gchar *kind)
{
    GPtrArray *list = g_ptr_array_new_with_free_func(resource_free);
    gchar *bundled = bundled_dir(kind);
    collect_resources(list, bundled, kind);
    g_free(bundled);
    gchar *override = g_build_filename(g_get_user_config_dir(), WOZT_ID, kind, NULL);
    collect_resources(list, override, kind);
    g_free(override);
    g_ptr_array_sort(list, sort_resource);
    return list;
}

static Resource *find_resource(GPtrArray *list, const gchar *id)
{
    if (!list) return NULL;
    for (guint i = 0; i < list->len; i++) {
        Resource *r = g_ptr_array_index(list, i);
        if (g_strcmp0(r->id, id) == 0) return r;
    }
    return NULL;
}

static gboolean apply_theme(WoztUI *ui)
{
    Resource *r = find_resource(ui->themes, ui->theme_id);
    if (!r) r = find_resource(ui->themes, "ocean");
    if (!r && ui->themes->len) r = g_ptr_array_index(ui->themes, 0);
    if (!r) { g_warning("WOZT UI: no usable theme JSON found"); return FALSE; }
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(r->path, &parser);
    if (!obj) return FALSE;
    JsonObject *palette = NULL;
    if (json_object_has_member(obj, "colors")) {
        JsonNode *n = json_object_get_member(obj, "colors");
        if (n && JSON_NODE_HOLDS_OBJECT(n)) palette = json_node_get_object(n);
    }
    const gchar *colors[WOZT_COLORS];
    gboolean valid = palette != NULL;
    for (int i = 0; i < WOZT_COLORS && valid; i++) {
        colors[i] = json_string(palette, color_keys[i]);
        if (!colors[i] || !g_regex_match_simple("^#[0-9A-Fa-f]{6}$", colors[i], 0, 0))
            valid = FALSE;
    }
    if (!valid) {
        g_warning("WOZT UI: invalid colors in %s", r->path);
        g_object_unref(parser); return FALSE;
    }
    const gchar *format =
        "window.wozt-window, .wozt-root { background-color:%s; color:%s; }"
        ".wozt-topbar { background-color:%s; border-bottom:1px solid %s; padding:5px 8px; }"
        ".wozt-topbar label { color:%s; }"
        ".wozt-panel { background-color:%s; border:1px solid %s; border-radius:7px; padding:10px; }"
        ".wozt-panel label { color:%s; }"
        ".wozt-root entry, .wozt-root spinbutton { background-color:%s; color:%s; border:1px solid %s; border-radius:4px; min-height:0; padding:3px 4px; }"
        ".wozt-root button, .wozt-root combobox button { background-image:none; background-color:%s; color:%s; border:1px solid %s; border-radius:5px; padding:3px 8px; }"
        ".wozt-root button:hover, .wozt-root combobox button:hover { background-color:%s; }"
        ".wozt-root button:active, .wozt-root button:checked, .wozt-root combobox button:checked { background-color:%s; }"
        ".wozt-root button:disabled { color:%s; opacity:0.65; }"
        ".wozt-root button:focus, .wozt-root entry:focus { border-color:%s; outline-color:%s; }"
        ".wozt-root scale highlight, .wozt-root scale slider { background-color:%s; }"
        ".wozt-root checkbutton check:checked { background-color:%s; color:%s; }"
        ".wozt-root .wozt-primary { border-color:%s; }"
        ".wozt-root .wozt-danger:hover { background-color:%s; }"
        "menu, menuitem, popover, tooltip, tooltip.background { background-color:%s; color:%s; }"
        "menuitem:hover, popover button:hover { background-color:%s; }"
        "menu label, menuitem label, popover label, tooltip label { color:%s; }";
    gchar *css = g_strdup_printf(format,
        colors[0], colors[4], colors[1], colors[3], colors[4],
        colors[1], colors[3], colors[4], colors[2], colors[4], colors[3],
        colors[7], colors[4], colors[3], colors[8], colors[9],
        colors[5], colors[6], colors[6], colors[6],
        colors[6], colors[4], colors[6], colors[10],
        colors[1], colors[4], colors[8], colors[4]);
    GError *error = NULL;
    gtk_css_provider_load_from_data(ui->css, css, -1, &error);
    if (error) { g_warning("WOZT CSS: %s", error->message); g_clear_error(&error); }
    g_free(css);
    g_object_unref(parser);
    if (g_strcmp0(ui->theme_id, r->id) != 0) {
        g_free(ui->theme_id); ui->theme_id = g_strdup(r->id);
    }
    return TRUE;
}

static void load_language(WoztUI *ui)
{
    g_hash_table_remove_all(ui->translations);
    if (g_strcmp0(ui->language_id, "en") == 0) return;
    Resource *r = find_resource(ui->languages, ui->language_id);
    if (!r) return;
    JsonParser *parser = NULL;
    JsonObject *obj = parse_object(r->path, &parser);
    if (!obj) return;
    JsonObject *dict = NULL;
    if (json_object_has_member(obj, "translations")) {
        JsonNode *node = json_object_get_member(obj, "translations");
        if (node && JSON_NODE_HOLDS_OBJECT(node)) dict = json_node_get_object(node);
    }
    if (dict) {
        GList *keys = json_object_get_members(dict);
        for (GList *it = keys; it; it = it->next) {
            const char *key = it->data;
            const char *value = json_string(dict, key);
            if (value) g_hash_table_insert(ui->translations, g_strdup(key), g_strdup(value));
        }
        g_list_free(keys);
    }
    g_object_unref(parser);
}

const char *wozt_ui_tr(WoztUI *ui, const char *english)
{
    if (!ui || !english) return english;
    const char *translated = g_hash_table_lookup(ui->translations, english);
    return translated ? translated : english;
}

static void translate_widget(GtkWidget *widget, gpointer user_data)
{
    WoztUI *ui = user_data;
    if (GTK_IS_LABEL(widget) && !g_object_get_data(G_OBJECT(widget), "wozt-dynamic")) {
        const gchar *english = g_object_get_data(G_OBJECT(widget), "wozt-english-label");
        if (!english) {
            const gchar *raw = gtk_label_get_label(GTK_LABEL(widget));
            english = g_strdup(raw ? raw : "");
            g_object_set_data_full(G_OBJECT(widget), "wozt-english-label", (gpointer)english, g_free);
            g_object_set_data(G_OBJECT(widget), "wozt-markup",
                              GINT_TO_POINTER(gtk_label_get_use_markup(GTK_LABEL(widget)) ? 1 : 0));
        }
        const char *new_label = wozt_ui_tr(ui, english);
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "wozt-markup")))
            gtk_label_set_markup(GTK_LABEL(widget), new_label);
        else
            gtk_label_set_text(GTK_LABEL(widget), new_label);
    }
    if (!g_object_get_data(G_OBJECT(widget), "wozt-dynamic")) {
    const gchar *original_tip = g_object_get_data(G_OBJECT(widget), "wozt-english-tooltip");
    if (!original_tip) {
        gchar *tip = gtk_widget_get_tooltip_text(widget);
        if (tip) {
            original_tip = tip;
            g_object_set_data_full(G_OBJECT(widget), "wozt-english-tooltip", tip, g_free);
        }
    }
    if (original_tip)
        gtk_widget_set_tooltip_text(widget, wozt_ui_tr(ui, original_tip));
    }
    if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), translate_widget, ui);
}

void wozt_ui_retranslate(WoztUI *ui)
{
    if (!ui) return;
    translate_widget(ui->root, ui);
    if (ui->language_cb) ui->language_cb(ui->language_data);
}

void wozt_ui_on_language_change(WoztUI *ui, void (*callback)(gpointer), gpointer user_data)
{
    ui->language_cb = callback;
    ui->language_data = user_data;
}

static void reload_resources(WoztUI *ui)
{
    ui->updating = TRUE;
    if (ui->themes) g_ptr_array_unref(ui->themes);
    if (ui->languages) g_ptr_array_unref(ui->languages);
    ui->themes = find_resources("themes");
    ui->languages = find_resources("languages");
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->theme_combo));
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->language_combo));
    for (guint i = 0; i < ui->themes->len; i++) {
        Resource *r = g_ptr_array_index(ui->themes, i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->theme_combo), r->id, r->name);
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->language_combo), "en", "English");
    for (guint i = 0; i < ui->languages->len; i++) {
        Resource *r = g_ptr_array_index(ui->languages, i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->language_combo), r->id, r->name);
    }
    if (!find_resource(ui->languages, ui->language_id) && g_strcmp0(ui->language_id, "en") != 0) {
        g_free(ui->language_id); ui->language_id = g_strdup("en");
    }
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->language_combo), ui->language_id);
    load_language(ui);
    apply_theme(ui);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->theme_combo), ui->theme_id);
    ui->updating = FALSE;
    wozt_ui_retranslate(ui);
}

static void on_theme_changed(GtkComboBox *box, gpointer data)
{
    WoztUI *ui = data;
    if (ui->updating) return;
    const char *id = gtk_combo_box_get_active_id(box);
    if (!id || g_strcmp0(ui->theme_id, id) == 0) return;
    g_free(ui->theme_id); ui->theme_id = g_strdup(id);
    if (apply_theme(ui)) save_settings(ui);
}

static void on_language_changed(GtkComboBox *box, gpointer data)
{
    WoztUI *ui = data;
    if (ui->updating) return;
    const char *id = gtk_combo_box_get_active_id(box);
    if (!id || g_strcmp0(ui->language_id, id) == 0) return;
    g_free(ui->language_id); ui->language_id = g_strdup(id);
    load_language(ui);
    wozt_ui_retranslate(ui);
    save_settings(ui);
}

static void on_reload(GtkButton *button, gpointer data)
{
    (void)button;
    WoztUI *ui = data;
    reload_resources(ui);
    save_settings(ui);
}

static gboolean on_move_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    WoztUI *ui = data;
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    if (event->type == GDK_2BUTTON_PRESS) {
        if (gtk_window_is_maximized(GTK_WINDOW(ui->window)))
            gtk_window_unmaximize(GTK_WINDOW(ui->window));
        else gtk_window_maximize(GTK_WINDOW(ui->window));
        return TRUE;
    }
    if (event->type != GDK_BUTTON_PRESS) return FALSE;
    gtk_window_begin_move_drag(GTK_WINDOW(ui->window), event->button,
                               (gint)event->x_root, (gint)event->y_root, event->time);
    return TRUE;
}

static void on_minimize(GtkButton *button, gpointer data)
{
    (void)button;
    gtk_window_iconify(GTK_WINDOW(((WoztUI *)data)->window));
}

static void on_maximize(GtkButton *button, gpointer data)
{
    (void)button;
    WoztUI *ui = data;
    if (gtk_window_is_maximized(GTK_WINDOW(ui->window)))
        gtk_window_unmaximize(GTK_WINDOW(ui->window));
    else gtk_window_maximize(GTK_WINDOW(ui->window));
}

static void on_close(GtkButton *button, gpointer data)
{
    (void)button;
    /* Blackbeard's native close button already hides to the tray. */
    gtk_widget_hide(((WoztUI *)data)->window);
}

static gboolean on_window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer data)
{
    (void)widget;
    WoztUI *ui = data;
    gboolean maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    for (guint i = 0; i < ui->resize_count; i++)
        gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(ui->overlay),
                                             ui->resize_zones[i], maximized);
    GtkWidget *image = gtk_button_get_image(GTK_BUTTON(ui->maximize_btn));
    if (GTK_IS_IMAGE(image))
        gtk_image_set_from_icon_name(GTK_IMAGE(image),
           maximized
                ? "view-restore-symbolic" : "view-fullscreen-symbolic",
           GTK_ICON_SIZE_MENU);
    return FALSE;
}

typedef struct {
    WoztUI *ui;
    GdkWindowEdge edge;
    const char *cursor_name;
} ResizeZone;

static gboolean on_resize_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    ResizeZone *zone = data;
    if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS ||
        gtk_window_is_maximized(GTK_WINDOW(zone->ui->window))) return FALSE;
    gtk_window_begin_resize_drag(GTK_WINDOW(zone->ui->window), zone->edge,
                                 event->button, (gint)event->x_root,
                                 (gint)event->y_root, event->time);
    return TRUE;
}

static gboolean on_resize_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void)event;
    ResizeZone *zone = data;
    GdkWindow *win = gtk_widget_get_window(widget);
    if (!win) return FALSE;
    GdkCursor *cursor = NULL;
    if (!gtk_window_is_maximized(GTK_WINDOW(zone->ui->window)))
        cursor = gdk_cursor_new_from_name(gtk_widget_get_display(widget), zone->cursor_name);
    gdk_window_set_cursor(win, cursor);
    if (cursor) g_object_unref(cursor);
    return FALSE;
}

static void resize_zone_free(gpointer zone, GClosure *closure)
{
    (void)closure;
    g_free(zone);
}

static void add_resize_zone(WoztUI *ui, GtkWidget *overlay,
                            GdkWindowEdge edge, GtkAlign halign,
                            GtkAlign valign, int w, int h, const char *cursor)
{
    GtkWidget *widget = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(widget), FALSE);
    gtk_widget_set_halign(widget, halign);
    gtk_widget_set_valign(widget, valign);
    gtk_widget_set_size_request(widget, w, h);
    gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK);
    ResizeZone *zone = g_new0(ResizeZone, 1);
    zone->ui = ui; zone->edge = edge; zone->cursor_name = cursor;
    g_signal_connect_data(widget, "button-press-event", G_CALLBACK(on_resize_press),
                          zone, resize_zone_free, 0);
    g_signal_connect(widget, "enter-notify-event", G_CALLBACK(on_resize_enter), zone);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), widget);
    g_assert(ui->resize_count < G_N_ELEMENTS(ui->resize_zones));
    ui->resize_zones[ui->resize_count++] = widget;
}

static GtkWidget *icon_button(const char *icon, const char *fallback, const char *tip)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
    if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), icon)) {
        gtk_button_set_label(GTK_BUTTON(button), fallback);
        gtk_widget_destroy(image);
    } else {
        gtk_button_set_image(GTK_BUTTON(button), image);
        gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    }
    gtk_widget_set_tooltip_text(button, tip);
    gtk_widget_set_can_focus(button, TRUE);
    return button;
}

WoztUI *wozt_ui_new(GtkWidget *window, const char *icon_path)
{
    WoztUI *ui = g_new0(WoztUI, 1);
    ui->window = window;
    ui->theme_id = g_strdup("ocean");
    ui->language_id = g_strdup("en");
    ui->translations = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    load_settings(ui);

    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_widget_set_size_request(window, 565, 440);
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "wozt-window");

    ui->css = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(ui->css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    GtkWidget *overlay = gtk_overlay_new();
    ui->overlay = overlay;
    gtk_container_add(GTK_CONTAINER(window), overlay);
    ui->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui->root), "wozt-root");
    gtk_container_add(GTK_CONTAINER(overlay), ui->root);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "wozt-topbar");
    gtk_box_pack_start(GTK_BOX(ui->root), header, FALSE, FALSE, 0);

    GtkWidget *drag = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag), FALSE);
    gtk_widget_add_events(drag, GDK_BUTTON_PRESS_MASK);
    GtkWidget *branding = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_container_add(GTK_CONTAINER(drag), branding);
    if (icon_path && g_file_test(icon_path, G_FILE_TEST_EXISTS)) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(icon_path, 23, 23, TRUE, NULL);
        if (pb) {
            gtk_box_pack_start(GTK_BOX(branding), gtk_image_new_from_pixbuf(pb), FALSE, FALSE, 0);
            g_object_unref(pb);
        }
    }
    GtkWidget *title = gtk_label_new("Blackbeard VOID PRO");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "wozt-title");
    gtk_box_pack_start(GTK_BOX(branding), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), drag, TRUE, TRUE, 0);
    g_signal_connect(drag, "button-press-event", G_CALLBACK(on_move_press), ui);

    ui->theme_combo = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(ui->theme_combo, "Theme");
    gtk_widget_set_size_request(ui->theme_combo, 116, -1);
    gtk_box_pack_start(GTK_BOX(header), ui->theme_combo, FALSE, FALSE, 0);
    ui->language_combo = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(ui->language_combo, "Language");
    gtk_widget_set_size_request(ui->language_combo, 100, -1);
    gtk_box_pack_start(GTK_BOX(header), ui->language_combo, FALSE, FALSE, 0);
    GtkWidget *reload = icon_button("view-refresh-symbolic", "R", "Reload themes and translations");
    gtk_box_pack_start(GTK_BOX(header), reload, FALSE, FALSE, 0);
    GtkWidget *minimize = icon_button("window-minimize-symbolic", "−", "Minimize");
    gtk_box_pack_start(GTK_BOX(header), minimize, FALSE, FALSE, 0);
    ui->maximize_btn = icon_button("view-fullscreen-symbolic", "□", "Maximize or restore");
    gtk_box_pack_start(GTK_BOX(header), ui->maximize_btn, FALSE, FALSE, 0);
    GtkWidget *close = icon_button("window-close-symbolic", "×", "Hide to system tray");
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "wozt-danger");
    gtk_box_pack_start(GTK_BOX(header), close, FALSE, FALSE, 0);
    g_signal_connect(reload, "clicked", G_CALLBACK(on_reload), ui);
    g_signal_connect(minimize, "clicked", G_CALLBACK(on_minimize), ui);
    g_signal_connect(ui->maximize_btn, "clicked", G_CALLBACK(on_maximize), ui);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close), ui);
    g_signal_connect(window, "window-state-event", G_CALLBACK(on_window_state), ui);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(ui->root), scroll, TRUE, TRUE, 0);
    ui->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(ui->body), 10);
    gtk_container_add(GTK_CONTAINER(scroll), ui->body);

    /* Four sides first, then four corners so corner hitboxes win. */
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_NORTH, GTK_ALIGN_FILL, GTK_ALIGN_START, -1, 5, "n-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_SOUTH, GTK_ALIGN_FILL, GTK_ALIGN_END, -1, 5, "s-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_WEST, GTK_ALIGN_START, GTK_ALIGN_FILL, 5, -1, "w-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_EAST, GTK_ALIGN_END, GTK_ALIGN_FILL, 5, -1, "e-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_NORTH_WEST, GTK_ALIGN_START, GTK_ALIGN_START, 12, 12, "nw-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_NORTH_EAST, GTK_ALIGN_END, GTK_ALIGN_START, 12, 12, "ne-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_SOUTH_WEST, GTK_ALIGN_START, GTK_ALIGN_END, 12, 12, "sw-resize");
    add_resize_zone(ui, overlay, GDK_WINDOW_EDGE_SOUTH_EAST, GTK_ALIGN_END, GTK_ALIGN_END, 12, 12, "se-resize");

    g_signal_connect(ui->theme_combo, "changed", G_CALLBACK(on_theme_changed), ui);
    g_signal_connect(ui->language_combo, "changed", G_CALLBACK(on_language_changed), ui);
    reload_resources(ui);
    return ui;
}

GtkWidget *wozt_ui_body(WoztUI *ui)
{
    return ui->body;
}
