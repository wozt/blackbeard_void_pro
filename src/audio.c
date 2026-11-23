#include "audio.h"

#include <glib.h>
#include <math.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static GPid  chain_pid = 0;
static char *filter_dir = NULL;

#define SINK_NAME "blackbeard_in"

/* BVP_PIPEWIRE_FAILSAFE_V4 */
/* A pending promotion may never select a non-existent or stale virtual sink. */
static guint promote_source = 0;
static unsigned int promote_attempts = 0;

/* Parse a pactl short-list row safely, even with variable whitespace. */
static gboolean sink_row(const char *line, int *id, char **name)
{
    char **parts = g_strsplit_set(line, "\t ", -1);
    int field = 0;
    char *first = NULL, *second = NULL;
    for (int i = 0; parts[i]; i++) {
        if (!parts[i][0]) continue;
        if (field++ == 0) first = parts[i];
        else { second = parts[i]; break; }
    }
    gboolean ok = FALSE;
    if (first && second) {
        char *end = NULL;
        long index = strtol(first, &end, 10);
        if (end && !*end && index >= 0 && index <= G_MAXINT) {
            if (id) *id = (int)index;
            if (name) *name = g_strdup(second);
            ok = TRUE;
        }
    }
    g_strfreev(parts);
    return ok;
}

static char *pactl_sinks(void)
{
    char *out = NULL;
    int status = 0;
    if (!g_spawn_command_line_sync("pactl list short sinks", &out,
                                    NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL)) {
        g_free(out);
        return NULL;
    }
    return out;
}

static int sink_index(const char *wanted)
{
    char *out = pactl_sinks();
    if (!out || !wanted) { g_free(out); return -1; }
    char **lines = g_strsplit(out, "\n", -1);
    int index = -1;
    for (int i = 0; lines[i]; i++) {
        int id = -1;
        char *name = NULL;
        if (sink_row(lines[i], &id, &name) && g_strcmp0(name, wanted) == 0) {
            index = id;
            g_free(name);
            break;
        }
        g_free(name);
    }
    g_strfreev(lines);
    g_free(out);
    return index;
}

static gboolean pactl_action(const char *action, const char *value,
                             const char *extra)
{
    gchar *argv[] = { (gchar *)"pactl", (gchar *)action, (gchar *)value,
                      (gchar *)extra, NULL };
    gint status = 0;
    GError *error = NULL;
    gboolean launched = g_spawn_sync(NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        NULL, NULL, NULL, NULL, &status, &error);
    gboolean ok = launched && g_spawn_check_wait_status(status, NULL);
    if (!ok)
        g_warning("pactl %s %s failed: %s", action, value,
                  error ? error->message : "command exited unsuccessfully");
    g_clear_error(&error);
    return ok;
}

static char *current_default_sink(void)
{
    char *out = NULL;
    int status = 0;
    if (!g_spawn_command_line_sync("pactl get-default-sink", &out, NULL,
                                    &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL)) {
        g_free(out);
        return NULL;
    }
    if (!out) return NULL;
    g_strstrip(out);
    return out;
}

/* Moving *every* stream is dangerous: it can move the convolution's own
   playback back into blackbeard_in, creating a self-loop and silence.
   Only move identifiable ordinary application streams on the specific
   source sink. Unknown/system/filter streams stay where they are. */
