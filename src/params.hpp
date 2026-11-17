// Repair Suite — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One table drives the DSP, the CLAP parameters, the editor, the LV2 metadata
// (via tools/gen_ttl.cpp) and the factory presets.  Adding a parameter means
// adding one row here and one field in dsp/controls.hpp.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "dsp/controls.hpp"

namespace repair {

// Parameter ids are the CLAP parameter ids and the index into Shared::values.
// They are also the ordering of the LV2 control ports (port = 4 + id), so this
// enum must only ever be appended to.
enum ParamId : uint32_t {
  // Global
  P_POWER = 0,
  P_INPUT,
  P_OUTPUT,
  P_DELTA,
  P_DC_ON,
  P_DC_FREQ,

  // De-Clip
  P_CLIP_ON,
  P_CLIP_MODE,
  P_CLIP_AUTO,
  P_CLIP_UPPER,
  P_CLIP_LOWER,
  P_CLIP_ITER,
  P_CLIP_HEADROOM,
  P_CLIP_MIX,

  // De-Click
  P_CLICK_ON,
  P_CLICK_MODE,
  P_CLICK_SENS,
  P_CLICK_WIDTH,
  P_CLICK_LINK,
  P_CLICK_MIX,

  // De-Hum
  P_HUM_ON,
  P_HUM_AUTO,
  P_HUM_FREQ,
  P_HUM_HARMONICS,
  P_HUM_ODD,
  P_HUM_DEPTH,
  P_HUM_PRECISION,
  P_HUM_MIX,

  // De-Noise
  P_NOISE_ON,
  P_NOISE_ADAPTIVE,
  P_NOISE_REDUCTION,
  P_NOISE_TONAL,
  P_NOISE_SMOOTH,
  P_NOISE_TRANSIENT,
  P_NOISE_FRAME,
  P_NOISE_MIX,

  // De-Reverb
  P_VERB_ON,
  P_VERB_AMOUNT,
  P_VERB_TAIL,
  P_VERB_PREDELAY,
  P_VERB_TRANSIENT,
  P_VERB_TILT,
  P_VERB_MIX,

  // Spectral Repair
  P_SPEC_ON,
  P_SPEC_OP,
  P_SPEC_AMOUNT,
  P_SPEC_PRESERVE,
  P_SPEC_MIX,

