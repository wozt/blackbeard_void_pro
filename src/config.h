/* Persistent settings, in ~/.config/blackbeard_void_pro/config
   Deliberately trivial format: one key = value per line. */
#ifndef BVP_CONFIG_H
#define BVP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define BVP_BANDS 10
#define BVP_MODES 3     /* convolution 1.0, 2.0, 3.0 */

typedef struct {
    bool    dolby;                  /* spatialisation enabled */
    int     dolby_mode;             /* 0 = conv 1.0, 1 = conv 2.0, 2 = conv 3.0 */
    /* One equaliser and preamp per method: they colour the sound
       differently, so a setting tuned for one is wrong for the others. */
    double  eq[BVP_MODES][BVP_BANDS];   /* gains in dB, -12..+12 */
    double  preamp[BVP_MODES];          /* dB */
    uint8_t r, g, b;                /* LED colour */
    uint8_t brightness;             /* 0-255 */
    uint8_t mic_gain;               /* 0-255 */
    bool    headless;               /* start hidden in the tray */
} bvp_config;

void bvp_config_defaults(bvp_config *c);
void bvp_config_load(bvp_config *c);
void bvp_config_save(const bvp_config *c);

extern const int bvp_band_freq[BVP_BANDS];

#endif