static void move_safe_streams(const char *from, const char *to)
{
    int from_index = sink_index(from);
    if (from_index < 0 || sink_index(to) < 0) return;

    char *out = NULL;
    int status = 0;
    if (!g_spawn_command_line_sync("pactl list sink-inputs", &out,
                                    NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL) || !out) {
        g_free(out);
        return;
    }
    char **blocks = g_strsplit(out, "Sink Input #", -1);
    for (int i = 1; blocks[i]; i++) {
        int stream_id = -1, actual_sink = -1;
        const char *sink_field = strstr(blocks[i], "Sink:");
        if (sscanf(blocks[i], "%d", &stream_id) != 1 ||
            !sink_field || sscanf(sink_field, "Sink: %d", &actual_sink) != 1 ||
            stream_id < 0 || actual_sink != from_index)
            continue;

        /* Filter chain / monitor / unknown clients are never redirected. */
        if (!strstr(blocks[i], "application.name =") ||
            strstr(blocks[i], "blackbeard_out") ||
            strstr(blocks[i], "blackbeard_in") ||
            strstr(blocks[i], "libpipewire-module-filter-chain") ||
            strstr(blocks[i], "Blackbeard VOID PRO") ||
            strstr(blocks[i], "application.name = \"PipeWire\"") ||
            strstr(blocks[i], "application.name = \"pw-cli\""))
            continue;

        char id[32];
        g_snprintf(id, sizeof(id), "%d", stream_id);
        (void)pactl_action("move-sink-input", id, to);
    }
    g_strfreev(blocks);
    g_free(out);
}

/* Only override the default when it is Blackbeard's *own* virtual sink.
   Respect explicit choices such as HDMI or a different headset. */
static void restore_hardware_route(void)
{
    char *current = current_default_sink();
    if (g_strcmp0(current, SINK_NAME) == 0) {
        char *physical = bvp_audio_find_sink();
        if (physical && sink_index(physical) >= 0 &&
            pactl_action("set-default-sink", physical, NULL))
            move_safe_streams(SINK_NAME, physical);
        else {
            /* The selected virtual sink may disappear at any moment.
               Drop the stale default hint; let WirePlumber choose among
               the remaining real outputs instead of keeping silence. */
            gchar *args[] = { (gchar *)"wpctl", (gchar *)"clear-default",
                              (gchar *)"0", NULL };
            gint exit_status = 0;
            (void)g_spawn_sync(NULL, args, NULL,
                G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL,
                &exit_status, NULL);
            g_warning("Blackbeard: physical Corsair sink unavailable; "
                      "requested WirePlumber auto-selection");
        }
        g_free(physical);
    }
    g_free(current);
}

void bvp_audio_set_filter_dir(const char *dir)
{
    g_free(filter_dir);
    filter_dir = g_strdup(dir);
}

bool bvp_audio_active(void)
{
    if (!chain_pid) return false;
    int status = 0;
    pid_t done = waitpid(chain_pid, &status, WNOHANG);
    if (done == 0) return true;
    if (done == chain_pid || (done < 0 && errno == ECHILD)) {
        g_spawn_close_pid(chain_pid);
        chain_pid = 0;
        if (promote_source) {
            g_source_remove(promote_source);
            promote_source = 0;
        }
        restore_hardware_route();
        g_warning("Blackbeard: filter process lost; will retry on next poll");
        return false;
    }
    return true;
}

char *bvp_audio_find_sink(void)
{
    /* Preserve the existing test hook for the null-sink audio tests. */
    const char *forced = g_getenv("BVP_TARGET_SINK");
    if (forced && *forced)
        return g_strdup(forced);

    char *out = pactl_sinks();
    if (!out) return NULL;
    char **lines = g_strsplit(out, "\n", -1);
    char *result = NULL;
    for (int i = 0; lines[i] && !result; i++) {
        char *name = NULL;
        if (sink_row(lines[i], NULL, &name) && name &&
            strstr(name, "Corsair_VOID_PRO") &&
            !g_str_has_prefix(name, "blackbeard_"))
            result = g_strdup(name);
        g_free(name);
    }
    g_strfreev(lines);
    g_free(out);
    return result;
}

static char *find_source(void)
{
    char *out = NULL;
    if (!g_spawn_command_line_sync("pactl list sources short",
                                   &out, NULL, NULL, NULL))
        return NULL;
    char *found = NULL;
    char **lines = g_strsplit(out, "\n", -1);
    for (int i = 0; lines[i] && !found; i++) {
        char **f = g_strsplit(lines[i], "\t", -1);
        /* skip the ".monitor" loopback of the output */
        if (f[0] && f[1] && strstr(f[1], "Corsair_VOID_PRO") &&
            !strstr(f[1], ".monitor"))
            found = g_strdup(f[1]);
        g_strfreev(f);
    }
    g_strfreev(lines);
    g_free(out);
    return found;
}

