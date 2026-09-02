#include "desktop.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "icons.h"

static char *icon_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                            "256x256", "apps", NULL);
}

char *bvp_desktop_icon_path(void)
{
    char *dir = icon_dir();
    char *p = g_build_filename(dir, BVP_APP_ID ".png", NULL);
    g_free(dir);
    return p;
}

static char *autostart_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "autostart",
                            BVP_APP_ID ".desktop", NULL);
}

static char *exe_path(void)
{
    char *p = g_file_read_link("/proc/self/exe", NULL);
    return p ? p : g_strdup(BVP_APP_ID);
}

static char *entry_text(bool headless)
{
    char *exe = exe_path();
    char *txt = g_strdup_printf(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Blackbeard VOID PRO\n"
        "Comment=Corsair VOID PRO Wireless control\n"
        "Exec=%s%s\n"
        "Icon=" BVP_APP_ID "\n"
        "Terminal=false\n"
        "Categories=AudioVideo;Audio;Settings;\n"
        "Keywords=corsair;headset;void;surround;equaliser;\n"
        "StartupNotify=false\n",
        exe, headless ? " --headless" : "");
    g_free(exe);
    return txt;
}

void bvp_desktop_install(void)
{
    char *dir = icon_dir();
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    char *icon = bvp_desktop_icon_path();
    bool fresh = !g_file_test(icon, G_FILE_TEST_EXISTS);
    if (fresh)
        bvp_write_boat_png(icon, 256);
    g_free(icon);

    /* A stale icon-theme cache makes name-based lookup miss the icon we
       just published, and the desktop falls back to a generic one. Only
       worth doing when we actually wrote the file. */
    if (fresh) {
        char *theme = g_build_filename(g_get_user_data_dir(), "icons",
                                       "hicolor", NULL);
        char *cmd = g_strdup_printf(
            "sh -c 'gtk-update-icon-cache -f -t \"%s\" >/dev/null 2>&1'", theme);
        g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
        g_free(cmd);
        g_free(theme);
    }

    char *appdir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    g_mkdir_with_parents(appdir, 0755);
    char *entry = g_build_filename(appdir, BVP_APP_ID ".desktop", NULL);
    char *txt = entry_text(false);
    g_file_set_contents(entry, txt, -1, NULL);
    g_free(txt);
    g_free(entry);
    g_free(appdir);
}

bool bvp_desktop_autostart_enabled(void)
{
    char *p = autostart_path();
    bool ok = g_file_test(p, G_FILE_TEST_EXISTS);
    g_free(p);
    return ok;
}

void bvp_desktop_set_autostart(bool enabled, bool headless)
{
    char *p = autostart_path();
    if (enabled) {
        char *dir = g_path_get_dirname(p);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        char *txt = entry_text(headless);
        g_file_set_contents(p, txt, -1, NULL);
        g_free(txt);
    } else {
        g_unlink(p);
    }
    g_free(p);
}
