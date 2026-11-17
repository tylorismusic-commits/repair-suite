// Repair Suite — de-clip
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Three reconstruction paths:
//   Fast     — cubic Hermite across each flat-top run.
//   Balanced — cubic initialisation plus one clipping-consistent spectral pass.
//   High     — iterative clipping-consistent STFT shrinkage (default).
//
// Block processing: a 1024-sample block is reconstructed with 512 samples of
// context on each side, so the module reports 1536 samples of latency.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <hdsp/db.hpp>

#include "controls.hpp"
#include "fft.hpp"

namespace repair {

class Declip {
public:
  static constexpr int kBlock = 1024;
  static constexpr int kContext = 512;
  static constexpr int kWindow = kBlock + 2 * kContext;  // 2048
  static constexpr int kFftSize = 512;
  static constexpr int kHop = 128;
  static constexpr int kHistory = 4096;

  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    workL_.assign(kWindow, 0.0f);
    workR_.assign(kWindow, 0.0f);
    origL_.assign(kWindow, 0.0f);
    origR_.assign(kWindow, 0.0f);
    cubicL_.assign(kWindow, 0.0f);
    cubicR_.assign(kWindow, 0.0f);
    accL_.assign(kWindow, 0.0f);
    accR_.assign(kWindow, 0.0f);
    frameL_.assign(kFftSize, 0.0f);
    frameR_.assign(kFftSize, 0.0f);
    reL_.assign(kFftSize, 0.0f);
    imL_.assign(kFftSize, 0.0f);
    reR_.assign(kFftSize, 0.0f);
    imR_.assign(kFftSize, 0.0f);
    clippedL_.assign(kWindow, 0);
    clippedR_.assign(kWindow, 0);
    histL_.assign(kHistory, 0.0f);
    histR_.assign(kHistory, 0.0f);
    outL_.assign(2 * kBlock, 0.0f);
    outR_.assign(2 * kBlock, 0.0f);
    window_.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
      window_[static_cast<size_t>(i)] =
          0.5f - 0.5f * std::cos(2.0f * 3.14159265358979323846f * i /
                                static_cast<float>(kFftSize));
    }
    fft_.prepare(kFftSize);
    reset();
  }

  void reset() noexcept {
    std::fill(histL_.begin(), histL_.end(), 0.0f);
    std::fill(histR_.begin(), histR_.end(), 0.0f);
    std::fill(outL_.begin(), outL_.end(), 0.0f);
    std::fill(outR_.begin(), outR_.end(), 0.0f);
    histWrite_ = 0;
    samplesSeen_ = 0;
    outRead_ = 0;
    outWrite_ = 0;
    outCount_ = 0;
    clippedSamples_ = 0;
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    mix_ = std::clamp(c.clipMix * 0.01f, 0.0f, 1.0f);
    headroomGain_ = hdsp::dbToGain(-std::max(0.0f, c.clipHeadroomDb));
  }

  int latencySamples() const noexcept { return kBlock + kContext - 1; }
  int clippedSamples() const noexcept { return clippedSamples_; }
  int lastClipCount() const noexcept { return lastClipCount_; }

  void process(float* left, float* right, int frames) noexcept {
    if (!left || !right || frames <= 0) {
      return;
    }
    lastClipCount_ = 0;
    for (int n = 0; n < frames; ++n) {
      histL_[static_cast<size_t>(histWrite_)] = left[n];
      histR_[static_cast<size_t>(histWrite_)] = right[n];
      histWrite_ = (histWrite_ + 1) % kHistory;
      ++samplesSeen_;

      if (samplesSeen_ >= kBlock + kContext &&
          ((samplesSeen_ - (kBlock + kContext)) % kBlock) == 0) {
        processBlock();
      }

      if (outCount_ > 0) {
        const float wetL = outL_[static_cast<size_t>(outRead_)];
        const float wetR = outR_[static_cast<size_t>(outRead_)];
        outRead_ = (outRead_ + 1) % static_cast<int>(outL_.size());
        --outCount_;
        const int64_t dryIndex =
            static_cast<int64_t>(samplesSeen_) - 1 - latencySamples();
        const float dryL = sampleAt(histL_, dryIndex);
        const float dryR = sampleAt(histR_, dryIndex);
        left[n] = mix_ * headroomGain_ * wetL + (1.0f - mix_) * dryL;
        right[n] = mix_ * headroomGain_ * wetR + (1.0f - mix_) * dryR;
      } else {
        left[n] = 0.0f;
        right[n] = 0.0f;
      }
    }
  }

