// Repair Suite — real-time STFT
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Streaming short-time Fourier transform with 75% overlap, Hann analysis and
// synthesis, and a caller-supplied spectral mask.  All buffers are allocated
// in prepare(); process() performs no allocation, locks or I/O.
//
// Latency is one frame (fftSize samples).  The transform is complex for both
// channels; the mask callback receives non-negative bins and the result is
// mirrored to the negative frequencies before the inverse.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace repair {

class Stft {
public:
  void prepare(float sampleRate, int fftSize = 2048, int hopSize = -1) noexcept {
    sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    fftSize_ = std::max(64, fftSize);
    if (hopSize <= 0) {
      hopSize = fftSize_ / 4;
    }
    hopSize_ = std::clamp(hopSize, 1, fftSize_);
    bins_ = fftSize_ / 2 + 1;

    window_.resize(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i) {
      window_[i] = 0.5f -
                   0.5f * std::cos(2.0f * kPi * static_cast<float>(i) /
                                   static_cast<float>(fftSize_));
    }
    // Hann at 75% overlap sums to ~1.5 in the power domain.  Compute the
    // actual normalisation for the configured hop instead of hard-coding it,
    // so other overlap factors stay correct.
    olaNorm_ = 0.0f;
    for (int i = 0; i < fftSize_; i += hopSize_) {
      olaNorm_ += window_[i] * window_[i];
    }
    if (olaNorm_ < 1.0e-6f) {
      olaNorm_ = 1.0f;
    }

    bitrev_.resize(static_cast<size_t>(fftSize_));
    int bits = 0;
    while ((1 << bits) < fftSize_) {
      ++bits;
    }
    for (int i = 0; i < fftSize_; ++i) {
      uint32_t x = static_cast<uint32_t>(i);
      uint32_t r = 0;
      for (int b = 0; b < bits; ++b) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
      }
      bitrev_[i] = static_cast<int>(r);
    }
    twCos_.resize(static_cast<size_t>(fftSize_ / 2));
    twSin_.resize(static_cast<size_t>(fftSize_ / 2));
    for (int i = 0; i < fftSize_ / 2; ++i) {
      const float a = -2.0f * kPi * static_cast<float>(i) /
                      static_cast<float>(fftSize_);
      twCos_[i] = std::cos(a);
      twSin_[i] = std::sin(a);
    }

