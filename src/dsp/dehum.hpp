// Repair Suite — de-hum
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Default engine: a cascade of narrow RBJ notches at the fundamental and its
// harmonics, with an overall depth blend.  Auto tune searches the 50/60 Hz
// regions and scores the harmonic series.
//
// Precision mode uses an adaptive harmonic canceller (one complex LMS weight
// pair per harmonic) rather than the notch cascade.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "controls.hpp"

namespace repair {

class Dehum {
public:
  static constexpr int kMaxHarmonics = 96;
  static constexpr int kAnalysis = 4096;

  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    for (int ch = 0; ch < 2; ++ch) {
      for (auto& n : notch_[ch]) {
        n.reset();
      }
      for (auto& h : harmonic_[ch]) {
        h.reset();
      }
    }
    anaBuf_.assign(kAnalysis, 0.0f);
    anaWrite_ = 0;
    anaCount_ = 0;
    anaTick_ = 0;
    autoHz_ = 50.0f;
    autoActive_ = false;
    activeHarmonics_ = 0;
    rebuild();
    reset();
  }

  void reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
      for (auto& n : notch_[ch]) {
        n.reset();
      }
      for (auto& h : harmonic_[ch]) {
        h.reset();
      }
    }
    std::fill(anaBuf_.begin(), anaBuf_.end(), 0.0f);
    anaWrite_ = 0;
    anaCount_ = 0;
    anaTick_ = 0;
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    mix_ = std::clamp(c.humMix * 0.01f, 0.0f, 1.0f);
    depth_ = 1.0f - std::pow(10.0f, -std::max(0.0f, c.humDepthDb) / 20.0f);
    if (!c.humAuto) {
      autoActive_ = true;
    }
    harmonics_ = std::clamp(c.humHarmonics, 1, kMaxHarmonics);
    oddOnly_ = c.humOdd;
    const float target = c.humAuto ? autoHz_ : c.humHz;
    if (std::fabs(target - currentHz_) > 0.01f ||
        harmonics_ != activeHarmonics_ || c.humOdd != activeOdd_ ||
        c.humPrecision != precision_) {
      precision_ = c.humPrecision;
      rebuild();
    }
  }

  int latencySamples() const noexcept { return 0; }
  float detectedHz() const noexcept { return currentHz_; }

  void process(float* left, float* right, int frames) noexcept {
    if (!left || !right || frames <= 0) {
      return;
    }
    for (int i = 0; i < frames; ++i) {
      const float l = left[i];
      const float r = right[i];
      anaBuf_[static_cast<size_t>(anaWrite_)] = 0.5f * (l + r);
      anaWrite_ = (anaWrite_ + 1) % kAnalysis;
      anaCount_ = std::min(anaCount_ + 1, kAnalysis);
      if (controls_.humAuto && ++anaTick_ >= static_cast<int>(sr_)) {
        anaTick_ = 0;
        if (anaCount_ >= kAnalysis) {
          updateAuto();
        }
      }

      if (!controls_.humOn || !controls_.power || activeHarmonics_ == 0 ||
          (controls_.humAuto && !autoActive_)) {
        continue;
      }
      float wl = l;
      float wr = r;
      if (precision_) {
        for (int k = 0; k < activeHarmonics_; ++k) {
          wl = harmonic_[0][static_cast<size_t>(k)].process(
              wl, freq_[static_cast<size_t>(k)], sr_);
          wr = harmonic_[1][static_cast<size_t>(k)].process(
              wr, freq_[static_cast<size_t>(k)], sr_);
        }
      } else {
        for (int k = 0; k < activeHarmonics_; ++k) {
          wl = notch_[0][static_cast<size_t>(k)].process(wl);
          wr = notch_[1][static_cast<size_t>(k)].process(wr);
        }
      }
      left[i] = l + mix_ * depth_ * (wl - l);
      right[i] = r + mix_ * depth_ * (wr - r);
    }
  }