  P_COUNT
};

// Scale-point labels for a stepped parameter.  Values are always 0..count-1.
struct Labels {
  const char* const* names = nullptr;
  int count = 0;
};

template <size_t N>
constexpr Labels labels(const char* const (&arr)[N]) noexcept {
  return Labels{arr, static_cast<int>(N)};
}

// `symbol` is the LV2 port symbol and is part of the saved-session format.
// Never rename one after release.
struct ParamSpec {
  const char* symbol;
  const char* name;
  const char* module;
  double min;
  double max;
  double def;
  bool stepped;
  bool hidden;
  bool toggled;
  const char* unit;
  Labels labels;
  // Recommended range drawn as an arc on the knob.  Same units as min/max.
  double recMin;
  double recMax;
};

inline constexpr const char* kClipModeLabels[] = {"High", "Balanced", "Fast"};
inline constexpr const char* kClickModeLabels[] = {"Click", "Crackle", "Thump",
                                                   "Dropout"};
inline constexpr const char* kNoiseFrameLabels[] = {"512", "1024", "2048",
                                                    "4096"};
inline constexpr const char* kSpectralOpLabels[] = {"Attenuate", "Erase",
                                                    "Interpolate", "Patch",
                                                    "Repair", "Amplify"};

inline constexpr ParamSpec kParamSpecs[P_COUNT] = {
    // Global
    {"power", "Power", "Global", 0.0, 1.0, 1.0, true, false, true, nullptr, {},
     0.0, 1.0},
    {"input", "Input", "Global", -18.0, 18.0, 0.0, false, false, false, "dB",
     {}, -6.0, 6.0},
    {"output", "Output", "Global", -18.0, 18.0, 0.0, false, false, false, "dB",
     {}, -6.0, 6.0},
    {"delta", "Delta", "Global", 0.0, 1.0, 0.0, true, false, true, nullptr, {},
     0.0, 1.0},
    {"dc_on", "DC Filter", "Output", 0.0, 1.0, 1.0, true, false, true, nullptr,
     {}, 0.0, 1.0},
    {"dc_freq", "Rumble", "Output", 5.0, 60.0, 20.0, false, false, false, "Hz",
     {}, 10.0, 30.0},

    // De-Clip
    {"clip_on", "De-Clip On", "De-Clip", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"clip_mode", "Mode", "De-Clip", 0.0, 2.0, 0.0, true, false, false, nullptr,
     labels(kClipModeLabels), 0.0, 2.0},
    {"clip_auto", "Auto Threshold", "De-Clip", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"clip_upper", "Upper", "De-Clip", -24.0, 0.0, 0.0, false, false, false,
     "dB", {}, -6.0, 0.0},
    {"clip_lower", "Lower", "De-Clip", -24.0, 0.0, 0.0, false, false, false,
     "dB", {}, -6.0, 0.0},
    {"clip_iter", "Iterations", "De-Clip", 4.0, 30.0, 12.0, true, false, false,
     nullptr, {}, 8.0, 16.0},
    {"clip_headroom", "Headroom", "De-Clip", 0.0, 12.0, 0.0, false, false,
     false, "dB", {}, 0.0, 6.0},
    {"clip_mix", "Mix", "De-Clip", 0.0, 100.0, 100.0, false, false, false,
     "%", {}, 80.0, 100.0},

    // De-Click
    {"click_on", "De-Click On", "De-Click", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"click_mode", "Mode", "De-Click", 0.0, 3.0, 0.0, true, false, false,
     nullptr, labels(kClickModeLabels), 0.0, 3.0},
    {"click_sens", "Sensitivity", "De-Click", 0.0, 10.0, 5.0, false, false,
     false, nullptr, {}, 3.0, 7.0},
    {"click_width", "Max Width", "De-Click", 0.1, 20.0, 5.0, false, false,
     false, "ms", {}, 1.0, 10.0},
    {"click_link", "Stereo Link", "De-Click", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"click_mix", "Mix", "De-Click", 0.0, 100.0, 100.0, false, false, false,
     "%", {}, 80.0, 100.0},

    // De-Hum
    {"hum_on", "De-Hum On", "De-Hum", 0.0, 1.0, 1.0, true, false, true, nullptr,
     {}, 0.0, 1.0},
    {"hum_auto", "Auto Tune", "De-Hum", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"hum_freq", "Frequency", "De-Hum", 40.0, 70.0, 50.0, false, false, false,
     "Hz", {}, 49.0, 61.0},
    {"hum_harmonics", "Harmonics", "De-Hum", 1.0, 96.0, 12.0, true, false,
     false, nullptr, {}, 4.0, 24.0},
    {"hum_odd", "Odd Only", "De-Hum", 0.0, 1.0, 0.0, true, false, true, nullptr,
     {}, 0.0, 1.0},
    {"hum_depth", "Depth", "De-Hum", 0.0, 60.0, 30.0, false, false, false, "dB",
     {}, 12.0, 48.0},
    {"hum_precision", "Precision", "De-Hum", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"hum_mix", "Mix", "De-Hum", 0.0, 100.0, 100.0, false, false, false, "%",
     {}, 80.0, 100.0},

    // De-Noise
    {"noise_on", "De-Noise On", "De-Noise", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"noise_adaptive", "Adaptive", "De-Noise", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"noise_reduction", "Reduction", "De-Noise", 0.0, 40.0, 12.0, false, false,
     false, "dB", {}, 6.0, 24.0},
    {"noise_tonal", "Tonal", "De-Noise", 0.0, 40.0, 12.0, false, false, false,
     "dB", {}, 6.0, 24.0},
    {"noise_smooth", "Smoothing", "De-Noise", 0.0, 10.0, 5.0, false, false,
     false, nullptr, {}, 3.0, 7.0},
    {"noise_transient", "Transient", "De-Noise", 0.0, 10.0, 5.0, false, false,
     false, nullptr, {}, 3.0, 8.0},
    {"noise_frame", "Frame", "De-Noise", 0.0, 3.0, 2.0, true, false, false,
     nullptr, labels(kNoiseFrameLabels), 1.0, 2.0},
    {"noise_mix", "Mix", "De-Noise", 0.0, 100.0, 100.0, false, false, false,
     "%", {}, 80.0, 100.0},

    // De-Reverb
    {"verb_on", "De-Reverb On", "De-Reverb", 0.0, 1.0, 0.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"verb_amount", "Amount", "De-Reverb", 0.0, 20.0, 6.0, false, false, false,
     "dB", {}, 2.0, 10.0},
    {"verb_tail", "Tail", "De-Reverb", 0.2, 3.0, 0.8, false, false, false, "s",
     {}, 0.4, 1.4},
    {"verb_predelay", "Pre-delay", "De-Reverb", 0.0, 100.0, 30.0, false, false,
     false, "ms", {}, 15.0, 50.0},
    {"verb_transient", "Transient", "De-Reverb", 0.0, 10.0, 6.0, false, false,
     false, nullptr, {}, 4.0, 8.0},
    {"verb_tilt", "Tilt", "De-Reverb", -6.0, 6.0, 0.0, false, false, false,
     "dB", {}, -3.0, 3.0},
    {"verb_mix", "Mix", "De-Reverb", 0.0, 100.0, 100.0, false, false, false,
     "%", {}, 50.0, 100.0},

    // Spectral Repair
    {"spec_on", "Spectral On", "Spectral", 0.0, 1.0, 1.0, true, false, true,
     nullptr, {}, 0.0, 1.0},
    {"spec_op", "Operation", "Spectral", 0.0, 5.0, 2.0, true, false, false,
     nullptr, labels(kSpectralOpLabels), 0.0, 5.0},
    {"spec_amount", "Amount", "Spectral", 0.0, 96.0, 24.0, false, false, false,
     "dB", {}, 6.0, 48.0},
    {"spec_preserve", "Preserve Floor", "Spectral", 0.0, 1.0, 1.0, true, false,
     true, nullptr, {}, 0.0, 1.0},
    {"spec_mix", "Mix", "Spectral", 0.0, 100.0, 100.0, false, false, false,
     "%", {}, 80.0, 100.0},
};

static_assert(sizeof(kParamSpecs) / sizeof(kParamSpecs[0]) == P_COUNT,
              "kParamSpecs must cover every parameter id");

// The shared STFT is 2048 samples, so a noise profile has 1025 bins.
constexpr int kProfileBins = 1025;

// The control values, meters and one-shot UI actions shared between the audio
// thread, the host thread and the editor.
class Shared {
public:
  Shared() {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      values_[p].store(kParamSpecs[p].def, std::memory_order_relaxed);
      dirty_[p].store(false, std::memory_order_relaxed);
    }
    grDb.store(0.0f);
    noiseGrDb.store(0.0f);
    verbGrDb.store(0.0f);
    specActive.store(0.0f);
    inPeakDb.store(-120.0f);
    outPeakDb.store(-120.0f);
    inPeakLDb.store(-120.0f);
    inPeakRDb.store(-120.0f);
    outPeakLDb.store(-120.0f);
    outPeakRDb.store(-120.0f);
    noiseFloorDb.store(-120.0f);
    captureSeconds.store(0.0f);
    captureComplete.store(0.0f);
    transportSeconds.store(0.0);
    transportValid.store(false);
    editCount.store(0);
    captureActive.store(false);
    noiseLearnActive.store(false);
    compare.store(false);
    captureFreeze.store(false);
    clearCapture.store(false);
    clearEdits.store(false);
    profileBins.store(0);
    profileValid.store(false);
    profilePending.store(false);
    for (int i = 0; i < kProfileBins; ++i) {
      noiseProfile[i].store(0.0f);
    }
  }

