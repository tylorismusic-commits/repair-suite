# Repair Suite — Research and Plan

Status: Phase 1 automatic chain complete · 2026-11-17
Target: first public alpha `0.1.0-alpha.1` for Linux x86_64, CLAP + LV2
Licence: GPL-3.0-or-later

---

## Phase 1 status (2026-11-17)

All five automatic repair modules are now wired into the chain: De-Clip,
De-Click, De-Hum, De-Noise and De-Reverb.  The spectral editor, precision
re-synthesis and adaptive/tonal refinements remain.


- [x] Copy the suite scaffold and rename it to Repair Suite.
- [x] CLAP + LV2 bundles build and load in host smoke tests.
- [x] 48 host parameters covering all planned modules.
- [x] 1200×760 panel skeleton with seven pages and wired controls.
- [x] Shared real-time STFT (2048 / 75%) with identity reconstruction tests.
- [x] Spectral capture/overlay prototype with host-time anchors and tests.
- [x] Wire all five automatic repair modules into the chain.
- [x] CLAP/LV2 UI smoke tests under Xvfb.
- [x] Add the noise-profile state blob (CLAP and LV2 state).
- [ ] Add spectral-edit state blobs.
- [ ] Prototype host transport in a real DAW against the capture overlay.

## 1. Product thesis

Most Linux restoration tools are either single-purpose, offline editors or
speech-tuned denoisers. Repair Suite should be the practical, mix-safe repair
strip: one plug-in that fixes the damage types that actually stop a recording
being usable — clipping, clicks/crackle, hum, broadband noise and room reverb —
and adds a selection-based spectral repair page for the events the automatic
modules cannot reach.

It is not an RX replacement, but it is closer than a simple four-module
utility: one panel, one fixed chain, one preset system, and a spectral editor
that lives in the same window. The design goal is "the repair disappears", not
"the repair has every option".

Design principles:

- **One plug-in.** All modules live in one CLAP/LV2 bundle with one panel. No
  separate installers, no per-module licence checks.
- **One fixed chain, per-stage bypass.** Every module is always instantiated so
  latency never changes when bypassing a stage. The chain strip shows what is
  active.
- **Real-time first, offline-friendly.** Every module must run in a host at
  48 kHz without xruns. High-quality modes may add latency, but it is reported
  and constant for the chosen mode.
- **Learn, then tweak.** Noise and hum have explicit Learn workflows. Auto
  Analyze proposes a starting point; it does not replace listening.
- **Hear what is removed.** Per-module and global Delta monitoring, plus
  residual meters, are first-class controls, not hidden test modes.
- **No black boxes.** Deterministic, documented DSP. Predictable CPU. No model
  downloads, no subscriptions, no telemetry.

## 2. Scope

### In scope for 0.1

| Module | Job |
|---|---|
| **De-Clip** | High-quality digital/analogue flat-top reconstruction, upper/lower thresholds, auto detect |
| **De-Click** | Clicks, vinyl crackle, digital dropouts, plosives/thumps, mouth noise |
| **De-Hum** | Mains hum and buzz, 50/60 Hz with drifting fundamental, up to 96 harmonics |
| **De-Noise** | Steady hiss, air noise, camera/room tone, wind; learned or adaptive profile |
| **De-Reverb** | Late-reverb/room reduction with tail-length and transient protection |
| **Spectral Repair** | Capture, select and repair/attenuate/erase time-frequency events; interpolate/patch |
| **Trim / Output** | DC/rumble filter, input/output trim, mix, Delta, auto output |

### Explicit non-goals for 0.1

- Machine-learning source separation, dialogue isolation or voice extraction.
- A separate standalone editor application. Spectral repair lives in the
  plug-in; a standalone/ARA path remains a future option.
- ARA 2 integration. CLAP/LV2 do not provide ARA; a VST3/ARA build is a major
  separate project.
- Multichannel beyond stereo, Atmos, surround.
- De-wow/flutter, pitch correction, tempo repair.
- A full photographic spectral editor with unlimited undo, layers and batch
  matching in 0.1. The first release needs selection, interpolate, attenuate,
  erase and patch; the advanced match/copy workflow can follow.

## 3. Competitive and prior-art review

| Product | Modules | Notes relevant to us |
|---|---|---|
| iZotope RX 12 | De-click, De-clip, De-crackle, De-hum, De-reverb, Dialogue Isolate, Spectral De-noise, Spectral Repair | The full reference. Spectral Repair is standalone/ARA; the plug-ins are per-module. |
| Acon Restoration Suite 3 | DeNoise 3, DeHum 3, DeClick 3, DeClip 3, all-in-one | Closest automatic-repair match. DeHum uses sinusoidal re-synthesis in precision mode; DeClick separates clicks/crackle/thumps/plosives; DeClip has upper/lower thresholds and level histograms. |
| Acon DeVerberate 3 | De-reverb | Deep-learning dialogue dereverb; shows how high the bar is for "good" de-reverb. |
| CEDAR Retouch | Spectral editor plug-in | The proof that spectral editing can exist inside a plug-in window: capture, select, interpolate, patch, erase, repair. Host/time integration is the hard part. |
| Waves Restoration | X-Noise, X-Click, X-Hum, X-Crackle | Old but shows the value of a simple, low-CPU profile-based workflow. |
| Noise Repellent / libspecbleach | Spectral denoise | Mature GPL/LGPL spectral engine; useful reference for noise estimation, masking veto and transient protection. |
| Audacity Click Removal / Clip Fix | Click removal, declip | Simple, well-understood algorithms: threshold + spike width for clicks; cubic spline across flat tops for clipping. Good baseline quality gates. |
| SPADE / ASPADE | Declipping research | Sparse iterative reconstruction is the quality ceiling, but it is offline-ish and CPU-heavy. |