int bvp_audio_get_mic_gain(void)
{
    char *src = find_source();
    if (!src)
        return -1;
    char *cmd = g_strdup_printf("pactl get-source-volume %s", src);
    char *out = NULL;
    int pct = -1;
    if (g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL) && out) {
        char *p = strchr(out, '%');
        if (p) {                       /* walk back over the digits */
            char *start = p;
            while (start > out && g_ascii_isdigit(start[-1]))
                start--;
            pct = atoi(start);
        }
    }
    g_free(out);
    g_free(cmd);
    g_free(src);
    return pct;
}

void bvp_audio_set_mic_gain(int percent)
{
    char *src = find_source();
    if (!src)
        return;
    percent = CLAMP(percent, 0, 100);
    char *cmd = g_strdup_printf("pactl set-source-volume %s %d%%", src, percent);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
    g_free(cmd);
    g_free(src);
}

/* Runs in the child between fork and exec: ask the kernel to signal it
   when we die, so a crash or a kill -9 on us cannot leave the chain
   loaded. */
static void child_die_with_parent(gpointer data)
{
    (void)data;
    prctl(PR_SET_PDEATHSIG, SIGTERM);
}

void bvp_audio_cleanup_stale(void)
{
    /* Repair an orphaned default *before* removing any stale virtual sink. */
    restore_hardware_route();
    /* Preserve the original precise process match; no generic pw-cli kill. */
    g_spawn_command_line_sync(
        "pkill -f \"libpipewire-module-filter-chain.*" SINK_NAME "\"",
        NULL, NULL, NULL, NULL);
    g_usleep(200000);
    restore_hardware_route();
}

/* Two measurement generations live side by side, so the two can be
   compared by ear:
     v1 -- single sweep, deconvolution regularised at 1e-3, tail faded
           from tap 2800
     v2 -- several sweeps averaged after aligning on the direct peak,
           regularisation lowered to 2e-4 now that the noise floor is
           lower, response band-limited to the swept 20 Hz - 20 kHz, and
           the fade pushed to 3400 so the reverb tail is no longer cut */
static char *version_dir(const bvp_config *cfg)
{
    static const char *dirs[BVP_MODES] = { "v1", "v2", "v3" };
    int m = (cfg && cfg->dolby_mode >= 0 && cfg->dolby_mode < BVP_MODES)
            ? cfg->dolby_mode : 0;
    const char *v = dirs[m];
    char *p = g_build_filename(filter_dir, v, NULL);
    if (g_file_test(p, G_FILE_TEST_IS_DIR))
        return p;
    g_free(p);
    return g_strdup(filter_dir);      /* flat layout: older installs */
}

static bool filters_present(void)
{
    if (!filter_dir)
        return false;
    static const char *n[] = { "ir_LL.wav", "ir_LR.wav", "ir_RL.wav", "ir_RR.wav" };
    for (int i = 0; i < 4; i++) {
        char *p = g_build_filename(filter_dir, "v1", n[i], NULL);
        if (!g_file_test(p, G_FILE_TEST_EXISTS)) {
            g_free(p);
            p = g_build_filename(filter_dir, n[i], NULL);
        }
        bool ok = g_file_test(p, G_FILE_TEST_EXISTS);
        g_free(p);
        if (!ok) return false;
    }
    return true;
}

/* Builds the SPA description of the graph. It is explicitly 2-in / 2-out:
   PipeWire only duplicates mono graphs automatically. The `copy` nodes feed
   both convolvers on each side, and the `mixer` nodes sum the direct and
   crossed paths. */
