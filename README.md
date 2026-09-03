# Blackbeard VOID PRO

Native Linux control for the **Corsair VOID PRO Wireless** headset — no
iCUE, no Windows VM, no Wine.

| Feature | Notes |
|---|---|
| Virtual surround ("Dolby") | Reproduced by convolution from measurements of the real USB stream; three generations to pick from |
| 10-band equaliser + preamp | With a live curve display |
| Battery level and charge state | Read over HID, matches iCUE's reading |
| LED colour and brightness | Full lighting protocol |
| Microphone gain | Drives the headset's hardware capture volume |
| Tray icon, hot-plug, autostart | Single instance, headless mode available |

## Install

```sh
./install.sh
```

It checks dependencies, builds, and then asks — separately — whether to
install the udev rule, copy the binary into `~/.local`, add a menu entry
and enable autostart. Nothing happens without a yes.

Build dependencies on Debian/Ubuntu:

```sh
sudo apt install libgtk-3-dev libayatana-appindicator3-dev libhidapi-dev
```

PipeWire is required at runtime.

## How the surround works

The headset's "Dolby" mode is **not** a switch on the device: Windows
applies the processing host-side, inside `audiodg.exe`. Playing
logarithmic sweeps and deconvolving the captured USB stream showed a
**binaural crossfeed** — each channel fed into the opposite ear 0.19 ms
later (the interaural delay) through a head-shadow filter, −0.1 dB at
60 Hz but −15.6 dB at 16 kHz.

`filters/` holds the measured 2×2 impulse-response matrix, in three
generations. The first two come from sweeps. The third is identified from
real programme material — music, rain, gunfire, crowds, sixteen clips — by
least squares against what Windows actually sent over USB, and on a clip
it has never seen it leaves 5.6 dB less error than the sweep-derived ones.

That last campaign also showed where the copy stops: the processing is
reproducible, level-independent and stable in time, yet the best linear
fit depends on what is playing. Something upstream of the crossfeed steers
on the signal, the way a Pro Logic II upmix does, so no fixed convolution
gets much below −11 dB of residual — longer filters were tried and only
learn the training clip.

The microphone is mono in hardware — its USB descriptor declares a single
channel, so Windows receives the same stream.

## Layout

```
src/          the application (C, GTK 3, hidapi)
filters/v1/   measured impulse responses, first generation (one sweep)
filters/v2/   second generation: several sweeps averaged, band-limited
filters/v3/   third: identified from real programme material
tests/        test battery -- `make test`
install.sh    interactive installer
```

`make test` checks the build, the HID link (battery reading), that all three
convolution generations actually output sound, the settings file, the
desktop integration, single-instance behaviour and that no ghost sink is
left after a `kill -9`. Audio tests route the chain into a null sink, so
nothing is played into the headset.

## Settings

`~/.config/blackbeard_void_pro/config`, one `key = value` per line.
Delete it to restore defaults.
