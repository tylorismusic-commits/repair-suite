// Repair Suite — de-click
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Detection is a median-residual detector: a short median is a robust local
// predictor, and a click shows up as a large residual against it.  Repair is
// cubic Hermite interpolation across the detected run, with stereo-linked
// detection by default.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "controls.hpp"

namespace repair {

class Declick {
public:
  struct Event {
    int offsetSamples = 0;  // relative to the start of the output block
    float widthMs = 0.0f;
    float peakDb = -120.0f;
  };

  static constexpr int kBlock = 512;
  static constexpr int kContext = 128;
  static constexpr int kWindow = kBlock + 2 * kContext;  // 768
  static constexpr int kHistory = 2048;

  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    origL_.assign(kWindow, 0.0f);
    origR_.assign(kWindow, 0.0f);
    workL_.assign(kWindow, 0.0f);
    workR_.assign(kWindow, 0.0f);
    resL_.assign(kWindow, 0.0f);
    resR_.assign(kWindow, 0.0f);
    absRes_.assign(kWindow, 0.0f);
    sorted_.assign(kWindow, 0.0f);
    lpBuf_.assign(kWindow, 0.0f);
    maskL_.assign(kWindow, 0);
    maskR_.assign(kWindow, 0);
    maskC_.assign(kWindow, 0);
    histL_.assign(kHistory, 0.0f);
    histR_.assign(kHistory, 0.0f);
    outL_.assign(2 * kBlock, 0.0f);
    outR_.assign(2 * kBlock, 0.0f);
    runsL_.reserve(kWindow / 2);
    runsR_.reserve(kWindow / 2);
    runsC_.reserve(kWindow / 2);
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
    clicks_ = 0;
    totalClicks_ = 0;
    events_.clear();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    mix_ = std::clamp(c.clickMix * 0.01f, 0.0f, 1.0f);
    maxWidth_ = std::clamp(
        static_cast<int>(c.clickWidthMs * 0.001f * sr_), 1, kWindow - 8);
    if (c.clickMode == 1) {  // Crackle: shorter events
      maxWidth_ = std::clamp(maxWidth_, 1, std::max(1, kWindow / 8));
    }
  }

  int latencySamples() const noexcept { return kBlock + kContext - 1; }
  int clickCount() const noexcept { return totalClicks_; }
  int lastClicks() const noexcept { return clicks_; }
  const std::vector<Event>& lastEvents() const noexcept { return events_; }

  void process(float* left, float* right, int frames) noexcept {
    if (!left || !right || frames <= 0) {
      return;
    }
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
        left[n] = mix_ * wetL + (1.0f - mix_) * dryL;
        right[n] = mix_ * wetR + (1.0f - mix_) * dryR;
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
  };

  float sampleAt(const std::vector<float>& h, int64_t absolute) const noexcept {
    if (absolute < 0) {
      return 0.0f;
    }
    return h[static_cast<size_t>(absolute % kHistory)];
  }

  void processBlock() noexcept {
    const int64_t blockStart =
        static_cast<int64_t>(samplesSeen_) - (kBlock + kContext);
    const int64_t contextStart = blockStart - kContext;
    for (int i = 0; i < kWindow; ++i) {
      origL_[static_cast<size_t>(i)] = sampleAt(histL_, contextStart + i);
      origR_[static_cast<size_t>(i)] = sampleAt(histR_, contextStart + i);
    }

    workL_ = origL_;
    workR_ = origR_;
    clicks_ = 0;
    events_.clear();
    std::fill(maskL_.begin(), maskL_.end(), 0);
    std::fill(maskR_.begin(), maskR_.end(), 0);

    if (controls_.clickOn && controls_.power) {
      runsL_.clear();
      runsR_.clear();
      detect(origL_, resL_, maskL_, runsL_, 0);
      detect(origR_, resR_, maskR_, runsR_, 1);

      if (controls_.clickLink) {
        std::fill(maskC_.begin(), maskC_.end(), 0);
        for (int i = 0; i < kWindow; ++i) {
          maskC_[static_cast<size_t>(i)] =
              static_cast<uint8_t>(maskL_[static_cast<size_t>(i)] |
                                   maskR_[static_cast<size_t>(i)]);
        }
        runsC_.clear();
        maskToRuns(maskC_, runsC_);
        for (const Run& r : runsC_) {
          reconstruct(workL_, origL_, r);
          reconstruct(workR_, origR_, r);
          recordEvent(r, &origL_, &origR_);
        }
        clicks_ = static_cast<int>(runsC_.size());
        totalClicks_ += clicks_;
      } else {
        for (const Run& r : runsL_) {
          reconstruct(workL_, origL_, r);
          recordEvent(r, &origL_, nullptr);
        }
        for (const Run& r : runsR_) {
          reconstruct(workR_, origR_, r);
          recordEvent(r, nullptr, &origR_);
        }
        clicks_ = static_cast<int>(runsL_.size() + runsR_.size());
        totalClicks_ += clicks_;
      }
    }

    const int offset = kContext;
    for (int i = 0; i < kBlock; ++i) {
      outL_[static_cast<size_t>(outWrite_)] =
          workL_[static_cast<size_t>(offset + i)];
      outR_[static_cast<size_t>(outWrite_)] =
          workR_[static_cast<size_t>(offset + i)];
      outWrite_ = (outWrite_ + 1) % static_cast<int>(outL_.size());
      outCount_ = std::min(outCount_ + 1, static_cast<int>(outL_.size()));
    }
  }