What that means for the design:

- The table stakes are the automatic modules, per-module bypass/mix, learn
  workflows, delta monitoring and sensible presets.
- The differentiators are: a fixed chain with shared STFT latency, a live
  spectrogram with damage overlays, auto analysis, and an in-plug-in spectral
  repair page.
- "Good de-clip" means shipping the iterative reconstruction path, not only the
  cubic fallback. It is the quality item users will judge first.
- "De-reverb" must be positioned honestly: a musical reduction tool, not a
  miracle dryer. Transient protection and a tail-length control matter more
  than a huge reduction range.
- Spectral repair is the largest engineering item. It needs a capture buffer,
  a selection UI and a way to apply non-causal edits to a live timeline. Plan it
  as a first-class phase, not a hidden extra tab.
- Avoid copying LGPL/GPL code directly. Implement from published methods and
  keep the LGPL libraries as reference only. If a dependency is used, link it
  dynamically with notices and a relinkable-object policy; prefer no dependency.

## 4. Signal chain

```
Input
  → De-Clip
  → DC / rumble HPF (12 dB/oct, 5–40 Hz)
  → De-Click
  → De-Hum
  → De-Noise
  → De-Reverb
  → Spectral Repair (manual, timeline-anchored)
  → Output trim / auto gain
  → Output
```

Order rationale — deepest damage first, then steady tone, then broadband
noise, then variable reverb, then manual events:

- De-clip first, on the raw input before any filter or level change. A high-pass
  filter smears flat tops and alters the trustworthy samples around them;
  de-clipping needs the original rail positions. Estimate DC internally for
  detection, but do not filter before reconstruction.
- DC/rumble filtering next, once the peaks are restored. It gives the click and
  hum detectors a clean low end without having corrupted the declip stage.
- De-click next removes impulses that would otherwise smear through the hum
  estimator and the noise profile.
- De-hum next removes the steady harmonic series before the denoiser estimates
  a broadband floor. It is the last "narrow" automatic damage type.
- De-noise next, on the cleanest possible signal, so it is deciding what is
  noise rather than what is damage. Running it before de-reverb also means the
  denoiser sees a stationary noise floor instead of one modulated by the
  de-reverb gain.
- De-reverb after de-noise, on an already-quieter signal, with transient
  protection keeping direct sounds intact.
- Spectral Repair last: it is the manual escape hatch for events the automatic
  chain cannot handle, applied to the cleaned signal.

All stages are always present. `ON` bypasses DSP but not latency. Each module
also has `MIX` for parallel use; at 0% it is bit-transparent (with latency
compensation).

Latency:

| Module | Mode | Latency at 48 kHz |
|---|---|---|
| De-Clip | High (iterative, default) | ~40–80 ms |
| De-Clip | Balanced (AR) | ~5 ms |
| De-Clip | Fast (cubic) | ~2 ms |
| De-Click | Standard | 5 ms lookahead |
| De-Hum | Notch | 0 |
| De-Hum | Precision (re-synthesis) | ~5–20 ms (tracking window), reported |
| De-Noise | Balanced (2048, 75%) | ~43 ms |
| De-Noise | Low latency (512) | ~11 ms |
| De-Reverb | shares the De-Noise STFT | no additional latency |
| Spectral Repair | timeline overlay | 0 ms in playback; capture pass first |
| Output | DC filter | 0 |

The engine reports the sum. De-Noise and De-Reverb share one analysis/synthesis
pass, so enabling de-reverb does not stack another STFT delay. Switching modes
re-reports; hosts handle it. The default preset uses high-quality declip,
5 ms declick, zero-latency notch hum, balanced denoise and de-reverb.

A full default chain can be ~100–130 ms with High De-Clip + balanced De-Noise.
That is a mixing/repair chain, not a live-tracking chain. Ship a **Low Latency**
preset (Fast De-Clip, notch hum, low-latency denoise, no de-reverb) around
~20 ms, and show the current total latency in the status line.

## 5. DSP design

### 5.1 Shared STFT engine

Put a reusable real-time STFT in the suite first; upstream to `hearth-dsp` if it
stays general.

- Radix-2 real FFT (or vendored PFFFT if profiling demands it).
- Hann analysis and synthesis, 75% overlap, COLA-normalised.
- Preallocated per-channel state; no allocation or locks on the audio thread.
- Selectable frame: 512 / 1024 / 2048 / 4096 samples.
- Shared analysis feeds De-Noise and the UI spectrogram.
- Optional Mid/Side processing for stereo-linked modules.
- Report `frame - hop` latency (plus any module lookahead).