  double get(uint32_t p) const noexcept {
    return values_[p].load(std::memory_order_relaxed);
  }

  void set(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) {
      return;
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) {
      v = static_cast<double>(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    }
    values_[p].store(v, std::memory_order_relaxed);
    dirty_[p].store(true, std::memory_order_release);
  }

  void setFromHost(uint32_t p, double v) noexcept {
    if (!std::isfinite(v)) {
      return;
    }
    const ParamSpec& s = kParamSpecs[p];
    v = std::min(std::max(v, s.min), s.max);
    if (s.stepped) {
      v = static_cast<double>(static_cast<int>(v + (v >= 0 ? 0.5 : -0.5)));
    }
    values_[p].store(v, std::memory_order_relaxed);
  }

  bool takeDirty(uint32_t p) noexcept {
    return dirty_[p].exchange(false, std::memory_order_acquire);
  }

  bool isDirty(uint32_t p) const noexcept {
    return dirty_[p].load(std::memory_order_acquire);
  }

  bool anyDirty() const noexcept {
    for (uint32_t p = 0; p < P_COUNT; ++p) {
      if (dirty_[p].load(std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  // A learned noise profile is binary session state, not a host parameter.
  // The audio thread publishes it here; state save/load and the UI read it.
  void setProfile(const float* data, int bins) noexcept {
    if (!data || bins <= 0) {
      return;
    }
    const int n = std::min(bins, kProfileBins);
    for (int i = 0; i < n; ++i) {
      noiseProfile[i].store(std::max(0.0f, data[i]),
                            std::memory_order_relaxed);
    }
    profileBins.store(n, std::memory_order_relaxed);
    profileValid.store(true, std::memory_order_release);
    profilePending.store(true, std::memory_order_release);
  }

  int getProfile(float* out, int maxBins) const noexcept {
    if (!profileValid.load(std::memory_order_acquire)) {
      return 0;
    }
    const int n = std::min(profileBins.load(std::memory_order_relaxed),
                           maxBins);
    for (int i = 0; i < n; ++i) {
      out[i] = noiseProfile[i].load(std::memory_order_relaxed);
    }
    return n;
  }

  std::atomic<float> grDb;
  std::atomic<float> noiseGrDb;
  std::atomic<float> verbGrDb;
  std::atomic<float> specActive;
  std::atomic<float> inPeakDb;
  std::atomic<float> outPeakDb;
  std::atomic<float> inPeakLDb;
  std::atomic<float> inPeakRDb;
  std::atomic<float> outPeakLDb;
  std::atomic<float> outPeakRDb;
  std::atomic<float> noiseFloorDb;
  std::atomic<float> captureSeconds;
  std::atomic<float> captureComplete;
  std::atomic<double> transportSeconds;
  std::atomic<bool> transportValid;
  std::atomic<int> editCount;
  std::atomic<bool> captureActive;
  std::atomic<bool> captureFreeze;
  std::atomic<bool> noiseLearnActive;
  std::atomic<bool> compare;
  std::atomic<bool> clearCapture;
  std::atomic<bool> clearEdits;
  std::atomic<float> noiseProfile[kProfileBins];
  std::atomic<int> profileBins;
  std::atomic<bool> profileValid;
  std::atomic<bool> profilePending;
  std::atomic<float> bpm;      // reserved for later tempo-synced capture
  std::atomic<bool> hostTempo;

private:
  std::atomic<double> values_[P_COUNT];
  std::atomic<bool> dirty_[P_COUNT];
};

inline Controls controlsFrom(const Shared& s) noexcept {
  Controls c;
  c.power = s.get(P_POWER) > 0.5;
  c.delta = s.get(P_DELTA) > 0.5;
  c.inputDb = static_cast<float>(s.get(P_INPUT));
  c.outputDb = static_cast<float>(s.get(P_OUTPUT));
  c.dcOn = s.get(P_DC_ON) > 0.5;
  c.dcHz = static_cast<float>(s.get(P_DC_FREQ));

  c.clipOn = s.get(P_CLIP_ON) > 0.5;
  c.clipMode = static_cast<int>(s.get(P_CLIP_MODE) + 0.5);
  c.clipAuto = s.get(P_CLIP_AUTO) > 0.5;
  c.clipUpperDb = static_cast<float>(s.get(P_CLIP_UPPER));
  c.clipLowerDb = static_cast<float>(s.get(P_CLIP_LOWER));
  c.clipIterations = static_cast<int>(s.get(P_CLIP_ITER) + 0.5);
  c.clipHeadroomDb = static_cast<float>(s.get(P_CLIP_HEADROOM));
  c.clipMix = static_cast<float>(s.get(P_CLIP_MIX));

  c.clickOn = s.get(P_CLICK_ON) > 0.5;
  c.clickMode = static_cast<int>(s.get(P_CLICK_MODE) + 0.5);
  c.clickSensitivity = static_cast<float>(s.get(P_CLICK_SENS));
  c.clickWidthMs = static_cast<float>(s.get(P_CLICK_WIDTH));
  c.clickLink = s.get(P_CLICK_LINK) > 0.5;
  c.clickMix = static_cast<float>(s.get(P_CLICK_MIX));

  c.humOn = s.get(P_HUM_ON) > 0.5;
  c.humAuto = s.get(P_HUM_AUTO) > 0.5;
  c.humHz = static_cast<float>(s.get(P_HUM_FREQ));
  c.humHarmonics = static_cast<int>(s.get(P_HUM_HARMONICS) + 0.5);
  c.humOdd = s.get(P_HUM_ODD) > 0.5;
  c.humDepthDb = static_cast<float>(s.get(P_HUM_DEPTH));
  c.humPrecision = s.get(P_HUM_PRECISION) > 0.5;
  c.humMix = static_cast<float>(s.get(P_HUM_MIX));

  c.noiseOn = s.get(P_NOISE_ON) > 0.5;
  c.noiseAdaptive = s.get(P_NOISE_ADAPTIVE) > 0.5;
  c.noiseReductionDb = static_cast<float>(s.get(P_NOISE_REDUCTION));
  c.noiseTonalDb = static_cast<float>(s.get(P_NOISE_TONAL));
  c.noiseSmoothing = static_cast<float>(s.get(P_NOISE_SMOOTH));
  c.noiseTransient = static_cast<float>(s.get(P_NOISE_TRANSIENT));
  c.noiseFrame = static_cast<int>(s.get(P_NOISE_FRAME) + 0.5);
  c.noiseMix = static_cast<float>(s.get(P_NOISE_MIX));

  c.verbOn = s.get(P_VERB_ON) > 0.5;
  c.verbAmountDb = static_cast<float>(s.get(P_VERB_AMOUNT));
  c.verbTailSec = static_cast<float>(s.get(P_VERB_TAIL));
  c.verbPredelayMs = static_cast<float>(s.get(P_VERB_PREDELAY));
  c.verbTransient = static_cast<float>(s.get(P_VERB_TRANSIENT));
  c.verbTiltDb = static_cast<float>(s.get(P_VERB_TILT));
  c.verbMix = static_cast<float>(s.get(P_VERB_MIX));

  c.specOn = s.get(P_SPEC_ON) > 0.5;
  c.specOp = static_cast<int>(s.get(P_SPEC_OP) + 0.5);
  c.specAmountDb = static_cast<float>(s.get(P_SPEC_AMOUNT));
  c.specPreserve = s.get(P_SPEC_PRESERVE) > 0.5;
  c.specMix = static_cast<float>(s.get(P_SPEC_MIX));

  c.captureArm = s.captureActive.load(std::memory_order_relaxed);
  c.captureFreeze = s.captureFreeze.load(std::memory_order_relaxed);
  c.clearCapture = s.clearCapture.load(std::memory_order_relaxed);
  c.clearEdits = s.clearEdits.load(std::memory_order_relaxed);
  c.noiseLearn = s.noiseLearnActive.load(std::memory_order_relaxed);
  c.compare = s.compare.load(std::memory_order_relaxed);
  return c;
}

} // namespace repair