  void recordEvent(const Run& r, const std::vector<float>* l,
                   const std::vector<float>* right) noexcept {
    if (events_.size() >= 8) {
      return;
    }
    float peak = 0.0f;
    for (int i = r.a; i <= r.b; ++i) {
      if (l) {
        peak = std::max(peak, std::fabs((*l)[static_cast<size_t>(i)]));
      }
      if (right) {
        peak = std::max(peak, std::fabs((*right)[static_cast<size_t>(i)]));
      }
    }
    Event e;
    e.offsetSamples = r.a - kContext;
    e.widthMs = static_cast<float>(r.b - r.a + 1) * 1000.0f / sr_;
    e.peakDb = 20.0f * std::log10(std::max(1.0e-6f, peak));
    events_.push_back(e);
  }

  void detect(const std::vector<float>& x, std::vector<float>& residual,
              std::vector<uint8_t>& mask, std::vector<Run>& runs,
              int channel) noexcept {
    if (controls_.clickMode == 3) {
      detectDropouts(x, mask, runs);
      return;
    }
    if (controls_.clickMode == 2) {
      detectThumps(x, mask, runs, channel);
      return;
    }
    const std::vector<float>& sig = x;
    // Median of five samples, with edge clamping.
    for (int i = 0; i < kWindow; ++i) {
      float v[5];
      for (int k = -2; k <= 2; ++k) {
        const int at = std::clamp(i + k, 0, kWindow - 1);
        v[k + 2] = sig[static_cast<size_t>(at)];
      }
      std::sort(v, v + 5);
      residual[static_cast<size_t>(i)] =
          sig[static_cast<size_t>(i)] - v[2];
    }
    double sumSq = 0.0;
    float peakAbs = 0.0f;
    for (int i = 0; i < kWindow; ++i) {
      const float av = std::fabs(sig[static_cast<size_t>(i)]);
      peakAbs = std::max(peakAbs, av);
      sumSq += static_cast<double>(av) * av;
      absRes_[static_cast<size_t>(i)] =
          std::fabs(residual[static_cast<size_t>(i)]);
    }
    std::copy(absRes_.begin(), absRes_.end(), sorted_.begin());
    std::nth_element(sorted_.begin(), sorted_.begin() + sorted_.size() / 2,
                     sorted_.end());
    const float sigma =
        std::max(1.0e-6f, sorted_[sorted_.size() / 2] * 1.4826f);
    const float rms = static_cast<float>(std::sqrt(sumSq / kWindow));
    const float sens = std::clamp(controls_.clickSensitivity, 0.0f, 10.0f);
    const float factor = std::clamp(8.0f - 0.7f * sens, 0.6f, 12.0f);
    // A peak-relative floor stops low-level noise and breath from being
    // mistaken for clicks.  Those false detections were replacing harmless
    // samples with interpolated ones and could sound like added crackle.
    const float threshold =
        std::max(std::max(sigma * factor, rms * 0.002f), peakAbs * 0.045f);
    for (int i = 2; i < kWindow - 2; ++i) {
      if (std::fabs(residual[static_cast<size_t>(i)]) <= threshold) {
        continue;
      }
      int end = i;
      while (end + 1 < kWindow - 2 &&
             std::fabs(residual[static_cast<size_t>(end + 1)]) > threshold) {
        ++end;
      }
      if (end - i + 1 <= maxWidth_) {
        for (int j = i; j <= end; ++j) {
          mask[static_cast<size_t>(j)] = 1;
        }
        Run r;
        r.a = i;
        r.b = end;
        runs.push_back(r);
      }
      i = end + 1;
    }
  }

