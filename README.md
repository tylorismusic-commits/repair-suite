# Repair Suite

**Status: feature-complete alpha.** One CLAP + LV2 plug-in with the automatic
repair chain and an in-window spectral editor. Manual tuning on real material
and the preset browser are the remaining release work.

Repair Suite is a real-time audio repair strip for Linux, in the same visual
language as Comp Suite, Vocal Suite and Space Suite.

![Repair Suite panel](docs/panel.png)

![Spectral repair page](docs/spectral.png)

## Signal chain

```
Input → De-Clip → De-Click → De-Hum → De-Noise → De-Reverb
      → Spectral Repair → Output
```

All stages are always instantiated. Per-stage ON and MIX keep latency constant.
Total latency is **4221 samples** (~87.9 ms at 48 kHz): 1535 for De-Clip, 639
for De-Click and 2047 for the shared STFT. Hum, noise and reverb add no extra
latency.

## Automatic modules

- **De-Clip** — flat-top detection, Fast/Balanced/High reconstruction, iterative
  clipping-consistent STFT mode, automatic thresholds, mix and headroom.
- **De-Click** — median-residual click/crackle detection, stereo-linked cubic
  repair, sensitivity, width and mix.
- **De-Hum** — notch bank and adaptive sinusoidal precision mode, automatic
  50/60 Hz harmonic detection, odd-only, depth and mix.
- **De-Noise** — two-second learned profile, spectral subtraction, transient
  hold, smoothing, reduction and mix. The profile is saved in session state.
- **De-Reverb** — late-tail spectral subtraction with tail length, pre-delay,
  transient protection, tilt and mix, sharing the denoise STFT.

## Spectral editor

- Rolling capture, stable freeze snapshot, full spectrogram with log frequency
  axis.
- Rectangle, lasso and brush selection.
- Time zoom (wheel or buttons) and right-drag panning.
- Attenuate, Erase, Interpolate, Patch, basic Match and Amplify.
- Undo/redo, edit list, per-edit host-time anchoring and playback overlay.
- Preview loop of the selected/edited audio.
- Original-vs-processed difference spectrogram.
- Spectral edits are saved in CLAP and LV2 session state.

LV2 spectral capture depends on the host providing `instance-access`; when it
is absent the page says so. CLAP always has direct access.

## Build

Requires CMake 3.16+, a C++17 compiler, Cairo, FreeType, X11, LV2 and lilv
development packages.

```sh
git submodule update --init --recursive   # if third_party is a submodule
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLV2_DIR="$HOME/.lv2" -DCLAP_DIR="$HOME/.clap"
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build
```

Installs to:

- `~/.clap/RepairSuite.clap` + `RepairSuite.resources/`
- `~/.lv2/repair-suite.lv2/`

## Still to do

- Preset browser UI (the preset table and LV2 preset export already exist).
- Real-world listening/tuning pass on vinyl, dialogue, drums and clipped
  material.
- More sophisticated Match (the current version repeats the last repair at
  similar-band events).
- ARA/standalone spectral editing, if the Linux host ecosystem makes it useful.

Full research, design and roadmap: [`docs/RESEARCH_AND_PLAN.md`](docs/RESEARCH_AND_PLAN.md).

Licence: GPL-3.0-or-later.
