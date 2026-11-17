// Repair Suite — spectral de-noise
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Operates on the shared STFT's non-negative bins.  Learn captures an average
// noise power spectrum; the reduction stage uses a smoothed spectral-subtraction
// gain with a transient hold and a stereo-linked (single-gain) decision.
#pragma once

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <vector>

#include <hdsp/db.hpp>

#include "controls.hpp"

namespace repair {

class Denoise {
public:
  void prepare(float sampleRate, int bins, int hop) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    bins_ = std::max(2, bins);
    hop_ = std::max(1, hop);
    noise_.assign(static_cast<size_t>(bins_), 0.0f);
    smoothGain_.assign(static_cast<size_t>(bins_), 1.0f);
    prevPower_.assign(static_cast<size_t>(bins_), 0.0f);
    learnFrames_ = 0;
    learning_ = false;
    learned_ = false;
    lastReductionDb_ = 0.0f;
    reset();
  }

  void retune(float sampleRate, int bins, int hop) noexcept {
    std::vector<float> old = noise_;
    const int oldBins = bins_;
    const bool wasLearned = learned_;
    prepare(sampleRate, bins, hop);
    if (wasLearned && oldBins > 1 && bins_ > 1) {
      for (int k = 0; k < bins_; ++k) {
        const float pos = static_cast<float>(k) *
                          static_cast<float>(oldBins - 1) /
                          static_cast<float>(bins_ - 1);
        const int i0 = std::min(static_cast<int>(pos), oldBins - 2);
        const int i1 = i0 + 1;
        const float t = pos - static_cast<float>(i0);
        noise_[static_cast<size_t>(k)] =
            (1.0f - t) * old[static_cast<size_t>(i0)] +
            t * old[static_cast<size_t>(i1)];
      }
      learned_ = true;
      profileReady_.store(true, std::memory_order_release);
    }
  }

  void reset() noexcept {
    std::fill(noise_.begin(), noise_.end(), 0.0f);
    std::fill(smoothGain_.begin(), smoothGain_.end(), 1.0f);
    std::fill(prevPower_.begin(), prevPower_.end(), 0.0f);
    learnFrames_ = 0;
    adaptiveFrames_ = 0;
    learning_ = false;
    learned_ = false;
    learnControl_ = false;
    lastReductionDb_ = 0.0f;
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    mix_ = std::clamp(c.noiseMix * 0.01f, 0.0f, 1.0f);
    floorGain_ = hdsp::dbToGain(-std::clamp(c.noiseReductionDb, 0.0f, 60.0f));
    tonalFloorGain_ = hdsp::dbToGain(-std::clamp(c.noiseTonalDb, 0.0f, 60.0f));
    smoothCoeff_ = std::clamp(c.noiseSmoothing * 0.09f, 0.0f, 0.9f);
    transientThreshold_ =
        1.0f + std::max(0.0f, 10.0f - c.noiseTransient) * 0.5f;
    learnLimit_ = std::max(1, static_cast<int>(2.0f * sr_ / hop_));

    const bool wantLearn = c.noiseLearn && c.noiseOn;
    if (wantLearn && !learnControl_) {
      std::fill(noise_.begin(), noise_.end(), 0.0f);
      learnFrames_ = 0;
      learned_ = false;
      learning_ = true;
    } else if (!wantLearn && learnControl_) {
      learned_ = learned_ || learnFrames_ > 0;
      learning_ = false;
      if (learned_) {
        profileReady_.store(true, std::memory_order_release);
      }
    }
    learnControl_ = wantLearn;
  }

  bool learning() const noexcept { return learning_; }
  bool learned() const noexcept { return learned_; }
  int learnFrames() const noexcept { return learnFrames_; }
  int profileBins() const noexcept { return bins_; }
  float lastReductionDb() const noexcept { return lastReductionDb_; }

  // Called by the host glue when learning completes, so the learned profile
  // can be published to Shared and saved with the session.
  bool takeProfileReady() noexcept {
    return profileReady_.exchange(false, std::memory_order_acq_rel);
  }

  int copyProfile(float* out, int maxBins) const noexcept {
    const int n = std::min(bins_, maxBins);
    for (int i = 0; i < n; ++i) {
      out[i] = noise_[static_cast<size_t>(i)];
    }
    return n;
  }

  bool loadProfile(const float* data, int bins) noexcept {
    if (!data || bins != bins_) {
      return false;
    }
    for (int i = 0; i < bins_; ++i) {
      noise_[static_cast<size_t>(i)] = std::max(0.0f, data[i]);
    }
    learned_ = true;
    learning_ = false;
    learnFrames_ = learnLimit_;
    learnControl_ = false;
    profileReady_.store(false, std::memory_order_relaxed);
    return true;
  }