Perfect-reconstruction test: identity gain mask must return the input to
< −120 dBFS error.

### 5.2 De-Clip

De-clip is a headline feature, so the iterative reconstruction path is the
default, not a hidden "best" mode. The cubic path exists for live tracking and
for material where the high-quality path is overkill.

**Detection**

- Flat-top detector: runs of samples at/near the same magnitude, plus a level
  histogram with a spike at the rails.
- Auto threshold from the histogram; separate upper/lower thresholds for
  asymmetric clipping. Estimate and remove DC offset for detection only, then
  preserve the original DC/level through reconstruction.
- Handle both hard digital clipping and soft/analogue saturation: a "soft"
  detector uses curvature and run-length statistics rather than only exact rail
  samples. Soft-clip reconstruction is less certain; label it clearly.
- Show clipped percentage and a live input/output histogram. The histogram is
  both a diagnostic and a control (drag the threshold handles).
- Waveform overlay: original flat top vs reconstructed peak, so the user can
  see what is being invented.

**Repair**

- **High (default): clipping-consistent sparse reconstruction.**
  1. Detect reliable samples (`Ωr`), positive clips (`Ω+`) and negative clips
     (`Ω−`).
  2. Initialise clipped runs with cubic Hermite or AR interpolation. A good
     initial guess cuts the iteration count.
  3. STFT (75% overlap) → magnitude shrinkage (hard/soft threshold, optionally
     a social-sparsity operator) → inverse STFT.
  4. Project back onto the clipping-consistent set: reliable samples exactly
     equal the input; positive clips stay at or above `+τ`; negative clips at
     or below `−τ`.
  5. Repeat for a bounded number of iterations (start 8–15, adapt down when
     converged), then overlap-add.
  - Process only blocks that contain clipped runs; clean blocks pass through a
  delay line. This keeps the average CPU low even with a high iteration count.
  - Latency ~40–80 ms at 48 kHz. Report it and keep it constant in this mode.
- **Balanced (AR):** LPC interpolation with boundary matching, no iteration.
  Better than cubic on sustained tonal material and longer runs.
- **Fast (cubic):** cubic Hermite across each clipped run using the slopes at
  the reliable boundaries. Cheap, ~2 ms latency, for live use and light
  clipping.
- **Residual listen:** solo the difference between input and reconstructed
  output to hear what the algorithm is adding. Essential for judging "good"
  versus "over-invented".
- Output headroom control to avoid re-clipping the restored peaks, with a
  "normalise restored peaks" option and an auto-gain that preserves perceived
  level.

Controls: Upper/lower threshold, Auto detect, Mode (High/Balanced/Fast),
Iterations (High), Output headroom, Delta/residual, Mix.

Meters: clip %, restored peak, iteration count, histogram, residual level.

Quality gates: ΔSDR on synthetic clipped sines/noise/music at 1–6 dB clipping;
compare against cubic and AR baselines; no overshoot beyond a configured
ceiling; no pumping or metallic ringing on sustained tones; all-clipped and
all-silent inputs stay finite.


### 5.3 De-Click

**Detection**

- Short-window linear prediction (AR order 8–16) estimated from a sliding clean
  region; residual `e[n] = x[n] - x̂[n]`.
- Robust threshold: `|e[n]| > sensitivity * localResidualRms`, plus a
  derivative/edge detector for very short impulses.
- Width limit in samples/ms. Events longer than the limit are treated as
  dropouts and handled by the gap mode or left alone.
- Stereo-linked detection by default: if either channel is flagged, repair both
  with their own boundaries, preserving the image. Per-channel option.
- Modes:
  - **Click** — isolated events up to 10 ms.
  - **Crackle** — dense, low-level impulses (vinyl/78s), shorter windows and a
    more aggressive threshold.
  - **Thump / plosive** — low-frequency transient detector on the LF envelope;
    repair with HPF + interpolation.
  - **Dropout** — flat/near-zero runs; fill from both sides.

**Repair**

- Short gaps: cubic Hermite between the last reliable sample and the first
  reliable sample, using slopes from the surrounding samples.
- Longer gaps: AR/LPC interpolation, solved over the gap with boundary
  constraints.
- Crossfade repair edges over 1–2 ms to avoid new clicks.
- Optional second detection pass to catch residual edges.

Controls: Mode, Sensitivity, Max width, Repair quality, Stereo link, Delta,
Mix.

Meters: clicks/second, crackle density, longest repaired event, delta level.


### 5.4 De-Hum

Two engines, because "zero latency" and "maximum transparency" are different
jobs:

**Notch mode (default, zero latency)**

- Cascade of biquad notches at `f0 * k`, k = 1..N (N up to 96).
- Separate Q for low/high harmonics; gentle Q default (e.g. 20–40) to avoid
  ringing.
- Odd-harmonic-only switch for buzz.
- Auto fine-tune scans 45–65 Hz for the strongest harmonic series and sets f0.
- Fixed 50/60/auto buttons.
- Try to keep the notch depth adaptive: deeper only when hum is actually
  present, so quiet passages are not hollowed out.

**Precision mode (sinusoidal re-synthesis, low latency)**