static char *build_graph(const bvp_config *cfg, const char *target, bool use_ir)
{
    GString *nodes = g_string_new(NULL);
    GString *links = g_string_new(NULL);

    g_string_append(nodes, "{ type = builtin name = inL label = copy } ");
    g_string_append(nodes, "{ type = builtin name = inR label = copy } ");

    const char *headL = "inL:Out", *headR = "inR:Out";

    if (use_ir) {
        static const char *k[4] = { "LL", "LR", "RL", "RR" };
        for (int i = 0; i < 4; i++) {
            char *vd = version_dir(cfg);
            char *fn = g_strdup_printf("ir_%s.wav", k[i]);
            char *p  = g_build_filename(vd, fn, NULL);
            g_free(fn); g_free(vd);
            g_string_append_printf(nodes,
                "{ type = builtin name = c%s label = convolver "
                "config = { filename = \"%s\" } } ", k[i], p);
            g_free(p);
        }
        g_string_append(nodes, "{ type = builtin name = mixL label = mixer } ");
        g_string_append(nodes, "{ type = builtin name = mixR label = mixer } ");
        g_string_append(links,
            "{ output = \"inL:Out\" input = \"cLL:In\" } "
            "{ output = \"inL:Out\" input = \"cLR:In\" } "
            "{ output = \"inR:Out\" input = \"cRL:In\" } "
            "{ output = \"inR:Out\" input = \"cRR:In\" } "
            "{ output = \"cLL:Out\" input = \"mixL:In 1\" } "
            "{ output = \"cRL:Out\" input = \"mixL:In 2\" } "
            "{ output = \"cLR:Out\" input = \"mixR:In 1\" } "
            "{ output = \"cRR:Out\" input = \"mixR:In 2\" } ");
        headL = "mixL:Out";
        headR = "mixR:Out";
    }

    int md = (cfg->dolby_mode == 1) ? 1 : 0;
    double mult = pow(10.0, cfg->preamp[md] / 20.0);
    char tailL[32], tailR[32];
    const char *sides[2] = { "L", "R" };
    const char *heads[2] = { headL, headR };

    for (int s = 0; s < 2; s++) {
        char prev[64];
        g_snprintf(prev, sizeof(prev), "%s", heads[s]);
        g_string_append_printf(nodes,
            "{ type = builtin name = pre%s label = linear "
            "control = { \"Mult\" = %.5f \"Add\" = 0.0 } } ", sides[s], mult);
        g_string_append_printf(links, "{ output = \"%s\" input = \"pre%s:In\" } ",
                               prev, sides[s]);
        g_snprintf(prev, sizeof(prev), "pre%s:Out", sides[s]);

        for (int i = 0; i < BVP_BANDS; i++) {
            char name[32];
            g_snprintf(name, sizeof(name), "b%s%d", sides[s], i + 1);
            g_string_append_printf(nodes,
                "{ type = builtin name = %s label = bq_peaking "
                "control = { \"Freq\" = %d \"Q\" = 1.2 \"Gain\" = %.2f } } ",
                name, bvp_band_freq[i], cfg->eq[md][i]);
            g_string_append_printf(links, "{ output = \"%s\" input = \"%s:In\" } ",
                                   prev, name);
            g_snprintf(prev, sizeof(prev), "%s:Out", name);
        }
        g_snprintf(s == 0 ? tailL : tailR, 32, "%s", prev);
    }

    char *graph = g_strdup_printf(
        "{ node.description = \"Blackbeard VOID PRO\" "
        "media.name = \"Blackbeard VOID PRO\" "
        "filter.graph = { nodes = [ %s ] links = [ %s ] "
        "inputs = [ \"inL:In\" \"inR:In\" ] outputs = [ \"%s\" \"%s\" ] } "
        "capture.props = { node.name = \"%s\" "
        "node.description = \"Corsair headset (Blackbeard)\" "
        "media.class = Audio/Sink audio.channels = 2 audio.position = [ FL FR ] } "
        "playback.props = { node.name = \"blackbeard_out\" node.passive = true "
        "node.target = \"%s\" audio.channels = 2 audio.position = [ FL FR ] } }",
        nodes->str, links->str, tailL, tailR, SINK_NAME, target);

    g_string_free(nodes, TRUE);
    g_string_free(links, TRUE);
    return graph;
}