  void detectThumps(const std::vector<float>& x, std::vector<uint8_t>& mask,
                    std::vector<Run>& runs, int channel) noexcept {
    const float lpA = std::exp(-2.0f * 3.14159265358979323846f * 150.0f / sr_);
    const float envA = std::exp(-2.0f * 3.14159265358979323846f * 12.0f / sr_);
    float lpState = lowpassState_[static_cast<size_t>(channel)];
    float env = thumpEnv_[static_cast<size_t>(channel)];
    const float sens = std::clamp(controls_.clickSensitivity, 0.0f, 10.0f);
    const float threshold = 0.015f + 0.012f * (10.0f - sens);
    int i = 0;
    while (i < kWindow) {
      lpState = lpA * lpState + (1.0f - lpA) * x[static_cast<size_t>(i)];
      env = envA * env + (1.0f - envA) * std::fabs(lpState);
      lpBuf_[static_cast<size_t>(i)] = std::fabs(lpState) - env;
      ++i;
    }
    lowpassState_[static_cast<size_t>(channel)] = lpState;
    thumpEnv_[static_cast<size_t>(channel)] = env;
    i = 2;
    while (i < kWindow - 2) {
      if (lpBuf_[static_cast<size_t>(i)] <= threshold) {
        ++i;
        continue;
      }
      const int start = i;
      while (i < kWindow &&
             lpBuf_[static_cast<size_t>(i)] > threshold * 0.4f) {
        ++i;
      }
      const int end = i - 1;
      if (end > start && end - start + 1 <= maxWidth_) {
        for (int j = start; j <= end; ++j) {
          mask[static_cast<size_t>(j)] = 1;
        }
        Run r;
        r.a = start;
        r.b = end;
        runs.push_back(r);
      }
    }
  }

  void detectDropouts(const std::vector<float>& x,
                      std::vector<uint8_t>& mask,
                      std::vector<Run>& runs) noexcept {
    const float zero = 1.0e-4f;
    const int limit = std::min(maxWidth_, 4096);
    int i = 1;
    while (i < kWindow - 1) {
      if (std::fabs(x[static_cast<size_t>(i)]) > zero) {
        ++i;
        continue;
      }
      const int start = i;
      while (i < kWindow && std::fabs(x[static_cast<size_t>(i)]) <= zero) {
        ++i;
      }
      const int end = i - 1;
      if (end > start && end - start + 1 <= limit &&
          std::fabs(x[static_cast<size_t>(start - 1)]) > 0.01f &&
          std::fabs(x[static_cast<size_t>(end + 1)]) > 0.01f) {
        for (int j = start; j <= end; ++j) {
          mask[static_cast<size_t>(j)] = 1;
        }
        Run r;
        r.a = start;
        r.b = end;
        runs.push_back(r);
      }
    }
  }

  void maskToRuns(const std::vector<uint8_t>& mask,
                  std::vector<Run>& runs) noexcept {
    int i = 0;
    while (i < kWindow) {
      if (!mask[static_cast<size_t>(i)]) {
        ++i;
        continue;
      }
      const int start = i;
      while (i < kWindow && mask[static_cast<size_t>(i)]) {
        ++i;
      }
      Run r;
      r.a = start;
      r.b = i - 1;
      if (r.b - r.a + 1 <= maxWidth_) {
        runs.push_back(r);
      }
    }
  }

  void reconstruct(std::vector<float>& dst, const std::vector<float>& src,
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
      if (controls_.clickMode == 2) {
        dst[static_cast<size_t>(j)] = y0 + (y1 - y0) * t;
      } else {
        dst[static_cast<size_t>(j)] =
            h00 * y0 + h10 * h * m0 + h01 * y1 + h11 * h * m1;
      }
    }
  }

  float sr_ = 48000.0f;
  Controls controls_;
  float mix_ = 1.0f;
  int maxWidth_ = 64;
  int clicks_ = 0;
  int totalClicks_ = 0;
  float lowpassState_[2] = {0.0f, 0.0f};
  float thumpEnv_[2] = {0.0f, 0.0f};
  std::vector<float> origL_, origR_, workL_, workR_;
  std::vector<float> resL_, resR_, absRes_, sorted_, lpBuf_;
  std::vector<uint8_t> maskL_, maskR_, maskC_;
  std::vector<Run> runsL_, runsR_, runsC_;
  std::vector<Event> events_;
  std::vector<float> histL_, histR_, outL_, outR_;
  int histWrite_ = 0;
  uint64_t samplesSeen_ = 0;
  int outRead_ = 0;
  int outWrite_ = 0;
  int outCount_ = 0;
};

} // namespace repair
