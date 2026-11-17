// Repair Suite — factory presets
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <initializer_list>
#include <vector>

#include "params.hpp"

namespace repair {

struct Preset {
  const char* name = "";
  const char* category = "";
  const char* blurb = "";
  std::array<float, P_COUNT> values{};
};

struct PresetValue {
  int param = -1;
  float value = 0.0f;
};

inline Preset makePreset(const char* name, const char* category,
                         const char* blurb,
                         std::initializer_list<PresetValue> overrides) {
  Preset p;
  p.name = name;
  p.category = category;
  p.blurb = blurb;
  for (uint32_t i = 0; i < P_COUNT; ++i) {
    p.values[i] = static_cast<float>(kParamSpecs[i].def);
  }
  for (const PresetValue& v : overrides) {
    if (v.param >= 0 && v.param < static_cast<int>(P_COUNT)) {
      p.values[v.param] = v.value;
    }
  }
  return p;
}

#define V(p, x) PresetValue { static_cast<int>(p), static_cast<float>(x) }

inline const std::vector<Preset>& factoryPresets() {
  static const std::vector<Preset> presets = {
      makePreset("Vinyl Gentle", "Vinyl",
                 "Light crackle removal, 20 Hz rumble filter and a soft noise "
                 "floor.",
                 {V(P_CLICK_MODE, 1), V(P_CLICK_SENS, 4), V(P_CLICK_WIDTH, 3),
                  V(P_HUM_ON, 1), V(P_HUM_AUTO, 1), V(P_NOISE_ON, 1),
                  V(P_NOISE_REDUCTION, 6), V(P_DC_ON, 1), V(P_DC_FREQ, 20)}),
      makePreset("78 Transfer", "Vinyl",
                 "Dense crackle, broad noise reduction and a narrow band-limit "
                 "starting point.",
                 {V(P_CLICK_MODE, 1), V(P_CLICK_SENS, 7), V(P_CLICK_WIDTH, 2),
                  V(P_HUM_ON, 1), V(P_HUM_HARMONICS, 20), V(P_HUM_DEPTH, 36),
                  V(P_NOISE_ON, 1), V(P_NOISE_REDUCTION, 18),
                  V(P_NOISE_TONAL, 20), V(P_NOISE_TRANSIENT, 7),
                  V(P_DC_FREQ, 30)}),
      makePreset("Dialogue Hum", "Dialogue",
                 "50/60 Hz hum and room tone with a gentle declick pass.",
                 {V(P_HUM_ON, 1), V(P_HUM_AUTO, 1), V(P_HUM_HARMONICS, 16),
                  V(P_HUM_DEPTH, 42), V(P_CLICK_ON, 1), V(P_CLICK_SENS, 4),
                  V(P_NOISE_ON, 1), V(P_NOISE_REDUCTION, 10),
                  V(P_NOISE_TRANSIENT, 6), V(P_VERB_ON, 0)}),
      makePreset("Podcast Hiss", "Podcast",
                 "Steady hiss and camera noise; transient protection keeps "
                 "consonants intact.",
                 {V(P_NOISE_ON, 1), V(P_NOISE_REDUCTION, 14),
                  V(P_NOISE_TONAL, 12), V(P_NOISE_TRANSIENT, 7),
                  V(P_CLICK_SENS, 3), V(P_CLIP_ON, 1), V(P_VERB_ON, 0)}),
      makePreset("Clip Rescue", "Utility",
                 "High-quality clipping reconstruction with peak headroom.",
                 {V(P_CLIP_ON, 1), V(P_CLIP_MODE, 0), V(P_CLIP_AUTO, 1),
                  V(P_CLIP_ITER, 14), V(P_CLIP_HEADROOM, 3),
                  V(P_CLIP_MIX, 100), V(P_CLICK_ON, 1), V(P_CLICK_SENS, 5)}),
      makePreset("Room Tamer", "Space",
                 "Reduce late room sound without softening attacks.",
                 {V(P_VERB_ON, 1), V(P_VERB_AMOUNT, 7), V(P_VERB_TAIL, 0.7),
                  V(P_VERB_PREDELAY, 30), V(P_VERB_TRANSIENT, 7),
                  V(P_NOISE_ON, 1), V(P_NOISE_REDUCTION, 6)}),
      makePreset("Clip + Noise", "Utility",
                 "For overdriven location recordings: declip first, then "
                 "broadband cleanup.",
                 {V(P_CLIP_ON, 1), V(P_CLIP_AUTO, 1), V(P_CLIP_ITER, 16),
                  V(P_CLIP_HEADROOM, 4), V(P_CLICK_ON, 1), V(P_CLICK_SENS, 5),
                  V(P_NOISE_ON, 1), V(P_NOISE_REDUCTION, 12),
                  V(P_VERB_ON, 0)}),
      makePreset("Low Latency", "Utility",
                 "Fast declip and low-latency denoise for live use.",
                 {V(P_CLIP_MODE, 2), V(P_CLICK_SENS, 5), V(P_NOISE_ON, 1),
                  V(P_NOISE_REDUCTION, 8), V(P_NOISE_FRAME, 0),
                  V(P_VERB_ON, 0), V(P_HUM_ON, 1), V(P_HUM_PRECISION, 0)}),
  };
  return presets;
}

} // namespace repair