  void processBins(float* reL, float* imL, float* reR, float* imR,
                   int bins) noexcept {
    if (bins <= 0 || bins != bins_) {
      return;
    }
    float reduction = 0.0f;
    if (learning_) {
      for (int k = 0; k < bins; ++k) {
        const float p = power(reL[k], imL[k], reR[k], imR[k]);
        noise_[static_cast<size_t>(k)] +=
            (p - noise_[static_cast<size_t>(k)]) /
            static_cast<float>(learnFrames_ + 1);
      }
      ++learnFrames_;
      if (learnFrames_ >= learnLimit_) {
        learning_ = false;
        learned_ = true;
        profileReady_.store(true, std::memory_order_release);
      }
      return;  // monitor the noise while learning
    }

    if (!controls_.noiseOn || !controls_.power) {
      lastReductionDb_ = 0.0f;
      return;
    }
    if (!learned_ && controls_.noiseAdaptive && !learning_) {
      // Bootstrap an adaptive profile from the minimum power seen over the
      // first second, then start reducing.
      for (int k = 0; k < bins; ++k) {
        const float p = power(reL[k], imL[k], reR[k], imR[k]);
        if (adaptiveFrames_ == 0) {
          noise_[static_cast<size_t>(k)] = p;
        } else {
          noise_[static_cast<size_t>(k)] =
              std::min(noise_[static_cast<size_t>(k)], p);
        }
      }
      ++adaptiveFrames_;
      if (adaptiveFrames_ >= std::max(1, static_cast<int>(sr_ / hop_))) {
        learned_ = true;
        profileReady_.store(true, std::memory_order_release);
      }
      lastReductionDb_ = 0.0f;
      return;
    }
    if (!learned_) {
      lastReductionDb_ = 0.0f;
      return;
    }

    for (int k = 0; k < bins; ++k) {
      const float p = power(reL[k], imL[k], reR[k], imR[k]);
      const float n = noise_[static_cast<size_t>(k)] + 1.0e-12f;
      float floor = floorGain_;
      if (controls_.noiseTonalDb > 0.1f) {
        float neighbour = 0.0f;
        int count = 0;
        for (int j = std::max(1, k - 3); j <= std::min(bins - 2, k + 3); ++j) {
          if (j != k) {
            neighbour += noise_[static_cast<size_t>(j)];
            ++count;
          }
        }
        if (count > 0 && n > 2.0f * neighbour / count) {
          floor = std::min(floor, tonalFloorGain_);
        }
      }
      float g = 1.0f - 1.5f * n / (p + 1.0e-12f);
      g = std::max(g, floor);
      // Transient hold: do not gate a bin that just jumped.
      if (p > prevPower_[static_cast<size_t>(k)] * transientThreshold_) {
        g = std::max(g, 0.9f);
      }
      g = smoothCoeff_ * smoothGain_[static_cast<size_t>(k)] +
          (1.0f - smoothCoeff_) * g;
      smoothGain_[static_cast<size_t>(k)] = g;
      prevPower_[static_cast<size_t>(k)] = p;
      if (controls_.noiseAdaptive && p < n * 1.5f) {
        noise_[static_cast<size_t>(k)] =
            0.999f * noise_[static_cast<size_t>(k)] + 0.001f * p;
      }
      const float applied = 1.0f + mix_ * (g - 1.0f);
      reL[k] *= applied;
      imL[k] *= applied;
      reR[k] *= applied;
      imR[k] *= applied;
      reduction += 1.0f - applied;
    }
    lastReductionDb_ =
        -hdsp::gainToDb(std::max(1.0e-6f, 1.0f - reduction / bins));
  }

private:
  static float power(float reL, float imL, float reR, float imR) noexcept {
    const float l = reL * reL + imL * imL;
    const float r = reR * reR + imR * imR;
    return 0.5f * (l + r);
  }

  float sr_ = 48000.0f;
  int bins_ = 1025;
  int hop_ = 512;
  Controls controls_;
  std::vector<float> noise_, smoothGain_, prevPower_;
  int learnFrames_ = 0;
  int adaptiveFrames_ = 0;
  int learnLimit_ = 187;
  bool learning_ = false;
  bool learned_ = false;
  bool learnControl_ = false;
  std::atomic<bool> profileReady_{false};
  float mix_ = 1.0f;
  float floorGain_ = 0.1f;
  float tonalFloorGain_ = 0.25f;
  float smoothCoeff_ = 0.45f;
  float transientThreshold_ = 4.0f;
  float lastReductionDb_ = 0.0f;
};

} // namespace repair