- Track f0 with an adaptive notch / phase-locked loop on the fundamental.
- Estimate amplitude and phase of each harmonic with a sliding DFT / Goertzel
  window (40–80 ms).
- Synthesise the harmonic model and subtract it from the input.
- Crossfade parameter updates over one window to avoid zipper and phase jumps.
- Better preservation of programme material than a deep notch bank.
- This is the Acon-style "precision" approach; keep it optional because
  tracking artefacts on complex material are worse than a clean notch.

Controls: Frequency (Auto/50/60/manual), Harmonics, Odd only, Depth, Q/Width,
Precision mode, Fine-tune, M/S, Solo hum, Mix.

Meters: detected f0, hum level per harmonic, residual hum.


### 5.5 De-Noise

**Analysis**

- STFT magnitude with 75% overlap.
- Noise profile:
  - **Learn**: average/median of magnitude bins while the user plays a
    noise-only section; 1–3 seconds is enough; auto-stop.
  - **Adaptive**: minimum statistics (Martin) or a robust percentile tracker,
    with a slower update so speech/music does not get learned as noise.
- Per-bin noise PSD, smoothed across frequency and time.

**Gain rule**

- Start with decision-directed Wiener / MMSE-LSA:
  - a posteriori SNR `γ = |Y|² / λn`
  - a priori SNR `ξ = α * G²_prev * γ_prev + (1-α) * max(γ-1, 0)`
  - gain from the chosen estimator, floored at `gMin`.
- Offer a simpler spectral-subtraction curve as a low-CPU mode.
- Separate broadband and tonal reduction: detect stable peaks in the noise
  profile and allow deeper suppression there without dulling broadband noise.
- Transient protection: if a bin's energy rises sharply relative to its
  neighbours/time history, freeze or limit the gain reduction for that frame.
- Masking veto: compare the estimated residual to the masking threshold and
  back off where the noise is already inaudible.
- Temporal/frequency smoothing to suppress musical noise.
- Optional whitening to keep the residual noise natural instead of "gated".

Controls: Learn, Adaptive, Reduction (dB), Tonal reduction, Smoothing,
Transient protect, Whitening, Frame size, Delta, Mix.

Meters: noise floor, reduction curve over the spectrum, output noise floor,
delta.

Noise profile is **not** a normalised parameter. It is binary state saved with
the host session; see §7.

### 5.6 De-Reverb

De-reverb shares the De-Noise STFT and runs after the denoise gain in the same
analysis/synthesis pass, so it adds no second FFT latency. The target is
musical reduction of late room sound, not forensic drying of dialogue.

**Estimation**

- Estimate the late-reverberant PSD from delayed frames:
  `λr(t,f) = decay(f) * smooth(|Y(t-D,f)|²)`, where `D` is the pre-delay
  (typically 20–80 ms) and `decay(f)` is derived from the tail-length control.
- Optional auto-estimate: track the energy-decay curve after onsets to suggest
  a tail length; the user can override it.
- Work on a mid/side or per-channel basis; keep the stereo image stable.

**Gain**

- Power-domain subtraction with a spectral floor:
  `G = sqrt(max(1 - β * λr / |Y|², gFloor²))`, where `β` is Amount.
- Smooth the gain with a decision-directed / temporal smoother to avoid
  modulation and "swishing".
- Transient/direct protection: detect onsets and hold the gain near unity for
  a short window (5–20 ms), so attacks and consonants are not softened.
- Protect strong harmonic peaks from being mistaken for reverb.
- Optional "early" control to bias toward late-tail removal; do not promise
  early-reflection removal in 0.1.

**Modes**

- **Music (default):** gentle spectral subtraction, strong transient
  protection, tail length control.
- **Speech:** more aggressive reduction and a shorter pre-delay; still
  spectral, not WPE, in 0.1.
- **WPE (future):** weighted prediction error could be an optional speech mode
  later, but it is sensitive to noise and can damage music; do not put it on
  the 0.1 critical path.

Controls: Amount, Tail length (RT60), Pre-delay, Transient protect, Tilt,
M/S, Delta, Mix.

Meters: estimated reverb level, direct/reverb ratio, reduction, delta.

Quality gates: on synthetic convolutions, improve direct-to-reverberant ratio
without more than a small loss of direct energy; no pumping on sustained
tones; no obvious gating on speech; mix at 100% remains usable.

### 5.7 Spectral Repair

This is the largest UI and state feature in the product. It is a manual,
selection-based tool for events the automatic chain cannot remove: a dog bark,
a chair squeak, a cough, a string squeak, a single horn, a click that survived
de-clicking.

**Architecture constraint**

A CLAP/LV2 effect only sees a stream of audio blocks. It has no random access
to the host timeline and no offline file API. True spectral repair is
non-causal: it needs samples from both sides of the selected event. The
practical in-plug-in design is therefore a **capture-and-overlay** workflow,
anchored to host transport:

```
play/capture a section
  → freeze the spectrogram
  → select a time-frequency region
  → reconstruct the region offline from the captured buffer
  → store the replacement PCM keyed to absolute host time
  → on later playback, crossfade the stored replacement over the live signal
```

This gives real spectral repair inside one plug-in for a fixed mix. Limitations
must be explicit:

- It requires host transport (`clap_event_transport` or LV2 `time:Position`).
  Without transport, the page works as a capture/audition lab only.
- Edits are anchored to the timeline. If the track is moved, stretched or
  re-recorded, the user must re-capture; show an "edits are stale" warning when
  the transport jumps or the source changes.
- The replacement is rendered against the signal at capture time. If upstream
  modules change, re-capture the edit.

**Capture buffer**

- Rolling stereo buffer, 10–30 seconds, sample-rate aware.
- Tap point: after De-Reverb, before Output trim. Whatever the user sees and
  captures is what the spectral edit will replace.
- Continuous spectrogram of the captured audio; user can freeze and scroll.
- Store per-channel samples and a host-time stamp for each captured block.
- Memory budget: 30 s stereo at 48 kHz ≈ 11.5 MB, acceptable. Make the capture
  length a setting (5/15/30 s).

**Selection UI**

- Time-frequency rectangle, lasso and brush.
- Snap to time grid / zero crossings for edges.
- Time and frequency rulers, zoom, horizontal scroll.
- Overlay: selection, edit list, original/processed difference.
- Undo/redo per edit; "clear all edits."

**Operations**

| Tool | Behaviour |
|---|---|
| **Attenuate** | Reduce the selected region by 0–96 dB, with soft edges. |
| **Erase** | Remove the region completely (mask + fill with noise floor or silence). |
| **Interpolate** | Reconstruct the region from surrounding time-frequency content; default for clicks, coughs, squeaks. |
| **Patch / Copy** | Replace the region with a similar region from elsewhere in the capture; optional pitch/phase alignment. |
| **Repair** | Detect the loud event inside the selection and suppress only that, leaving the background/ambience intact. |
| **Amplify** | Raise a quiet event by up to +20 dB without lifting the background. |

Interpolation algorithm:

- 2D time-frequency interpolation from the boundary of the selected region, or
  time-domain AR/LPC interpolation for narrow events.
- For tonal material, prefer spectral interpolation along frequency; for
  transient material, prefer time-domain interpolation.
- Crossfade the repaired region into the surrounding audio over 5–20 ms.
- Optional "preserve noise floor" so the repair does not create a silent hole.

**State**

- Edit list: operation, channel mask, absolute start/end time, frequency range,
  parameters, replacement PCM or a deterministic recipe.
- Store replacement PCM because re-deriving it depends on upstream settings and
  the exact capture; cap total stored edit audio (e.g. 30 s) and warn when full.
- Versioned blob, checksum, sample-rate validation; see §7.

**Latency**

- Playback of stored edits: zero additional latency; alignment is by host time.
  The edit overlay is offset by the chain's reported latency so it lands on the
  same output samples the host expects.
- Capture/audition: buffer latency only.
- If an edit region begins before the current playback position (e.g. playback
  starts inside an edit), the stored replacement is applied from the start of
  the region; otherwise the live input is used.

**Out of scope for 0.1**

- Match/find similar events across the file.
- Unlimited layers and non-linear undo.
- ARA integration.
- Automatic source separation.

### 5.8 Auto Analyze

A one-button, non-ML diagnostic that measures a few seconds of input and sets a
safe starting point:

- Hum: autocorrelation/spectral peak test at 50/60 Hz, harmonic count, drift.
- Noise: minimum-statistics floor, spectral flatness, estimated SNR.
- Clipping: histogram rail detection, clipped %.
- Clicks: run the detector at a fixed sensitivity and count events/second.
- Reverb: estimate decay time and direct/reverb ratio from onsets.
- Report "Hum 50 Hz · −32 dB", "Noise floor −57 dBFS", "0.8% clipped",
  "12 clicks/s", "RT60 0.8 s".
- "Apply" sets the modules that are clearly needed; the user can undo/compare.
- No machine learning, no cloud, no hidden profile.

## 6. UI / UX

Panel: 1200×760, same construction and visual language as Comp Suite / Vocal
Suite (Cairo + Jost, letterboxed, scalable), but larger because the spectral
page needs room for a usable time-frequency display. Dark neutral skin with a
distinct repair accent (cool teal/steel rather than the Comp Suite blue).

Layout:

- **Header**: power, preset browser, A/B, global Delta, Auto Analyze, input
  trim, output trim.
- **Chain strip**: `CLIP → CLICK → HUM → NOISE → VERB → SPEC → OUT` with
  per-stage ON and removal activity. DC/rumble lives on the OUT page, after
  de-clip and before the detector stages.
- **Main display**: switches by page:
  - CLIP: waveform + histogram + restored-peak overlay;
  - CLICK: waveform + click markers + event meter;
  - HUM: spectrum + harmonic markers;
  - NOISE / VERB: spectrum + noise/reverb estimate and reduction curve;
  - SPECTRAL: large frozen/live spectrogram with selection tools and edit list.
- **Page tabs**: CLIP · CLICK · HUM · NOISE · VERB · SPECTRAL · OUT. Only the
  controls for the current page are shown; the chain strip switches stages
  directly.
- **Bottom**: input/output meters, removal meters, status line.

Spectral page workflow:

1. Arm Capture and play the damaged section. The rolling buffer fills and the
   spectrogram draws.
