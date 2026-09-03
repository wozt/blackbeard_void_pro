# Blackbeard VOID PRO

Native Linux control for the **Corsair VOID PRO Wireless** headset — no
iCUE, no Windows VM, no Wine.

| Feature | Notes |
|---|---|
| Virtual surround ("Dolby") | Reproduced by convolution, measured to within 0.01–0.36 dB of Windows; two measurement generations to pick from |
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

That is linear, so convolution reproduces it exactly. `filters/` holds the
measured 2×2 impulse-response matrix.

The microphone is mono in hardware — its USB descriptor declares a single
channel, so Windows receives the same stream.

## Layout

```
src/          the application (C, GTK 3, hidapi)
filters/v1/   measured impulse responses, first generation
filters/v2/   second generation: several sweeps averaged, band-limited
tests/        test battery -- `make test`
install.sh    interactive installer
```

`make test` checks the build, the HID link (battery reading), that both
convolution generations actually output sound, the settings file, the
desktop integration, single-instance behaviour and that no ghost sink is
left after a `kill -9`. Audio tests route the chain into a null sink, so
nothing is played into the headset.

## Settings

`~/.config/blackbeard_void_pro/config`, one `key = value` per line.
Delete it to restore defaults.
