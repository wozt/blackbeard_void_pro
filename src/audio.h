/* PipeWire processing chain: the measured 2x2 convolution matrix (the
   spatialiser) followed by a preamp and a 10-band equaliser.

   `pw-cli -m load-module` keeps the module loaded for as long as the
   process lives, so the child is kept around and killing it removes the
   chain. */
#ifndef BVP_AUDIO_H
#define BVP_AUDIO_H

#include <stdbool.h>
#include "config.h"

/* Directory holding ir_LL.wav, ir_LR.wav, ir_RL.wav, ir_RR.wav. */
void bvp_audio_set_filter_dir(const char *dir);

/* (Re)builds the chain. Without measured filters only the equaliser is
   applied. Returns false and fills `err` (caller frees) on failure. */
bool bvp_audio_apply(const bvp_config *cfg, char **err);

/* Removes the chain and points the default output back at the headset. */
void bvp_audio_stop(void);

bool bvp_audio_active(void);

/* Kills filter chains left behind by a previous run. Without this they
   pile up as ghost sinks: pw-cli keeps the module loaded for as long as it
   lives, and it outlives the application when that one is killed. */
void bvp_audio_cleanup_stale(void);

/* Corsair sink name, or NULL if absent (caller frees). */
char *bvp_audio_find_sink(void);

/* Microphone gain, 0-100 %. This is the headset's own hardware capture
   volume ("Headset Capture Volume", 0-64 in ALSA), which is what iCUE's
   "mic boost" slider drives -- no vendor protocol involved. */
int  bvp_audio_get_mic_gain(void);
void bvp_audio_set_mic_gain(int percent);

#endif
