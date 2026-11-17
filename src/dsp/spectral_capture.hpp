// Repair Suite — spectral capture, freeze snapshot and edit overlay
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audio thread: fills a rolling stereo buffer, takes a stable snapshot when the
// editor asks to freeze, and overlays stored edits on playback.
// Editor thread: requests a freeze, reads the snapshot, adds/clears edits.
//
// The edit list is guarded by a mutex on the editor side; the audio thread only
// uses try_lock and skips the overlay for one block if the editor is writing.
// That is a deliberate trade-off: the audio thread never blocks.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

#include "spectral_bridge.hpp"

namespace repair {

class SpectralCapture : public SpectralBridge {
public:
  using Edit = SpectralEdit;

  void prepare(float sampleRate, float maxSeconds = 30.0f) noexcept {
    sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    capacity_ = std::max(1, static_cast<int>(sampleRate_ * maxSeconds));
    left_.assign(static_cast<size_t>(capacity_), 0.0f);
    right_.assign(static_cast<size_t>(capacity_), 0.0f);
    frozenL_.assign(static_cast<size_t>(capacity_), 0.0f);
    frozenR_.assign(static_cast<size_t>(capacity_), 0.0f);
    reset();
  }

  void reset() noexcept {
    std::fill(left_.begin(), left_.end(), 0.0f);
    std::fill(right_.begin(), right_.end(), 0.0f);
    write_ = 0;
    count_ = 0;
    countAtomic_.store(0, std::memory_order_relaxed);
    captureStartTime_ = 0.0;
    capturing_ = false;
    frozen_.store(false, std::memory_order_relaxed);
    freezeRequest_.store(false, std::memory_order_relaxed);
    frozenReady_.store(false, std::memory_order_relaxed);
    frozenCount_ = 0;
    frozenStartTime_ = 0.0;
    {
      std::lock_guard<std::mutex> lock(editMutex_);
      edits_.clear();
    }
    editCount_.store(0, std::memory_order_relaxed);
    previewActive_.store(false, std::memory_order_release);
    {
      std::unique_lock<std::mutex> lock(previewMutex_, std::try_to_lock);
      if (lock.owns_lock()) {
        previewCount_ = 0;
        previewPos_.store(0, std::memory_order_relaxed);
      }
    }
  }

  void setArmed(bool armed) noexcept {
    if (armed == capturing_) {
      return;
    }
    capturing_ = armed;
    if (capturing_) {
      write_ = 0;
      count_ = 0;
      countAtomic_.store(0, std::memory_order_relaxed);
      captureStartTime_ = 0.0;
      captureStartValid_ = false;
      frozen_.store(false, std::memory_order_relaxed);
      frozenReady_.store(false, std::memory_order_relaxed);
      freezeRequest_.store(false, std::memory_order_relaxed);
      frozenCount_ = 0;
      previewActive_.store(false, std::memory_order_release);
    }
  }

  bool armed() const noexcept { return capturing_; }
  bool frozen() const noexcept {
    return frozen_.load(std::memory_order_acquire);
  }
  void freeze() noexcept {
    frozen_.store(true, std::memory_order_release);
    frozenReady_.store(true, std::memory_order_release);
  }
  void thaw() noexcept {
    frozen_.store(false, std::memory_order_release);
    frozenReady_.store(false, std::memory_order_release);
    freezeRequest_.store(false, std::memory_order_release);
  }

  // SpectralBridge ---------------------------------------------------------
  void requestFreeze() override {
    freezeRequest_.store(true, std::memory_order_release);
  }

  void requestThaw() override { thaw(); }

  bool frozenReady() const override {
    return frozenReady_.load(std::memory_order_acquire);
  }

  bool copyFrozen(std::vector<float>* left, std::vector<float>* right,
                  double* startTime) override {
    if (!left || !right || !frozenReady()) {
      return false;
    }
    const int n = frozenCount_;
    left->resize(static_cast<size_t>(n));
    right->resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      (*left)[static_cast<size_t>(i)] = frozenL_[static_cast<size_t>(i)];
      (*right)[static_cast<size_t>(i)] = frozenR_[static_cast<size_t>(i)];
    }
    if (startTime) {
      *startTime = frozenStartTime_;
    }
    return true;
  }

  bool addEdit(double startTime, double endTime, const float* left,
               const float* right, int frames) override {
    if (!left || !right || frames <= 0 || endTime <= startTime) {
      return false;
    }
    Edit edit;
    edit.startTime = startTime;
    edit.endTime = endTime;
    edit.left.assign(left, left + frames);
    edit.right.assign(right, right + frames);
    {
      std::lock_guard<std::mutex> lock(editMutex_);
      edits_.push_back(std::move(edit));
    }
    editCount_.fetch_add(1, std::memory_order_release);
    return true;
  }

