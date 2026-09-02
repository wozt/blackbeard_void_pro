#include "audio.h"

#include <glib.h>
#include <math.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

static GPid  chain_pid = 0;
static char *filter_dir = NULL;

#define SINK_NAME "blackbeard_in"

void bvp_audio_set_filter_dir(const char *dir)
{
    g_free(filter_dir);
    filter_dir = g_strdup(dir);
}

bool bvp_audio_active(void) { return chain_pid != 0; }

char *bvp_audio_find_sink(void)
{
    char *out = NULL;
    if (!g_spawn_command_line_sync("pactl list sinks short", &out, NULL, NULL, NULL))
        return NULL;

    char *found = NULL;
    char **lines = g_strsplit(out, "\n", -1);
    for (int i = 0; lines[i] && !found; i++) {
        char **f = g_strsplit(lines[i], "\t", -1);
        if (f[0] && f[1] && strstr(f[1], "Corsair_VOID_PRO"))
            found = g_strdup(f[1]);
        g_strfreev(f);
    }
    g_strfreev(lines);
    g_free(out);
    return found;
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
    /* Match on our own node name inside the pw-cli command line: that only
       ever appears in a chain we created. */
    g_spawn_command_line_sync(
        "pkill -f \"libpipewire-module-filter-chain.*" SINK_NAME "\"",
        NULL, NULL, NULL, NULL);
    g_usleep(200000);
}

static void set_default_sink(const char *name)
{
    char *cmd = g_strdup_printf("pactl set-default-sink %s", name);
    g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
    g_free(cmd);

    /* Move streams that are already playing, otherwise they stay on the
       previous destination. Some streams cannot be moved (our own chain
       output, monitors); pactl complains loudly about those, so its
       output is discarded. */
    char *out = NULL;
    if (g_spawn_command_line_sync("pactl list sink-inputs short",
                                  &out, NULL, NULL, NULL) && out) {
        char **lines = g_strsplit(out, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (!*lines[i]) continue;
            char **f = g_strsplit(lines[i], "\t", -1);
            if (f[0] && *f[0]) {
                char *mv = g_strdup_printf(
                    "sh -c 'pactl move-sink-input %s %s >/dev/null 2>&1'",
                    f[0], name);
                g_spawn_command_line_sync(mv, NULL, NULL, NULL, NULL);
                g_free(mv);
            }
            g_strfreev(f);
        }
        g_strfreev(lines);
    }
    g_free(out);
}

static bool filters_present(void)
{
    if (!filter_dir)
        return false;
    static const char *n[] = { "ir_LL.wav", "ir_LR.wav", "ir_RL.wav", "ir_RR.wav" };
    for (int i = 0; i < 4; i++) {
        char *p = g_build_filename(filter_dir, n[i], NULL);
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
            char *fn = g_strdup_printf("ir_%s.wav", k[i]);
            char *p  = g_build_filename(filter_dir, fn, NULL);
            g_free(fn);
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

    double mult = pow(10.0, cfg->preamp / 20.0);
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
                name, bvp_band_freq[i], cfg->eq[i]);
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
    set_default_sink(SINK_NAME);
    return G_SOURCE_REMOVE;
}

void bvp_audio_stop(void)
{
    if (!chain_pid)
        return;
    kill(chain_pid, SIGTERM);
    /* wait for it, otherwise the node name is still taken when we
       immediately reload and the module fails with "Could not load module" */
    waitpid(chain_pid, NULL, 0);
    g_spawn_close_pid(chain_pid);
    chain_pid = 0;
    g_usleep(250000);

    char *sink = bvp_audio_find_sink();
    if (sink) {
        set_default_sink(sink);
        g_free(sink);
    }
}

bool bvp_audio_apply(const bvp_config *cfg, char **err)
{
    char *target = bvp_audio_find_sink();
    if (!target) {
        if (err) *err = g_strdup("sink du casque introuvable");
        return false;
    }

    bvp_audio_stop();

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
        if (err) *err = g_strdup(e ? e->message : "failed to launch pw-cli");
        if (e) g_error_free(e);
        chain_pid = 0;
        return false;
    }
    /* the sink only exists once the module has actually loaded */
    g_timeout_add(800, promote_sink, NULL);
    return true;
}
