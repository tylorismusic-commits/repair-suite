// Repair Suite — radix-2 FFT
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small reusable complex FFT used by the STFT and the declipping reconstructions.
// Buffers are allocated in prepare(); the transform itself does not allocate.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace repair {

class Fft {
public:
  void prepare(int size) {
    n_ = std::max(2, size);
    // Round up to a power of two.
    int p = 1;
    while (p < n_) {
      p <<= 1;
    }
    n_ = p;
    int bits = 0;
    while ((1 << bits) < n_) {
      ++bits;
    }
    bitrev_.resize(static_cast<size_t>(n_));
    for (int i = 0; i < n_; ++i) {
      uint32_t x = static_cast<uint32_t>(i);
      uint32_t r = 0;
      for (int b = 0; b < bits; ++b) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
      }
      bitrev_[static_cast<size_t>(i)] = static_cast<int>(r);
    }
    twCos_.resize(static_cast<size_t>(n_ / 2));
    twSin_.resize(static_cast<size_t>(n_ / 2));
    for (int i = 0; i < n_ / 2; ++i) {
      const double a = -2.0 * 3.14159265358979323846 * i / n_;
      twCos_[static_cast<size_t>(i)] = static_cast<float>(std::cos(a));
      twSin_[static_cast<size_t>(i)] = static_cast<float>(std::sin(a));
    }
  }

  int size() const noexcept { return n_; }

  void forward(const float* realIn, float* re, float* im) const noexcept {
    for (int i = 0; i < n_; ++i) {
      re[i] = realIn[i];
      im[i] = 0.0f;
    }
    transform(re, im, false);
  }

  void inverse(float* re, float* im) const noexcept {
    transform(re, im, true);
    const float scale = 1.0f / static_cast<float>(n_);
    for (int i = 0; i < n_; ++i) {
      re[i] *= scale;
      im[i] *= scale;
    }
  }

private:
  void transform(float* re, float* im, bool inverse) const noexcept {
    for (int i = 0; i < n_; ++i) {
      const int j = bitrev_[static_cast<size_t>(i)];
      if (j > i) {
        std::swap(re[i], re[j]);
        std::swap(im[i], im[j]);
      }
    }
    for (int len = 2; len <= n_; len <<= 1) {
      const int half = len >> 1;
      const int step = n_ / len;
      for (int base = 0; base < n_; base += len) {
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
  }

  int n_ = 0;
  std::vector<int> bitrev_;
  std::vector<float> twCos_, twSin_;
};

} // namespace repair
