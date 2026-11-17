// Repair Suite — de-reverb
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Late-reverberation suppression in the shared STFT.  A delayed power estimate
// is decayed by the tail-length control and subtracted with a spectral floor;
// transient protection keeps direct attacks intact.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <hdsp/db.hpp>

#include "controls.hpp"

namespace repair {

class Dereverb {
public:
  static constexpr int kRing = 16;

  void prepare(float sampleRate, int bins, int hop) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    bins_ = std::max(2, bins);
    hop_ = std::max(1, hop);
    ringPower_.assign(static_cast<size_t>(kRing) * bins_, 0.0f);
    prevPower_.assign(static_cast<size_t>(bins_), 0.0f);
    smoothGain_.assign(static_cast<size_t>(bins_), 1.0f);
    writeIndex_ = 0;
    lastReductionDb_ = 0.0f;
    reset();
  }

  void reset() noexcept {
    std::fill(ringPower_.begin(), ringPower_.end(), 0.0f);
    std::fill(prevPower_.begin(), prevPower_.end(), 0.0f);
    std::fill(smoothGain_.begin(), smoothGain_.end(), 1.0f);
    writeIndex_ = 0;
    lastReductionDb_ = 0.0f;
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    mix_ = std::clamp(c.verbMix * 0.01f, 0.0f, 1.0f);
    delayFrames_ = std::clamp(
        static_cast<int>(c.verbPredelayMs * 0.001f * sr_ / hop_), 0, kRing - 2);
    const float rt60 = std::clamp(c.verbTailSec, 0.1f, 5.0f);
    decay_ = std::pow(10.0f,
                      -3.0f * static_cast<float>(hop_) / (rt60 * sr_));
    beta_ = 1.0f - hdsp::dbToGain(-std::clamp(c.verbAmountDb, 0.0f, 30.0f));
    floorGain_ = hdsp::dbToGain(
        -std::max(6.0f, std::clamp(c.verbAmountDb, 0.0f, 30.0f) * 2.0f));
    transientThreshold_ =
        1.0f + std::max(0.0f, 10.0f - c.verbTransient) * 0.4f;
    tilt_ = std::clamp(c.verbTiltDb, -6.0f, 6.0f);
  }

  float lastReductionDb() const noexcept { return lastReductionDb_; }

  void processBins(float* reL, float* imL, float* reR, float* imR,
                   int bins) noexcept {
    if (bins <= 0 || bins != bins_) {
      return;
    }
    if (!controls_.verbOn || !controls_.power) {
      lastReductionDb_ = 0.0f;
      return;
    }
    const int readIndex = (writeIndex_ - delayFrames_ + kRing) % kRing;
    float reduction = 0.0f;
    for (int k = 0; k < bins; ++k) {
      const float p = power(reL[k], imL[k], reR[k], imR[k]);
      const float delayed =
          ringPower_[static_cast<size_t>(readIndex * bins_ + k)];
      float lambda = decay_ * delayed;
      const float tiltNorm =
          2.0f * static_cast<float>(k) / static_cast<float>(bins_ - 1) - 1.0f;
      lambda *= hdsp::dbToGain(tilt_ * tiltNorm);
      float g = 1.0f - beta_ * lambda / (p + 1.0e-12f);
      g = std::max(g, floorGain_);
      if (p > prevPower_[static_cast<size_t>(k)] * transientThreshold_) {
        g = std::max(g, 0.92f);
      }
      g = 0.55f * smoothGain_[static_cast<size_t>(k)] + 0.45f * g;
      smoothGain_[static_cast<size_t>(k)] = g;
      prevPower_[static_cast<size_t>(k)] = p;
      ringPower_[static_cast<size_t>(writeIndex_ * bins_ + k)] = p;
      const float applied = 1.0f + mix_ * (g - 1.0f);
      reL[k] *= applied;
      imL[k] *= applied;
      reR[k] *= applied;
      imR[k] *= applied;
      reduction += 1.0f - applied;
    }
    writeIndex_ = (writeIndex_ + 1) % kRing;
    lastReductionDb_ =
        -hdsp::gainToDb(std::max(1.0e-6f, 1.0f - reduction / bins_));
  }

private:
  static float power(float reL, float imL, float reR, float imR) noexcept {
    return 0.5f * (reL * reL + imL * imL + reR * reR + imR * imR);
  }

  float sr_ = 48000.0f;
  int bins_ = 1025;
  int hop_ = 512;
  Controls controls_;
  std::vector<float> ringPower_, prevPower_, smoothGain_;
  int writeIndex_ = 0;
  int delayFrames_ = 2;
  float decay_ = 0.99f;
  float beta_ = 0.5f;
  float floorGain_ = 0.1f;
  float transientThreshold_ = 3.0f;
  float tilt_ = 0.0f;
  float mix_ = 1.0f;
  float lastReductionDb_ = 0.0f;
};

} // namespace repair
