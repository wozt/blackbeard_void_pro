#include "config.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

const int bvp_band_freq[BVP_BANDS] = { 32, 64, 125, 250, 500, 1000,
                                       2000, 4000, 8000, 16000 };

void bvp_config_defaults(bvp_config *c)
{
    c->dolby = true;
    c->dolby_mode = 0;
    for (int m = 0; m < BVP_MODES; m++) {
        for (int i = 0; i < BVP_BANDS; i++)
            c->eq[m][i] = 0.0;      /* the iCUE curve is NOT replayed here:
                                       the convolution filters were measured
                                       with the equaliser flat, see work.md */
        c->preamp[m] = 0.0;
    }
    c->r = 0; c->g = 0; c->b = 255;
    c->brightness = 200;
    c->mic_gain   = 100;   /* percent */
    c->headless   = false;
}

static char *config_path(void)
{
    return g_build_filename(g_get_user_config_dir(),
                            "blackbeard_void_pro", "config", NULL);
}

void bvp_config_load(bvp_config *c)
{
    bvp_config_defaults(c);

    char *path = config_path();
    char *data = NULL;
    if (!g_file_get_contents(path, &data, NULL, NULL)) {
        g_free(path);
        return;
    }
    g_free(path);

    char **lines = g_strsplit(data, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *eq = strchr(lines[i], '=');
        if (!eq || lines[i][0] == '#')
            continue;
        *eq = '\0';
        const char *key = g_strstrip(lines[i]);
        const char *val = g_strstrip(eq + 1);

        if (!strcmp(key, "dolby"))            c->dolby = atoi(val) != 0;
        else if (!strcmp(key, "dolby_mode"))  c->dolby_mode = atoi(val);
        else if (!strcmp(key, "preamp")) {    /* pre-per-mode files */
            for (int m = 0; m < BVP_MODES; m++)
                c->preamp[m] = g_ascii_strtod(val, NULL);
        }
        else if (!strncmp(key, "preamp", 6) && key[6] >= '0' &&
                 key[6] < '0' + BVP_MODES && key[7] == '\0')
            c->preamp[key[6] - '0'] = g_ascii_strtod(val, NULL);
        else if (!strcmp(key, "color_r"))     c->r = (uint8_t)atoi(val);
        else if (!strcmp(key, "color_g"))     c->g = (uint8_t)atoi(val);
        else if (!strcmp(key, "color_b"))     c->b = (uint8_t)atoi(val);
        else if (!strcmp(key, "brightness"))  c->brightness = (uint8_t)atoi(val);
        else if (!strcmp(key, "mic_gain"))    c->mic_gain = (uint8_t)atoi(val);
        else if (!strcmp(key, "headless"))    c->headless = atoi(val) != 0;
        else if (!strncmp(key, "eq", 2)) {
            int mode = 0, idx = -1;
            if (strchr(key + 2, '_') &&
                sscanf(key + 2, "%d_%d", &mode, &idx) == 2) {
                /* eq<mode>_<band> */
            } else {
                idx = atoi(key + 2);    /* older files: one shared curve */
                mode = -1;
            }
            if (idx >= 0 && idx < BVP_BANDS) {
                double v = g_ascii_strtod(val, NULL);
                if (mode < 0) {
                    for (int m = 0; m < BVP_MODES; m++)
                        c->eq[m][idx] = v;
                } else if (mode < BVP_MODES) {
                    c->eq[mode][idx] = v;
                }
            }
        }
    }
    g_strfreev(lines);
    g_free(data);
}

void bvp_config_save(const bvp_config *c)
{
    char *dir = g_build_filename(g_get_user_config_dir(),
                                 "blackbeard_void_pro", NULL);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GString *s = g_string_new("# blackbeard_void_pro\n");
    g_string_append_printf(s, "dolby = %d\n", c->dolby ? 1 : 0);
    g_string_append_printf(s, "dolby_mode = %d\n", c->dolby_mode);
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    for (int m = 0; m < BVP_MODES; m++) {
        for (int i = 0; i < BVP_BANDS; i++) {
            g_ascii_dtostr(buf, sizeof(buf), c->eq[m][i]);
            g_string_append_printf(s, "eq%d_%d = %s\n", m, i, buf);
        }
        g_ascii_dtostr(buf, sizeof(buf), c->preamp[m]);
        g_string_append_printf(s, "preamp%d = %s\n", m, buf);
    }
    g_string_append_printf(s, "color_r = %u\ncolor_g = %u\ncolor_b = %u\n",
                           c->r, c->g, c->b);
    g_string_append_printf(s, "brightness = %u\n", c->brightness);
    g_string_append_printf(s, "mic_gain = %u\n", c->mic_gain);
    g_string_append_printf(s, "headless = %d\n", c->headless ? 1 : 0);

    char *path = config_path();
    g_file_set_contents(path, s->str, -1, NULL);
    g_free(path);
    g_string_free(s, TRUE);
}