2. Freeze. Scroll/zoom to the event and select it with a rectangle, lasso or
   brush.
3. Choose Interpolate, Attenuate, Erase, Patch or Repair. Preview in a loop.
4. Apply. The edit appears in the edit list and is anchored to host time.
5. Return to the other pages. Edits play back automatically on the next pass.

Workflow details:

- Learn buttons arm a one-shot capture and show a countdown/progress.
- Delta is available per module and globally, with automatic latency
  compensation so A/B does not shift in time.
- Right-click typed values, gold value bubble, double-click reset to preset —
  same interaction model as Comp Suite 1.2.
- Presets grouped by material: Vinyl, 78 rpm, Tape, Field, Dialogue, Podcast,
  Drum Room, Guitar DI, Archive, Utility.
- Factory presets set module configuration and generic reduction amounts. They
  cannot contain learned noise profiles or spectral edits; those live in the
  host session state.

## 7. Engineering architecture

Start from the `vocal-suite` scaffold, not the generated `bleed-out` copy: it
has the mature CLAP/LV2/UI/test/CI layout, including `docs/`, `packaging/`,
`lv2/`, `test/`, `tools/` and `.github/`.

```
repair-suite/
  CMakeLists.txt
  src/
    clap/repair_suite_clap.cpp
    repair_suite_lv2.cpp
    params.hpp
    presets.hpp
    version.hpp
    value_text.hpp
    dsp/
      controls.hpp
      engine.hpp
      stft.hpp          (suite-local first, upstream later)
      declip.hpp
      declick.hpp
      dehum.hpp
      denoise.hpp
      dereverb.hpp
      spectral_repair.hpp  (capture, selection, reconstruction)
      spectral_edit.hpp    (edit list, timeline anchoring, state blob)
      input.hpp
      output.hpp
      meters.hpp
    ui/
      geometry.hpp
      lv2_ui.cpp
      panel.cpp
      panel.hpp
      spectrogram.hpp   (spectrogram rendering + selection hit-testing)
  test/
    test_dsp.cpp
    test_panel.cpp
    test_layout.cpp
    test_spectral.cpp
    test_state.cpp
    clap_host_test.cpp
    lv2_host_test.cpp
    lv2_ui_host_test.cpp
    ui_preview.cpp
  tools/
    gen_ttl.cpp
  lv2/
    manifest.ttl
    repair_suite.ttl
    presets.ttl
  docs/
  packaging/
```

Reuse from `hearth-dsp`: biquads, envelope, smoother, crossover, oversampling,
true peak, spectrum. Add a proper FFT/STFT; keep it header-only if practical so
it can move upstream. The de-noise/de-reverb STFT is shared. Spectral repair
uses its own capture buffer and can reuse the same FFT/STFT primitives.

Parameter model: append-only `ParamId`, normalised 0..1 values, one table drives
DSP/CLAP/LV2/TTL/UI. Hidden parameters for module power/mix, consistent with the
other suites.

State:

- CLAP: state extension with parameter values plus a versioned binary blob for
  the noise profile, detected hum frequency, de-reverb estimate and spectral
  edits.
- LV2: `state:interface` with the same blob.
- Spectral edit blob: edit list (operation, channel mask, absolute host-time
  start/end, frequency range, parameters) plus replacement PCM for each edit.
  Cap total edit audio, validate sample rate/channel count, and checksum.
- Noise profile blob: frame size, bin count, float PSD, spectral tilt; reject
  mismatches and fall back to "no profile" rather than loading garbage.
- Blob format: magic, version, sample rate, frame size, bin count, payload,
  checksum. Save/load tests must round-trip bit-exactly.
- If the host does not provide a stable transport, spectral edits are marked
  "audition only" and are not applied to playback; never guess the timeline.

Metadata: suite URI pattern
`https://github.com/tylorismusic-commits/repair-suite`, generated TTL checked
in CI, same preset export approach as the other suites.

## 8. Testing and quality gates

Unit / DSP:

- FFT and STFT: perfect reconstruction, Parseval, impulse/sine frequency
  accuracy.
- De-Hum: notch depth ≥ 30 dB at f0, −3 dB bandwidth, 50/60 Hz tuning accuracy,
  harmonic tracking under ±0.5 Hz drift, minimal loss at 1 kHz.
- De-Click: detect 100% of synthetic impulses above threshold, < 1% false
  positives on clean music, repair SNR improvement, no DC after repair.
- De-Clip: ΔSDR improvement on synthetic clipped sines/noise/music at 1–6 dB
  clipping; compare High vs Balanced vs cubic baselines; restored peak
  overshoot bounded; iteration convergence; no NaN on all-silent/all-clipped
  input.
- De-Noise: SNR improvement and spectral distortion on noise + speech/music;
  learned profile survives state round-trip; identity at 0% mix.
- De-Reverb: direct-to-reverberant ratio / C50 improvement on synthetic
  convolutions; direct-energy loss bounded; no pumping on sustained tones; no
  gating on speech; stable stereo image.
- Spectral Repair: capture buffer aligns with host time; selection maps to the
  correct samples; interpolation fills a synthetic click region without a hole;
  attenuate/erase depths exact; edits survive state round-trip; stale-timeline
  detection; edits apply only when transport matches.