private:
  struct Notch {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    void set(float freq, float q, float sampleRate) noexcept {
      const float w = 2.0f * 3.14159265358979323846f * freq / sampleRate;
      const float cw = std::cos(w);
      const float alpha = std::sin(w) / (2.0f * std::max(q, 0.1f));
      const float a0 = 1.0f + alpha;
      b0 = 1.0f / a0;
      b1 = -2.0f * cw / a0;
      b2 = 1.0f / a0;
      a1 = -2.0f * cw / a0;
      a2 = (1.0f - alpha) / a0;
    }

    float process(float x) noexcept {
      const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = x;
      y2 = y1;
      y1 = y;
      return y;
    }
  };

  struct PrecisionHarmonic {
    float phase = 0.0f;
    float wCos = 0.0f;
    float wSin = 0.0f;

    void reset() noexcept {
      phase = 0.0f;
      wCos = 0.0f;
      wSin = 0.0f;
    }

    float process(float x, float freq, float sr) noexcept {
      const float c = std::cos(phase);
      const float s = std::sin(phase);
      const float est = wCos * c + wSin * s;
      const float e = x - est;
      constexpr float mu = 0.004f;
      wCos += mu * e * c;
      wSin += mu * e * s;
      phase += 2.0f * 3.14159265358979323846f * freq / sr;
      if (phase >= 2.0f * 3.14159265358979323846f) {
        phase -= 2.0f * 3.14159265358979323846f;
      }
      return e;
    }
  };

  void rebuild() noexcept {
    currentHz_ = controls_.humAuto ? autoHz_ : controls_.humHz;
    currentHz_ = std::clamp(currentHz_, 20.0f, 200.0f);
    activeHarmonics_ = 0;
    const float nyquist = sr_ * 0.49f;
    for (int k = 1; k <= harmonics_; ++k) {
      if (oddOnly_ && (k % 2) == 0) {
        continue;
      }
      const float f = currentHz_ * static_cast<float>(k);
      if (f >= nyquist) {
        break;
      }
      const float q = std::max(8.0f, 30.0f - 0.15f * static_cast<float>(k));
      for (int ch = 0; ch < 2; ++ch) {
        notch_[ch][static_cast<size_t>(activeHarmonics_)].set(f, q, sr_);
        harmonic_[ch][static_cast<size_t>(activeHarmonics_)].reset();
      }
      freq_[static_cast<size_t>(activeHarmonics_)] = f;
      ++activeHarmonics_;
    }
    activeOdd_ = oddOnly_;
  }

  void updateAuto() noexcept {
    // Score a harmonic series for 50 Hz and 60 Hz regions.  The search is
    // deliberately coarse and slow (once per second); it is a convenience,
    // not a pitch tracker.
    auto score = [this](float f0) {
      const float nyquist = sr_ * 0.49f;
      double sum = 0.0;
      for (int k = 1; k <= 5; ++k) {
        const float f = f0 * static_cast<float>(k);
        if (f >= nyquist) {
          break;
        }
        double re = 0.0;
        double im = 0.0;
        const int start = (anaWrite_ - anaCount_ + kAnalysis) % kAnalysis;
        for (int i = 0; i < anaCount_; ++i) {
          const int at = (start + i) % kAnalysis;
          const float w = 0.5f - 0.5f * std::cos(
              2.0f * 3.14159265358979323846f * i / (anaCount_ - 1));
          const double v = anaBuf_[static_cast<size_t>(at)] * w;
          const double phase =
              2.0 * 3.14159265358979323846 * f * i / sr_;
          re += v * std::cos(phase);
          im -= v * std::sin(phase);
        }
        sum += std::sqrt(re * re + im * im);
      }
      return sum;
    };

    float bestHz = autoHz_;
    double best = -1.0;
    for (float base : {50.0f, 60.0f}) {
      for (int step = -8; step <= 8; ++step) {
        const float f = base + 0.25f * static_cast<float>(step);
        const double s = score(f);
        if (s > best) {
          best = s;
          bestHz = f;
        }
      }
    }
    double rms = 0.0;
    const int start = (anaWrite_ - anaCount_ + kAnalysis) % kAnalysis;
    for (int i = 0; i < anaCount_; ++i) {
      const float v = anaBuf_[static_cast<size_t>((start + i) % kAnalysis)];
      rms += static_cast<double>(v) * v;
    }
    rms = std::sqrt(rms / std::max(1, anaCount_));
    const bool present = best > rms * static_cast<double>(anaCount_) * 0.05;
    autoActive_ = present;
    if (present && bestHz != autoHz_) {
      autoHz_ = bestHz;
      rebuild();
    }
  }

  float sr_ = 48000.0f;
  Controls controls_;
  Notch notch_[2][kMaxHarmonics];
  PrecisionHarmonic harmonic_[2][kMaxHarmonics];
  float freq_[kMaxHarmonics] = {};
  std::vector<float> anaBuf_;
  int anaWrite_ = 0;
  int anaCount_ = 0;
  int anaTick_ = 0;
  float autoHz_ = 50.0f;
  float currentHz_ = 0.0f;
  int harmonics_ = 12;
  int activeHarmonics_ = 0;
  bool oddOnly_ = false;
  bool activeOdd_ = false;
  bool precision_ = false;
  bool autoActive_ = false;
  float mix_ = 1.0f;
  float depth_ = 1.0f;
};

} // namespace repair
