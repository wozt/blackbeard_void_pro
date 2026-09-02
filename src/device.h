/* HID access to the Corsair VOID PRO Wireless headset.
 *
 * Protocol reverse-engineered by capturing iCUE's real USB traffic on
 * Windows (USBPcap + tshark) -- see work.md.
 *
 *  - Battery: write {0xC9, 0x64}; the reply carries the level in
 *    data[2] & 0x7F (bit 7 is a flag) and the state in data[4].
 *  - Lighting: report 0xCB is a register-write protocol shaped as
 *      cb <pair count> <register> <value> ...
 *      colour     : registers 0x53 = R, 0x5F = G, 0x6B = B, written
 *                   inside a full sequence replayed as captured
 *      brightness : the same 0-255 value in 0x2C, 0x26, 0x27, 0x2D,
 *                   0x28, 0x29 (one per LED)
 *    NOTE: replaying this byte-for-byte does not (yet) light the LEDs
 *    on Linux -- see work.md, the missing piece is not in these
 *    reports.
 */
#ifndef BVP_DEVICE_H
#define BVP_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#define BVP_VID 0x1b1c
#define BVP_PID 0x0a14

typedef enum {
    BVP_BAT_UNKNOWN = 0,
    BVP_BAT_NORMAL,      /* headset on, running on battery */
    BVP_BAT_LOW,
    BVP_BAT_CHARGING,
    BVP_BAT_DISCONNECTED /* dongle present but headset off or out of range */
} bvp_battery_state;

typedef struct {
    bool              dongle_present;
    bvp_battery_state state;
    int               percent;      /* -1 when unknown */
} bvp_status;

/* Open the device if plugged in; no-op if already open.
   Returns true when a usable connection is available. */
bool bvp_device_open(void);
void bvp_device_close(void);
bool bvp_device_is_open(void);

/* Query status. Handles unplugging: on an I/O failure it closes
   cleanly so a later call can reopen. */
bvp_status bvp_device_poll(void);

/* Apply a static colour (0-255 per component). */
bool bvp_device_set_color(uint8_t r, uint8_t g, uint8_t b);

/* Apply global brightness (0-255). */
bool bvp_device_set_brightness(uint8_t level);

/* Apply colour and brightness in one pass. Replaying the sequence takes
   about 0.8 s, so prefer this over two separate calls. */
bool bvp_device_set_lighting(uint8_t r, uint8_t g, uint8_t b, uint8_t level);

const char *bvp_state_label(bvp_battery_state s);

#endif
