# EKIDIO SOUND — Plugins

Ten free, analog-inspired plugins for macOS (Audio Unit / AUv2): eight compressors, a character EQ and a soft tube air enhancer.

**Website:** https://ekidio.github.io/ekidio_sound_plugins/ · [Magyar](https://ekidio.github.io/ekidio_sound_plugins/hu/)
**Download:** [latest release (DMG installer)](https://github.com/Ekidio/ekidio_sound_plugins/releases/latest)

![EKIDIO SOUND plugins](docs/assets/montage.jpg)

| Plugin | Character |
|---|---|
| **OPTEKI** | optical leveling amplifier |
| **FETEKI** | FET peak limiter |
| **VARIEKI** | variable-mu dynamics (dual peak + RMS detection) |
| **BUSEKI** | stereo VCA bus compressor |
| **APEKI** | punch bus compressor |
| **DBEKI** | RMS compressor / limiter |
| **TUBEKI** | opto tube compressor |
| **FAIREKI** | variable-mu limiter with LAT/VERT |
| **DYNEKI-6** | six-band character EQ (BRIT / PUNCH), M/S, SAT and one-knob COMP |
| **SATEKI** | soft tube air enhancer (three tube stages, 5–16 kHz) |

Requirements: macOS 11+, Apple Silicon or Intel, an AU host (Logic Pro, GarageBand, Ableton Live, Reaper…). **Audio Unit (AU) only** — no VST3 version is planned.

## Install

Open the DMG and run **Install EKIDIO SOUND Plugins.pkg**. If macOS cannot verify the developer, click *Done*,
then *System Settings → Privacy & Security → Open Anyway*. Restart your DAW.

## Build from source

```bash
Scripts/build.sh all               # universal build, ad-hoc signing, install to ~/Library, auval -strict
Scripts/make-installer.sh          # build/EKIDIO-SOUND-Plugins-<version>.pkg and .dmg
```

Developer notes (Hungarian): [README.hu.md](README.hu.md)

## License

Free to use, not for redistribution — see [LICENSE.md](LICENSE.md).