- Latency: reported delay matches actual impulse delay; mode switches do not
  leave stale values.
- Stress: 0/denormal/NaN/inf inputs, 1-frame blocks, mono-in/stereo-out,
  44.1/48/96/192 kHz, all presets finite.

Golden-file tests: render fixed source files (vinyl crackle, 50/60 hum, clipped
drum bus, noisy field recording, roomy vocal) and compare metrics against
committed baselines, with tolerances rather than exact samples.

Host tests: CLAP and LV2 load, parameter round-trip, state save/load, transport
reporting, UI opens under Xvfb, `lv2_validate` clean.

Manual test matrix:

| Material | Damage | Checks |
|---|---|---|
| Vinyl transfer | crackle + rumble | crackle density, no dulling |
| 78 rpm | broadband noise + clicks | intelligibility, musical noise |
| Dialogue | 50/60 hum + room tone | hum residual, naturalness |
| Drum bus | digital clipping | peak reconstruction, cymbals |
| Guitar DI | mains buzz | odd harmonics, pick attack |
| Field recording | wind + clicks | LF handling, no pumping |
| Podcast | camera hiss | noise floor, sibilance |
| Roomy vocal | room reverb | direct presence, no gating |
| Drum room | cymbal wash/reverb | transient preservation |
| Any fixed mix | dog bark / chair squeak / cough | spectral select + interpolate, no hole |

Acceptance for alpha: no crashes, no xruns at 48 kHz/128-sample buffers on the
test machine with all modules on, CPU below the budget, every preset finite,
spectral edits survive save/load and re-play, and a listening panel of at least
three damaged files where the repair is clearly better and not obviously
artefacted.

## 9. Performance budget

Target, stereo, 48 kHz, all automatic modules on, default modes:

- Total CPU: ≤ 10% of one modern core per instance average (release build), with
  a ≤ 25% peak on heavily clipped blocks while the High declipper converges.
- De-Noise + De-Reverb (shared STFT): largest continuous share, ~4–6%.
- De-Clip High: bursty; only active blocks with clipping, ~8–15% while active.
- De-Hum notch: negligible; precision mode under ~1%.
- De-Click / De-Clip Balanced/Fast: under ~1–2%.
- Spectral Repair: offline reconstruction only when applying an edit; playback
  overlay is cheap.
- UI: ≤ 15% of one core at 30 fps with the spectrogram visible, and no audio
  thread allocation.
- Memory: < 64 MB per instance including FFT/STFT buffers, profile, 30 s
  capture buffer and stored spectral edits.

If the budget is missed, the order of cuts is: spectrogram update rate, declip
iteration count, spectral capture length, precision hum, FFT size.

## 10. Licensing

- Project: GPL-3.0-or-later, matching the suite family.
- `hearth-dsp`: in-house, GPL-3.0-or-later.
- FFT: write our own radix-2 first; if we vendor PFFFT, it is BSD-style and
  compatible. Include its notice.
- Do not copy libspecbleach/noise-repellent (LGPL/GPL) source into the tree.
  Use the published algorithms and our own implementation. If we ever link
  libspecbleach, link dynamically and ship notices plus a relink path.
- Do not copy Audacity GPL code directly; reimplement the published cubic
  declip and threshold click-repair methods.
- Include a THIRD_PARTY_NOTICES.md from day one.

## 11. Roadmap

Estimates are rough. The spectral capture/overlay prototype is the schedule
risk; if host transport is unreliable, the spectral page takes longer or ships
as audition-only in the first alpha.

### Phase 0 — spec and scaffold (1–2 days)

- Lock the seven-page scope below; no further modules until 0.2.
- Copy the `vocal-suite` scaffold, rename everything, get an empty plug-in to
  load in CLAP and LV2.
- Build the STFT and its tests; wire a pass-through engine and empty panel.
- [x] Noise-profile state blob and round-trip test.
- [ ] Spectral-edit state blob.
- Prototype host-transport capture/overlay before building the spectral UI.

### Phase 1 — first audible alpha (2–3 weeks)

- [x] De-Clip High (iterative, default), Balanced and Fast modes; auto
  threshold; mix and output headroom.  Histogram/waveform overlay UI still to
  come.
- [x] De-Click click/crackle detection with median-residual detection and
  cubic repair.  AR repair and thump/dropout modes still to come.
- [x] De-Hum notch + automatic 50/60 Hz harmonic scoring.  Precision
  re-synthesis mode is still scaffolded.
- [x] De-Noise learn + spectral subtraction + transient hold.  Adaptive
  profile tracking and tonal split still to come.
- [x] De-Reverb late-tail reduction sharing the De-Noise STFT.  Music-mode
  tuning and WPE speech mode still to come.
- Chain, per-stage bypass/mix, meters, Delta, typed values, presets v1.
- UI pages and live spectrum. Spectrogram page present but read-only.
- Alpha build for manual testing.

### Phase 2 — spectral repair v1 (2–3 weeks)

- Capture buffer and frozen spectrogram.
- Rectangle/lasso/brush selection, zoom/scroll, rulers.
- Attenuate, Erase and Interpolate.
- Timeline anchoring via CLAP transport / LV2 time; audition-only fallback.
- Edit list, undo/redo, edit state blob, stale-timeline warning.
- Preview loop and original/processed difference display.
- Beta with golden-file regression suite.