    inL_.assign(static_cast<size_t>(fftSize_), 0.0f);
    inR_.assign(static_cast<size_t>(fftSize_), 0.0f);
    frameL_.assign(static_cast<size_t>(fftSize_), 0.0f);
    frameR_.assign(static_cast<size_t>(fftSize_), 0.0f);
    reL_.assign(static_cast<size_t>(fftSize_), 0.0f);
    imL_.assign(static_cast<size_t>(fftSize_), 0.0f);
    reR_.assign(static_cast<size_t>(fftSize_), 0.0f);
    imR_.assign(static_cast<size_t>(fftSize_), 0.0f);
    olaL_.assign(static_cast<size_t>(fftSize_ + hopSize_), 0.0f);
    olaR_.assign(static_cast<size_t>(fftSize_ + hopSize_), 0.0f);
    outL_.assign(static_cast<size_t>(fftSize_ + hopSize_), 0.0f);
    outR_.assign(static_cast<size_t>(fftSize_ + hopSize_), 0.0f);
    reset();
  }

  void reset() noexcept {
    std::fill(inL_.begin(), inL_.end(), 0.0f);
    std::fill(inR_.begin(), inR_.end(), 0.0f);
    std::fill(olaL_.begin(), olaL_.end(), 0.0f);
    std::fill(olaR_.begin(), olaR_.end(), 0.0f);
    std::fill(outL_.begin(), outL_.end(), 0.0f);
    std::fill(outR_.begin(), outR_.end(), 0.0f);
    inWrite_ = 0;
    sinceHop_ = 0;
    samplesSeen_ = 0;
    frameStart_ = 0;
    outRead_ = 0;
    outWrite_ = 0;
    outCount_ = 0;
  }

  int latencySamples() const noexcept { return std::max(0, fftSize_ - 1); }
  int frameSize() const noexcept { return fftSize_; }
  int hopSize() const noexcept { return hopSize_; }
  int bins() const noexcept { return bins_; }
  float sampleRate() const noexcept { return sampleRate_; }

  // Identity pass-through.  Useful as an engine stage and as a test oracle.
  void process(float* left, float* right, int frames) noexcept {
    process(left, right, frames, [](float*, float*, float*, float*, int) {});
  }

  // mask(reL, imL, reR, imR, bins) may modify the non-negative bins in place.
  // The same gain is mirrored to the negative frequencies.
  template <typename Mask>
  void process(float* left, float* right, int frames, Mask&& mask) noexcept {
    if (!left || !right || frames <= 0 || fftSize_ <= 0) {
      return;
    }
    for (int n = 0; n < frames; ++n) {
      inL_[static_cast<size_t>(inWrite_)] = left[n];
      inR_[static_cast<size_t>(inWrite_)] = right[n];
      inWrite_ = (inWrite_ + 1) % fftSize_;
      ++sinceHop_;
      ++samplesSeen_;

      if (samplesSeen_ >= fftSize_ && sinceHop_ >= hopSize_) {
        sinceHop_ = 0;
        frameStart_ = samplesSeen_ - fftSize_;
        analyseFrame();
        mask(reL_.data(), imL_.data(), reR_.data(), imR_.data(), bins_);
        mirrorSpectrum();
        synthesiseFrame();
        queueHop();
      }

      if (outCount_ > 0) {
        left[n] = outL_[static_cast<size_t>(outRead_)];
        right[n] = outR_[static_cast<size_t>(outRead_)];
        outRead_ = (outRead_ + 1) % static_cast<int>(outL_.size());
        --outCount_;
      } else {
        left[n] = 0.0f;
        right[n] = 0.0f;
      }
    }
  }