static gboolean promote_sink(gpointer data)
{
    (void)data;
    if (!chain_pid) { promote_source = 0; return G_SOURCE_REMOVE; }

    /* A successful spawn just means pw-cli started, not that the module
       actually loaded or that its sink exists. */
    int child_status = 0;
    pid_t state = waitpid(chain_pid, &child_status, WNOHANG);
    if (state == chain_pid || (state < 0 && errno == ECHILD)) {
        g_spawn_close_pid(chain_pid);
        chain_pid = 0;
        promote_source = 0;
        g_warning("Blackbeard: filter process exited before audio became ready");
        restore_hardware_route();
        return G_SOURCE_REMOVE;
    }

    char *physical = bvp_audio_find_sink();
    if (!physical || sink_index(physical) < 0) {
        g_warning("Blackbeard: Corsair audio sink disappeared during activation");
        g_free(physical);
        promote_source = 0;
        bvp_audio_stop();
        return G_SOURCE_REMOVE;
    }

    if (sink_index(SINK_NAME) >= 0) {
        if (pactl_action("set-default-sink", SINK_NAME, NULL)) {
            move_safe_streams(physical, SINK_NAME);
            g_free(physical);
            promote_source = 0;
            return G_SOURCE_REMOVE;
        }
        g_warning("Blackbeard: could not activate virtual sink; restoring headset");
        g_free(physical);
        promote_source = 0;
        bvp_audio_stop();
        return G_SOURCE_REMOVE;
    }
    g_free(physical);
    if (++promote_attempts >= 25) { /* 5 s maximum, never switch blindly */
        g_warning("Blackbeard: virtual sink not created after 5 seconds");
        promote_source = 0;
        bvp_audio_stop();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void bvp_audio_stop(void)
{
    if (promote_source) {
        g_source_remove(promote_source);
        promote_source = 0;
    }
    /* Restore audio BEFORE destroying the virtual output. */
    restore_hardware_route();
    if (!chain_pid) return;

    kill(chain_pid, SIGTERM);
    waitpid(chain_pid, NULL, 0);
    g_spawn_close_pid(chain_pid);
    chain_pid = 0;
    g_usleep(250000);
    restore_hardware_route();
}

bool bvp_audio_apply(const bvp_config *cfg, char **err)
{
    char *target = bvp_audio_find_sink();
    if (!target || sink_index(target) < 0) {
        /* If the hardware vanishes, never keep a broken virtual default. */
        bvp_audio_stop();
        if (err) *err = g_strdup("Corsair audio sink unavailable; hardware route preserved");
        g_free(target);
        return false;
    }
    g_free(target);

    /* Cancel old promotions, then stop and restore the previous chain. */
    bvp_audio_stop();
    target = bvp_audio_find_sink();
    if (!target || sink_index(target) < 0) {
        if (err) *err = g_strdup("Corsair disappeared while reloading audio");
        g_free(target);
        return false;
    }

    char *graph = build_graph(cfg, target, filters_present());
    g_free(target);
    if (g_getenv("BVP_DEBUG"))
        g_printerr("=== GRAPH ===\n%s\n=== END ===\n", graph);

    char *argv[] = { "pw-cli", "-m", "load-module",
                     "libpipewire-module-filter-chain", graph, NULL };
    GError *e = NULL;
    gboolean ok = g_spawn_async(NULL, argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        child_die_with_parent, NULL, &chain_pid, &e);
    g_free(graph);
    if (!ok) {
        if (err) *err = g_strdup(e ? e->message : "could not launch pw-cli");
        g_clear_error(&e);
        chain_pid = 0;
        restore_hardware_route();
        return false;
    }
    /* Poll until the sink really exists, instead of blindly assuming 800 ms. */
    promote_attempts = 0;
    promote_source = g_timeout_add(200, promote_sink, NULL);
    return true;
}
