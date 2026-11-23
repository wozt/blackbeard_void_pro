# Blackbeard VOID PRO

Native Linux control for the **Corsair VOID PRO Wireless** headset — no
iCUE, no Windows VM, no Wine.

| Feature | Notes |
|---|---|
| Virtual surround ("Dolby") | Reproduced by convolution from measurements of the real USB stream; two generations to pick from |
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
It is nonetheless **not offered in the interface**: 2.0 is the one that
sounds right, and listening wins over the residual. The filters, the audio
path and the tests stay; `BVP_MODES_OFFERED` in `src/main.c` puts it back.

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
filters/v3/   third: identified from programme material, not offered in the UI
tests/        test battery -- `make test`
install.sh    interactive installer
```

`make test` runs the safe build and static checks without affecting PipeWire.
Live convolution and device diagnostics require an explicit opt-in,
as explained below.

## Settings

`~/.config/blackbeard_void_pro/config`, one `key = value` per line.
Delete it to restore defaults.

## License

Blackbeard VOID PRO is licensed under the GNU General Public License v3.0.
See [LICENSE](LICENSE) for details.

## WOZT GTK Standard interface

The GTK3 dashboard now uses a borderless custom header (drag, double-click
maximize, minimize, restore, and tray-hide), eight resize edges/corners,
responsive two-column panels that stack in narrow windows, vertical-only
scrolling, and the seven JSON palettes from
[wozt-gtk-standard](https://github.com/wozt/wozt-gtk-standard).

Theme and language IDs are stored separately in
`~/.config/blackbeard_void_pro/ui.ini`; the previous audio configuration file
remains untouched. JSON resources live in `data/themes/` and
`data/languages/`, with live selectors and reload in the header. Valid JSON files placed in
`~/.config/blackbeard_void_pro/themes/` or `languages/` can extend or override
the bundled resources. English is
the fallback for untranslated audio-specific diagnostics and tooltips.

Debian build requirements now include `libjson-glib-dev`, in addition to the
existing GTK3, libayatana-appindicator and hidapi development packages.

```bash
sudo apt install libgtk-3-dev libjson-glib-dev libayatana-appindicator3-dev libhidapi-dev
make -j"$(nproc)"
python3 -m unittest discover -s tests -p 'test_wozt_ui_assets.py'
./blackbeard_void_pro
```

For an optional portable x86_64 build, install `linuxdeploy` and its GTK
plugin, then run `bash tools/build-appimage.sh`. Verify the resulting AppImage
on another Linux desktop before release.

## Tests et sécurité audio

La commande `make test` exécute uniquement les **tests sûrs** : compilation,
validation JSON, garde-fous de la suite et présence des filtres. Elle ne
lance pas Blackbeard, ne lit pas `/dev/hidraw*` et ne modifie jamais
PipeWire, le périphérique audio par défaut ou les flux des applications.

Pour les tests matériels, fermer Blackbeard depuis le tray, puis lancer
**volontairement**, en sachant que le son peut être interrompu :

```bash
bash tests/run_tests_live.sh device --i-accept-audio-interruption
bash tests/run_tests_live.sh audio --i-accept-audio-interruption
```

Les tests audio créent leur propre sortie de test, enregistrent l'ancien
périphérique par défaut et tentent de le restaurer même en cas d'échec.
Ils ne tuent jamais les processus `pw-cli` d'autres applications. Les
anciens tests `cleanup` qui tuaient l'application avec `kill -9` ne font
plus partie de la suite automatisée ; ils nécessitent un banc d'essai
PipeWire isolé avant d'être réintroduits.
