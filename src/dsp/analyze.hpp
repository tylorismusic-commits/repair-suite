// Repair Suite — Auto Analyze
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Collects three seconds of input and estimates hum, noise floor, clipping,
// click density and a rough reverb tail.  Deliberately simple and offline: it
// runs once when the user presses Analyze, not continuously.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace repair {

class Analyzer {
public:
  struct Result {
    float humHz = 0.0f;
    float humDb = -120.0f;
    float noiseDb = -120.0f;
    float clipPct = 0.0f;
    float clickRate = 0.0f;
    float reverbSec = 0.0f;
  };

  void prepare(float sampleRate) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    capacity_ = std::max(1024, static_cast<int>(sr_ * 3.0f));
    buffer_.assign(static_cast<size_t>(capacity_), 0.0f);
    frameRms_.assign(static_cast<size_t>(capacity_ / 2400 + 1), 0.0f);
    absRes_.assign(static_cast<size_t>(capacity_), 0.0f);
    tmp_.assign(static_cast<size_t>(capacity_), 0.0f);
    window_.assign(static_cast<size_t>(capacity_), 0.0f);
    for (int i = 0; i < capacity_; ++i) {
      window_[static_cast<size_t>(i)] =
          0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i /
                               (capacity_ - 1));
    }
    reset();
  }

  void reset() noexcept {
    count_ = 0;
    active_ = false;
    ready_ = false;
    progress_ = 0.0f;
    result_ = Result{};
  }

  void start() noexcept {
    count_ = 0;
    active_ = true;
    ready_ = false;
    progress_ = 0.0f;
    result_ = Result{};
  }

  void process(const float* left, const float* right, int frames) noexcept {
    if (!active_ || !left || !right || frames <= 0) {
      return;
    }
    const int room = capacity_ - count_;
    const int n = std::min(room, frames);
    for (int i = 0; i < n; ++i) {
      buffer_[static_cast<size_t>(count_ + i)] =
          0.5f * (left[i] + (right ? right[i] : left[i]));
    }
    count_ += n;
    progress_ = static_cast<float>(count_) / capacity_;
    if (count_ >= capacity_) {
      analyse();
      active_ = false;
      ready_ = true;
      progress_ = 1.0f;
    }
  }

  bool ready() const noexcept { return ready_; }
  float progress() const noexcept { return progress_; }
  const Result& result() const noexcept { return result_; }

private:
  double toneMag(float hz, float sr) const noexcept {
    const double w = 2.0 * 3.14159265358979323846 * hz / sr;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (int i = 0; i < count_; ++i) {
      const double s0 =
          buffer_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)] +
          coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / (0.25 * count_);
  }

  void analyse() noexcept {
    if (count_ < 1024) {
      return;
    }
    // Noise floor: 10th percentile of 50 ms RMS frames.
    int frames = 0;
    for (int start = 0; start + 2400 <= count_; start += 2400) {
      double sum = 0.0;
      for (int i = 0; i < 2400; ++i) {
        const float v = buffer_[static_cast<size_t>(start + i)];
        sum += static_cast<double>(v) * v;
      }
      frameRms_[static_cast<size_t>(frames++)] =
          static_cast<float>(std::sqrt(sum / 2400.0));
    }
    if (frames > 0) {
      std::sort(frameRms_.begin(), frameRms_.begin() + frames);
      const float floorRms = frameRms_[static_cast<size_t>(frames / 10)];
      result_.noiseDb = 20.0f * std::log10(std::max(floorRms, 1.0e-7f));
    }

    int clipped = 0;
    for (int i = 0; i < count_; ++i) {
      if (std::fabs(buffer_[static_cast<size_t>(i)]) >= 0.999f) {
        ++clipped;
      }
    }
    result_.clipPct = 100.0f * static_cast<float>(clipped) / count_;

    // Hum: pick the stronger 50/60 Hz harmonic series.
    float bestHz = 50.0f;
    double bestScore = -1.0;
    double bestFund = 0.0;
    for (float base : {50.0f, 60.0f}) {
      double score = 0.0;
      double fund = 0.0;
      for (int k = 1; k <= 5; ++k) {
        const double m = toneMag(base * k, sr_);
        score += m;
        if (k == 1) {
          fund = m;
        }
      }
      if (score > bestScore) {
        bestScore = score;
        bestFund = fund;
        bestHz = base;
      }
    }
    result_.humDb = 20.0f * std::log10(std::max(static_cast<float>(bestFund),
                                                1.0e-7f));
    result_.humHz = result_.humDb > -60.0f ? bestHz : 0.0f;

    // Clicks: median-residual outliers.
    int clicks = 0;
    for (int i = 2; i < count_; ++i) {
      absRes_[static_cast<size_t>(i)] =
          std::fabs(buffer_[static_cast<size_t>(i)] -
                    2.0f * buffer_[static_cast<size_t>(i - 1)] +
                    buffer_[static_cast<size_t>(i - 2)]);
    }
    const int tmpCount = std::max(0, count_ - 2);
    for (int j = 0; j < tmpCount; ++j) {
      tmp_[static_cast<size_t>(j)] = absRes_[static_cast<size_t>(j + 2)];
    }
    if (tmpCount > 0) {
      std::nth_element(tmp_.begin(), tmp_.begin() + tmpCount / 2,
                       tmp_.begin() + tmpCount);
      const float sigma =
          std::max(1.0e-5f, tmp_[static_cast<size_t>(tmpCount / 2)] * 1.4826f);
      const float threshold = 8.0f * sigma;
      for (int j = 3; j < count_ - 1; ++j) {
        if (absRes_[static_cast<size_t>(j)] > threshold &&
            absRes_[static_cast<size_t>(j)] >=
                absRes_[static_cast<size_t>(j - 1)] &&
            absRes_[static_cast<size_t>(j)] >=
                absRes_[static_cast<size_t>(j + 1)]) {
          ++clicks;
        }
      }
    }
    result_.clickRate =
        static_cast<float>(clicks) / (static_cast<float>(count_) / sr_);

    // Rough reverb estimate: energy decay after the loudest peak.
    int peak = 0;
    float peakAbs = 0.0f;
    for (int j = 0; j < count_; ++j) {
      const float a = std::fabs(buffer_[static_cast<size_t>(j)]);
      if (a > peakAbs) {
        peakAbs = a;
        peak = j;
      }
    }
    if (peakAbs > 0.01f) {
      int end = peak;
      while (end < count_ &&
             std::fabs(buffer_[static_cast<size_t>(end)]) > peakAbs * 0.1f) {
        ++end;
      }
      if (end > peak + 64) {
        result_.reverbSec =
            static_cast<float>(end - peak) / sr_ * 3.0f;  // -20 dB -> RT60
      }
    }
  }

  float sr_ = 48000.0f;
  int capacity_ = 1;
  int count_ = 0;
  bool active_ = false;
  bool ready_ = false;
  float progress_ = 0.0f;
  Result result_;
  std::vector<float> buffer_, frameRms_, absRes_, tmp_;
  std::vector<double> window_;
};

} // namespace repair
