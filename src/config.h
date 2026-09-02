/* Persistent settings, in ~/.config/blackbeard_void_pro/config
   Deliberately trivial format: one key = value per line. */
#ifndef BVP_CONFIG_H
#define BVP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define BVP_BANDS 10

typedef struct {
    bool    dolby;                  /* spatialisation enabled */
    double  eq[BVP_BANDS];          /* gains in dB, -12..+12 */
    double  preamp;                 /* dB */
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