private:
  struct Run {
    int a = 0;
    int b = 0;
    float sign = 1.0f;
  };

  float sampleAt(const std::vector<float>& h, int64_t absolute) const noexcept {
    if (absolute < 0) {
      return 0.0f;
    }
    return h[static_cast<size_t>(absolute % kHistory)];
  }

  void processBlock() noexcept {
    const int64_t blockStart = static_cast<int64_t>(samplesSeen_) -
                               (kBlock + kContext);
    const int64_t contextStart = blockStart - kContext;
    for (int i = 0; i < kWindow; ++i) {
      origL_[static_cast<size_t>(i)] = sampleAt(histL_, contextStart + i);
      origR_[static_cast<size_t>(i)] = sampleAt(histR_, contextStart + i);
    }

    if (controls_.clipAuto) {
      float maxPos = 0.0f;
      float maxNeg = 0.0f;
      for (int i = 0; i < kWindow; ++i) {
        const float l = origL_[static_cast<size_t>(i)];
        const float r = origR_[static_cast<size_t>(i)];
        maxPos = std::max({maxPos, l, r});
        maxNeg = std::min({maxNeg, l, r});
      }
      upper_ = maxPos > 1.0e-4f ? maxPos : 1.0f;
      lower_ = maxNeg < -1.0e-4f ? maxNeg : -1.0f;
    } else {
      upper_ = hdsp::dbToGain(controls_.clipUpperDb);
      lower_ = -hdsp::dbToGain(controls_.clipLowerDb);
    }

    workL_ = origL_;
    workR_ = origR_;
    runsL_.clear();
    runsR_.clear();
    std::fill(clippedL_.begin(), clippedL_.end(), 0);
    std::fill(clippedR_.begin(), clippedR_.end(), 0);
    lastClipCount_ = 0;
    if (controls_.clipOn && controls_.power) {
      detectRuns(origL_, runsL_, clippedL_, upper_, lower_);
      detectRuns(origR_, runsR_, clippedR_, upper_, lower_);
      clippedSamples_ = lastClipCount_;

      for (const Run& r : runsL_) {
        reconstructCubic(workL_, origL_, r);
      }
      for (const Run& r : runsR_) {
        reconstructCubic(workR_, origR_, r);
      }

      if (!runsL_.empty() || !runsR_.empty()) {
        cubicL_ = workL_;
        cubicR_ = workR_;
        if (controls_.clipMode == 0) {
          refine();
        } else if (controls_.clipMode == 1) {
          spectralPass();
          enforceConsistency();
          blendCubic(0.5f);
        }
      }
    }

    const int offset = kContext;
    for (int i = 0; i < kBlock; ++i) {
      outL_[static_cast<size_t>(outWrite_)] = workL_[static_cast<size_t>(offset + i)];
      outR_[static_cast<size_t>(outWrite_)] = workR_[static_cast<size_t>(offset + i)];
      outWrite_ = (outWrite_ + 1) % static_cast<int>(outL_.size());
      outCount_ = std::min(outCount_ + 1, static_cast<int>(outL_.size()));
    }
  }

  void detectRuns(const std::vector<float>& x, std::vector<Run>& runs,
                  std::vector<uint8_t>& mask, float upper,
                  float lower) noexcept {
    const float eps = 1.0e-6f;
    int i = 0;
    while (i < kWindow) {
      const bool pos = x[static_cast<size_t>(i)] >= upper - eps;
      const bool neg = x[static_cast<size_t>(i)] <= lower + eps;
      if (!pos && !neg) {
        ++i;
        continue;
      }
      const int start = i;
      const float sign = pos ? 1.0f : -1.0f;
      while (i < kWindow) {
        if (pos && x[static_cast<size_t>(i)] >= upper - eps) {
          ++i;
        } else if (neg && x[static_cast<size_t>(i)] <= lower + eps) {
          ++i;
        } else {
          break;
        }
      }
      const int end = i - 1;
      if (end > start) {
        Run r;
        r.a = start;
        r.b = end;
        r.sign = sign;
        runs.push_back(r);
        for (int j = start; j <= end; ++j) {
          mask[static_cast<size_t>(j)] = 1;
        }
        lastClipCount_ += end - start + 1;
      }
    }
  }

  void reconstructCubic(std::vector<float>& dst, const std::vector<float>& src,
                        const Run& r) noexcept {
    if (r.a <= 0 || r.b + 1 >= kWindow) {
      return;
    }
    const int a = r.a;
    const int b = r.b;
    const float y0 = src[static_cast<size_t>(a - 1)];
    const float y1 = src[static_cast<size_t>(b + 1)];
    const int i0 = std::max(0, a - 3);
    const int i1 = std::min(kWindow - 1, b + 3);
    const float m0 = (src[static_cast<size_t>(a - 1)] -
                      src[static_cast<size_t>(i0)]) /
                     static_cast<float>(a - i0);
    const float m1 = (src[static_cast<size_t>(i1)] -
                      src[static_cast<size_t>(b + 1)]) /
                     static_cast<float>(i1 - b);
    const float h = static_cast<float>(b + 2 - a);
    for (int j = a; j <= b; ++j) {
      const float t = static_cast<float>(j - (a - 1)) / h;
      const float t2 = t * t;
      const float t3 = t2 * t;
      const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
      const float h10 = t3 - 2.0f * t2 + t;
      const float h01 = -2.0f * t3 + 3.0f * t2;
      const float h11 = t3 - t2;
      float v = h00 * y0 + h10 * h * m0 + h01 * y1 + h11 * h * m1;
      if (!std::isfinite(v)) {
        v = r.sign * upper_;
      }
      dst[static_cast<size_t>(j)] = v;
    }
  }

  void blendCubic(float spectralWeight) noexcept {
    for (const Run& r : runsL_) {
      for (int i = r.a; i <= r.b; ++i) {
        workL_[static_cast<size_t>(i)] =
            spectralWeight * workL_[static_cast<size_t>(i)] +
            (1.0f - spectralWeight) * cubicL_[static_cast<size_t>(i)];
      }
    }
    for (const Run& r : runsR_) {
      for (int i = r.a; i <= r.b; ++i) {
        workR_[static_cast<size_t>(i)] =
            spectralWeight * workR_[static_cast<size_t>(i)] +
            (1.0f - spectralWeight) * cubicR_[static_cast<size_t>(i)];
      }
    }
  }

  void refine() noexcept {
    const int iterations = std::clamp(controls_.clipIterations, 1, 30);
    for (int it = 0; it < iterations; ++it) {
      spectralPass();
      enforceConsistency();
    }
  }

  void spectralPass() noexcept {
    std::fill(accL_.begin(), accL_.end(), 0.0f);
    std::fill(accR_.begin(), accR_.end(), 0.0f);
    for (int start = 0; start + kFftSize <= kWindow; start += kHop) {
      for (int i = 0; i < kFftSize; ++i) {
        frameL_[static_cast<size_t>(i)] =
            workL_[static_cast<size_t>(start + i)] *
            window_[static_cast<size_t>(i)];
        frameR_[static_cast<size_t>(i)] =
            workR_[static_cast<size_t>(start + i)] *
            window_[static_cast<size_t>(i)];
      }
      fft_.forward(frameL_.data(), reL_.data(), imL_.data());
      fft_.forward(frameR_.data(), reR_.data(), imR_.data());
      shrink(reL_.data(), imL_.data());
      shrink(reR_.data(), imR_.data());
      fft_.inverse(reL_.data(), imL_.data());
      fft_.inverse(reR_.data(), imR_.data());
      for (int i = 0; i < kFftSize; ++i) {
        const float w = window_[static_cast<size_t>(i)] / 1.5f;
        accL_[static_cast<size_t>(start + i)] +=
            reL_[static_cast<size_t>(i)] * w;
        accR_[static_cast<size_t>(start + i)] +=
            reR_[static_cast<size_t>(i)] * w;
      }
    }
    workL_ = accL_;
    workR_ = accR_;
  }

  void shrink(float* re, float* im) const noexcept {
    float maxMag = 0.0f;
    for (int k = 0; k < kFftSize / 2 + 1; ++k) {
      const float m = std::sqrt(re[k] * re[k] + im[k] * im[k]);
      maxMag = std::max(maxMag, m);
    }
    if (maxMag <= 1.0e-12f) {
      return;
    }
    const float threshold = 0.05f * maxMag;
    for (int k = 0; k <= kFftSize / 2; ++k) {
      const int mirror = (k == 0 || k == kFftSize / 2) ? k : kFftSize - k;
      const float m = std::sqrt(re[k] * re[k] + im[k] * im[k]);
      const float scale = m > threshold ? 1.0f : 0.0f;
      re[k] *= scale;
      im[k] *= scale;
      if (mirror != k) {
        re[mirror] = re[k];
        im[mirror] = -im[k];
      }
    }
    im[0] = 0.0f;
    im[kFftSize / 2] = 0.0f;
  }

  void enforceConsistency() noexcept {
    for (int i = 0; i < kWindow; ++i) {
      if (!clippedL_[static_cast<size_t>(i)]) {
        workL_[static_cast<size_t>(i)] = origL_[static_cast<size_t>(i)];
      }
      if (!clippedR_[static_cast<size_t>(i)]) {
        workR_[static_cast<size_t>(i)] = origR_[static_cast<size_t>(i)];
      }
    }
    for (const Run& r : runsL_) {
      for (int i = r.a; i <= r.b; ++i) {
        workL_[static_cast<size_t>(i)] = enforceOne(workL_[static_cast<size_t>(i)],
                                                   r.sign);
      }
    }
    for (const Run& r : runsR_) {
      for (int i = r.a; i <= r.b; ++i) {
        workR_[static_cast<size_t>(i)] = enforceOne(workR_[static_cast<size_t>(i)],
                                                   r.sign);
      }
    }
  }

  float enforceOne(float v, float sign) const noexcept {
    if (!std::isfinite(v)) {
      v = sign * upper_;
    }
    const float limit = upper_ * 4.0f;
    if (sign > 0.0f) {
      return std::clamp(v, upper_, limit);
    }
    return std::clamp(v, -limit, lower_);
  }

  float sr_ = 48000.0f;
  Controls controls_;
  Fft fft_;
  std::vector<float> window_;
  std::vector<float> workL_, workR_, origL_, origR_, accL_, accR_;
  std::vector<float> cubicL_, cubicR_;
  std::vector<float> frameL_, frameR_, reL_, imL_, reR_, imR_;
  std::vector<uint8_t> clippedL_, clippedR_;
  std::vector<Run> runsL_, runsR_;
  std::vector<float> histL_, histR_, outL_, outR_;
  int histWrite_ = 0;
  uint64_t samplesSeen_ = 0;
  int outRead_ = 0;
  int outWrite_ = 0;
  int outCount_ = 0;
  float mix_ = 1.0f;
  float headroomGain_ = 1.0f;
  int clippedSamples_ = 0;
  int lastClipCount_ = 0;
  float upper_ = 1.0f;
  float lower_ = -1.0f;
};

} // namespace repair