private:
  static constexpr float kPi = 3.14159265358979323846f;

  void analyseFrame() noexcept {
    // Oldest sample sits at inWrite_.
    for (int i = 0; i < fftSize_; ++i) {
      const int src = (inWrite_ + i) % fftSize_;
      frameL_[static_cast<size_t>(i)] = inL_[static_cast<size_t>(src)] *
                                        window_[static_cast<size_t>(i)];
      frameR_[static_cast<size_t>(i)] = inR_[static_cast<size_t>(src)] *
                                        window_[static_cast<size_t>(i)];
    }
    fft(frameL_.data(), reL_.data(), imL_.data(), false);
    fft(frameR_.data(), reR_.data(), imR_.data(), false);
  }

  void mirrorSpectrum() noexcept {
    // The callback works on bins 0..N/2.  Mirror to the negative frequencies
    // so the inverse transform is real.
    for (int k = 1; k < fftSize_ / 2; ++k) {
      const int m = fftSize_ - k;
      reL_[static_cast<size_t>(m)] = reL_[static_cast<size_t>(k)];
      imL_[static_cast<size_t>(m)] = -imL_[static_cast<size_t>(k)];
      reR_[static_cast<size_t>(m)] = reR_[static_cast<size_t>(k)];
      imR_[static_cast<size_t>(m)] = -imR_[static_cast<size_t>(k)];
    }
    imL_[0] = 0.0f;
    imR_[0] = 0.0f;
    reL_[static_cast<size_t>(fftSize_ / 2)] =
        reL_[static_cast<size_t>(fftSize_ / 2)];  // Nyquist stays real
    imL_[static_cast<size_t>(fftSize_ / 2)] = 0.0f;
    imR_[static_cast<size_t>(fftSize_ / 2)] = 0.0f;
  }

  void synthesiseFrame() noexcept {
    fft(reL_.data(), reL_.data(), imL_.data(), true);
    fft(reR_.data(), reR_.data(), imR_.data(), true);
    for (int i = 0; i < fftSize_; ++i) {
      const float w = window_[static_cast<size_t>(i)];
      const int at = (frameStart_ + i) % static_cast<int>(olaL_.size());
      olaL_[static_cast<size_t>(at)] +=
          reL_[static_cast<size_t>(i)] * w / olaNorm_;
      olaR_[static_cast<size_t>(at)] +=
          reR_[static_cast<size_t>(i)] * w / olaNorm_;
    }
  }

  void queueHop() noexcept {
    // Samples [frameStart, frameStart + hop) will receive no more overlap
    // contributions: the next frame starts at frameStart + hop.
    for (int i = 0; i < hopSize_; ++i) {
      const int at = (frameStart_ + i) % static_cast<int>(olaL_.size());
      outL_[static_cast<size_t>(outWrite_)] = olaL_[static_cast<size_t>(at)];
      outR_[static_cast<size_t>(outWrite_)] = olaR_[static_cast<size_t>(at)];
      olaL_[static_cast<size_t>(at)] = 0.0f;
      olaR_[static_cast<size_t>(at)] = 0.0f;
      outWrite_ = (outWrite_ + 1) % static_cast<int>(outL_.size());
      outCount_ = std::min(outCount_ + 1, static_cast<int>(outL_.size()));
    }
  }

  // In-place radix-2 FFT.  `in` is the real input; `re`/`im` receive the
  // spectrum.  For the inverse, pass `re` as both `in` and `re`.
  void fft(const float* in, float* re, float* im, bool inverse) noexcept {
    const int n = fftSize_;
    if (!inverse) {
      for (int i = 0; i < n; ++i) {
        re[i] = in[i];
        im[i] = 0.0f;
      }
    } else if (in != re) {
      for (int i = 0; i < n; ++i) {
        re[i] = in[i];
      }
    }
    for (int i = 0; i < n; ++i) {
      const int j = bitrev_[static_cast<size_t>(i)];
      if (j > i) {
        std::swap(re[i], re[j]);
        std::swap(im[i], im[j]);
      }
    }
    for (int len = 2; len <= n; len <<= 1) {
      const int half = len >> 1;
      const int step = n / len;
      for (int base = 0; base < n; base += len) {
        for (int j = 0; j < half; ++j) {
          const float wr = twCos_[static_cast<size_t>(j * step)];
          const float wi = inverse ? -twSin_[static_cast<size_t>(j * step)]
                                   : twSin_[static_cast<size_t>(j * step)];
          const int a = base + j;
          const int b = a + half;
          const float tr = wr * re[b] - wi * im[b];
          const float ti = wr * im[b] + wi * re[b];
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }
    if (inverse) {
      const float scale = 1.0f / static_cast<float>(n);
      for (int i = 0; i < n; ++i) {
        re[i] *= scale;
        im[i] *= scale;
      }
    }
  }

  float sampleRate_ = 48000.0f;
  int fftSize_ = 2048;
  int hopSize_ = 512;
  int bins_ = 1025;
  float olaNorm_ = 1.5f;
  std::vector<float> window_;
  std::vector<int> bitrev_;
  std::vector<float> twCos_, twSin_;
  std::vector<float> inL_, inR_;
  std::vector<float> frameL_, frameR_;
  std::vector<float> reL_, imL_, reR_, imR_;
  std::vector<float> olaL_, olaR_;
  std::vector<float> outL_, outR_;
  int inWrite_ = 0;
  int sinceHop_ = 0;
  int samplesSeen_ = 0;
  int frameStart_ = 0;
  int outRead_ = 0;
  int outWrite_ = 0;
  int outCount_ = 0;
};

} // namespace repair