### Phase 3 — completeness and release polish

- Patch/Copy, Repair and Amplify spectral tools.
- Precision hum mode and Auto Analyze.
- CPU pass, low-latency denoise mode, state-size limits.
- Preset tuning against the manual test matrix.
- Documentation, screenshots, release packaging, CI matrix.
- `0.1.0` release with LV2 + CLAP tarballs.

### Later

- WPE-based speech de-reverb mode, only if the spectral mode leaves a clear gap.
- Spectral match/find-similar-events.
- ARA 2 via a separate VST3 build, if Linux host support makes it worthwhile.
- Optional RNNoise-class neural denoise as a separate model, clearly labelled
  and never required.

## 12. Risks and open questions

**Risks**

- **De-noise is the credibility module.** A weak denoiser makes the whole suite
  feel amateur. Budget the most tuning time here and keep the residual/
  transient protection honest.
- **Declip expectations are high.** "Good de-clip" means the iterative path must
  actually beat cubic/AR on real clipped material, not just exist in a menu.
  Budget for listening tests and for the 40–80 ms default latency.
- **De-reverb is the easiest module to over-promise.** It reduces room sound; it
  does not un-record a room. Keep transient protection strong and make the
  default musical.
- **Spectral repair is the largest risk.** The capture/overlay model only works
  with stable host transport and a fixed mix. If the edit state becomes large,
  save/load and undo become fragile. Prototype the timeline anchoring before
  committing the UI.
- **Musical noise** from spectral gating is the most likely complaint. Build
  smoothing, masking veto and transient protection early, not as polish.
- **Latency stacking.** High declip + denoise/dereverb can push the default
  chain past 100 ms. Keep the total visible and make the low-latency modes real.
- **State compatibility.** Noise profiles, reverb estimates and spectral edit
  blobs can break sessions if handled casually. Version and validate from the
  first commit.
- **Scope creep in the spectral editor.** Selection, interpolate, erase and
  patch are enough for 0.1. Match/find-similar, layers and batch processing are
  a different product.

**Open questions for Tylor**

1. **Spectral workflow:** is the capture-and-overlay model acceptable (works
   with host transport, edits anchored to time, re-capture after moving the
   clip), or do you want to hold out for a true ARA-style editor later?
2. **De-reverb target:** music/room reduction (recommended, spectral, gentle)
   or dialogue drying (more aggressive, possibly WPE later)?
3. **Latency:** is a ~40–80 ms default De-Clip acceptable in exchange for the
   high-quality iterative reconstruction? Fast mode remains ~2 ms.
4. **Panel size:** 1200×760 for the spectral page, or keep 1040×640 and use a
   pop-out/zoom view for spectral editing?
5. **Name/visual identity:** "Repair Suite" with a teal/steel skin, or
   something else?

## 13. References

- Acon Digital Restoration Suite 3 — module set and workflow:
  https://acondigital.com/products/restoration-suite
- iZotope RX — module set and spectral repair positioning:
  https://www.izotope.com/products/rx-advanced
- iZotope, "Order of Operations: Audio Repair" — deepest-damage-first chain:
  https://www.izotope.com/community/blog/order-of-audio-repair-operations
- CEDAR Retouch — in-plug-in spectral editing workflow:
  https://cedaraudio.com/products/cambridge/camretouch
- Acon Digital DeVerberate — de-reverb positioning:
  https://acondigital.com/products/deverberate
- libspecbleach — spectral denoise architecture, noise estimation, masking
  veto, transient protection (LGPL-2.1, reference only):
  https://github.com/lucianodato/libspecbleach
- Noise Repellent — practical denoise UX and low-latency mode:
  https://github.com/lucianodato/noise-repellent
- Audacity Clip Fix — cubic spline declipping baseline:
  https://github.com/audacity/audacity/blob/master/share/nyquist-plug-ins/clipfix.ny
- Audacity Click Removal — threshold/spike-width baseline.
- Gaultier, Kitić, Gribonval, Bertin, "Sparsity-based audio declipping
  methods: selected overview, new algorithms, and large-scale evaluation",
  arXiv:2005.10228.
- Adler et al., "A Constrained Matching Pursuit Approach to Audio Declipping".
- Záviška, Rajmic et al., audio declipping survey and evaluation:
  arXiv:2007.07663.
- Lebart, Boucher, Denbigh, "A New Method Based on Spectral Subtraction for
  Speech Dereverberation".
- Habets, "Late Reverberant Spectral Variance Estimation Based on a Statistical
  Model".
- Drude et al., NARA-WPE — weighted prediction error dereverberation:
  https://github.com/fgnt/nara_wpe
- Martin, "Noise Power Spectral Density Estimation Based on Optimal Smoothing
  and Minimum Statistics".
- Ephraim & Malah, MMSE-LSA speech enhancement.
- Janssen, "Adaptive interpolation of discrete-time signals that can be
  modeled as autoregressive processes" — click detection/interpolation.
- Oudre, "Interpolation of Missing Samples in Audio Signals Based on
  Autoregressive Modeling".