  void clearEdits() override {
    {
      std::lock_guard<std::mutex> lock(editMutex_);
      edits_.clear();
    }
    editCount_.store(0, std::memory_order_release);
  }

  int editCount() const override {
    return editCount_.load(std::memory_order_acquire);
  }

  float capturedSeconds() const override {
    return static_cast<float>(countAtomic_.load(std::memory_order_relaxed)) /
           sampleRate_;
  }

  // Audio path -------------------------------------------------------------
  void push(const float* left, const float* right, int frames,
            double blockStartTime, bool transportValid) noexcept {
    updatePlaybackTime(blockStartTime, transportValid);
    if (freezeRequest_.exchange(false, std::memory_order_acq_rel)) {
      snapshot();
    }
    if (!capturing_ || frozen_.load(std::memory_order_acquire) || !left ||
        !right || frames <= 0) {
      return;
    }
    if (!transportValid) {
      captureStartValid_ = false;
      write_ = 0;
      count_ = 0;
      countAtomic_.store(0, std::memory_order_relaxed);
      return;
    }
    if (captureStartValid_) {
      const double expected =
          captureStartTime_ + static_cast<double>(count_) / sampleRate_;
      if (std::fabs(blockStartTime - expected) >
          std::max(0.05, 4.0 / sampleRate_)) {
        write_ = 0;
        count_ = 0;
        countAtomic_.store(0, std::memory_order_relaxed);
        captureStartValid_ = false;
      }
    }
    if (!captureStartValid_) {
      captureStartTime_ = blockStartTime;
      captureStartValid_ = true;
    }
    for (int i = 0; i < frames; ++i) {
      if (count_ >= capacity_) {
        captureStartTime_ += 1.0 / sampleRate_;
      }
      left_[static_cast<size_t>(write_)] = left[i];
      right_[static_cast<size_t>(write_)] = right[i];
      write_ = (write_ + 1) % capacity_;
      count_ = std::min(count_ + 1, capacity_);
    }
    countAtomic_.store(count_, std::memory_order_relaxed);
  }

  int capturedFrames() const noexcept {
    return countAtomic_.load(std::memory_order_relaxed);
  }
  double captureStartTime() const noexcept { return captureStartTime_; }
  bool captureValid() const noexcept { return captureStartValid_; }

  int copyCaptured(std::vector<float>* left,
                   std::vector<float>* right) const {
    if (!left || !right) {
      return 0;
    }
    const int n = countAtomic_.load(std::memory_order_relaxed);
    left->resize(static_cast<size_t>(n));
    right->resize(static_cast<size_t>(n));
    const int start = (write_ - n + capacity_) % capacity_;
    for (int i = 0; i < n; ++i) {
      const int at = (start + i) % capacity_;
      (*left)[static_cast<size_t>(i)] = left_[static_cast<size_t>(at)];
      (*right)[static_cast<size_t>(i)] = right_[static_cast<size_t>(at)];
    }
    return n;
  }

  const std::vector<Edit>& edits() const noexcept { return edits_; }

  // Host state thread: a consistent copy of the edit list.
  void copyEdits(std::vector<SpectralEdit>* out) const override {
    if (!out) {
      return;
    }
    std::lock_guard<std::mutex> lock(editMutex_);
    *out = edits_;
  }

  void replaceEdits(const std::vector<SpectralEdit>& edits) override {
    std::lock_guard<std::mutex> lock(editMutex_);
    edits_ = edits;
    editCount_.store(static_cast<int>(edits.size()),
                     std::memory_order_release);
  }

  void setPreview(const float* left, const float* right, int frames) override {
    if (!left || !right || frames <= 0) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(previewMutex_);
      previewL_.assign(left, left + frames);
      previewR_.assign(right, right + frames);
      previewCount_ = frames;
      previewPos_.store(0, std::memory_order_release);
    }
    previewActive_.store(true, std::memory_order_release);
  }

  void clearPreview() override {
    previewActive_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(previewMutex_);
    previewCount_ = 0;
    previewPos_.store(0, std::memory_order_release);
  }

  bool previewActive() const override {
    return previewActive_.load(std::memory_order_acquire);
  }

  bool readPreview(float* left, float* right, int frames) noexcept {
    if (!previewActive_.load(std::memory_order_acquire) || frames <= 0) {
      return false;
    }
    std::unique_lock<std::mutex> lock(previewMutex_, std::try_to_lock);
    if (!lock.owns_lock() || previewCount_ <= 0) {
      return false;
    }
    int pos = previewPos_.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
      left[i] = previewL_[static_cast<size_t>(pos)];
      right[i] = previewR_[static_cast<size_t>(pos)];
      pos = (pos + 1) % previewCount_;
    }
    previewPos_.store(pos, std::memory_order_relaxed);
    return true;
  }

  void copyEditsForState(std::vector<Edit>* out) const {
    if (!out) {
      return;
    }
    std::lock_guard<std::mutex> lock(editMutex_);
    *out = edits_;
  }

  void replaceEditsFromState(const std::vector<Edit>& edits) {
    {
      std::lock_guard<std::mutex> lock(editMutex_);
      edits_ = edits;
    }
    editCount_.store(static_cast<int>(edits.size()),
                     std::memory_order_release);
  }

  void applyOverlay(float* left, float* right, int frames,
                    double blockStartTime, bool transportValid) noexcept {
    if (!left || !right || frames <= 0 ||
        editCount_.load(std::memory_order_acquire) == 0 || !transportValid) {
      return;
    }
    std::unique_lock<std::mutex> lock(editMutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      return;
    }
    const double fadeSec = 0.005;
    for (int i = 0; i < frames; ++i) {
      const double t = blockStartTime + static_cast<double>(i) / sampleRate_;
      // Later edits win in overlaps.  This matches preview and diff playback,
      // where the edit list is applied in insertion order.
      const Edit* chosen = nullptr;
      for (auto it = edits_.rbegin(); it != edits_.rend(); ++it) {
        if (t >= it->startTime && t < it->endTime) {
          chosen = &*it;
          break;
        }
      }
      if (!chosen) {
        continue;
      }
      const double local = t - chosen->startTime;
      const int index = static_cast<int>(local * sampleRate_ + 0.5);
      if (index < 0 || index >= static_cast<int>(chosen->left.size())) {
        continue;
      }
      const double fromStart = local;
      const double fromEnd = chosen->endTime - t;
      const double fade =
          std::min({1.0, fromStart / fadeSec, fromEnd / fadeSec});
      const float g = static_cast<float>(std::clamp(fade, 0.0, 1.0));
      left[i] += g * (chosen->left[static_cast<size_t>(index)] - left[i]);
      right[i] += g * (chosen->right[static_cast<size_t>(index)] - right[i]);
    }
  }

  double playbackTime() const noexcept { return playbackTime_; }
  bool playbackValid() const noexcept { return playbackValid_; }

private:
  void snapshot() noexcept {
    if (count_ <= 0) {
      return;
    }
    const int start = (write_ - count_ + capacity_) % capacity_;
    for (int i = 0; i < count_; ++i) {
      const int at = (start + i) % capacity_;
      frozenL_[static_cast<size_t>(i)] = left_[static_cast<size_t>(at)];
      frozenR_[static_cast<size_t>(i)] = right_[static_cast<size_t>(at)];
    }
    frozenCount_ = count_;
    frozenStartTime_ = captureStartTime_;
    frozen_.store(true, std::memory_order_release);
    frozenReady_.store(true, std::memory_order_release);
  }

  void updatePlaybackTime(double seconds, bool valid) noexcept {
    playbackTime_ = valid ? seconds : playbackTime_;
    playbackValid_ = valid;
  }

  float sampleRate_ = 48000.0f;
  int capacity_ = 1;
  std::vector<float> left_, right_;
  std::vector<float> frozenL_, frozenR_;
  int write_ = 0;
  int count_ = 0;
  std::atomic<int> countAtomic_{0};
  double captureStartTime_ = 0.0;
  bool captureStartValid_ = false;
  bool capturing_ = false;
  std::atomic<bool> frozen_{false};
  std::atomic<bool> freezeRequest_{false};
  std::atomic<bool> frozenReady_{false};
  int frozenCount_ = 0;
  double frozenStartTime_ = 0.0;
  mutable std::mutex editMutex_;
  std::vector<Edit> edits_;
  std::atomic<int> editCount_{0};
  std::mutex previewMutex_;
  std::vector<float> previewL_, previewR_;
  int previewCount_ = 0;
  std::atomic<int> previewPos_{0};
  std::atomic<bool> previewActive_{false};
  double playbackTime_ = 0.0;
  bool playbackValid_ = false;
};

} // namespace repair
