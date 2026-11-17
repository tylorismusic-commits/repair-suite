// Repair Suite — spectral capture and edit overlay (prototype)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0 prototype for the in-plug-in spectral repair workflow:
//   - a rolling stereo capture buffer with absolute host-time stamps
//   - a list of replacement edits keyed to host time
//   - playback-time overlay with short crossfades
//
// The UI/audio-thread handoff is deliberately not implemented yet.  This class
// documents the timeline model and is covered by unit tests; the production
// version will add a lock-free capture/command bridge and state serialisation.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace repair {

class SpectralCapture {
public:
  struct Edit {
    double startTime = 0.0;
    double endTime = 0.0;
    std::vector<float> left;
    std::vector<float> right;
  };

  void prepare(float sampleRate, float maxSeconds = 30.0f) noexcept {
    sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    capacity_ = std::max(1, static_cast<int>(sampleRate_ * maxSeconds));
    left_.assign(static_cast<size_t>(capacity_), 0.0f);
    right_.assign(static_cast<size_t>(capacity_), 0.0f);
    reset();
  }

  void reset() noexcept {
    std::fill(left_.begin(), left_.end(), 0.0f);
    std::fill(right_.begin(), right_.end(), 0.0f);
    write_ = 0;
    count_ = 0;
    captureStartTime_ = 0.0;
    capturing_ = false;
    frozen_ = false;
    edits_.clear();
  }

  void setArmed(bool armed) noexcept {
    if (armed == capturing_) {
      return;
    }
    capturing_ = armed;
    if (capturing_) {
      write_ = 0;
      count_ = 0;
      captureStartTime_ = 0.0;
      captureStartValid_ = false;
      frozen_ = false;
    }
  }

  bool armed() const noexcept { return capturing_; }
  bool frozen() const noexcept { return frozen_; }
  void freeze() noexcept { frozen_ = true; }
  void thaw() noexcept { frozen_ = false; }

  // Push a block and the host transport time of its first sample.  Capture
  // starts at the first block with valid transport; a jump resets the buffer so
  // edits are never anchored to the wrong audio.
  void push(const float* left, const float* right, int frames,
            double blockStartTime, bool transportValid) noexcept {
    if (!capturing_ || !left || !right || frames <= 0) {
      updatePlaybackTime(blockStartTime, transportValid);
      return;
    }
    if (!transportValid) {
      captureStartValid_ = false;
      updatePlaybackTime(blockStartTime, false);
      return;
    }
    if (captureStartValid_) {
      const double expected =
          captureStartTime_ + static_cast<double>(count_) / sampleRate_;
      if (std::fabs(blockStartTime - expected) >
          std::max(0.05, 4.0 / sampleRate_)) {
        // Transport jumped: the previous capture is no longer continuous.
        write_ = 0;
        count_ = 0;
        captureStartValid_ = false;
      }
    }
    if (!captureStartValid_) {
      captureStartTime_ = blockStartTime;
      captureStartValid_ = true;
    }
    for (int i = 0; i < frames; ++i) {
      left_[static_cast<size_t>(write_)] = left[i];
      right_[static_cast<size_t>(write_)] = right[i];
      write_ = (write_ + 1) % capacity_;
      count_ = std::min(count_ + 1, capacity_);
    }
    updatePlaybackTime(blockStartTime, true);
  }

  float capturedSeconds() const noexcept {
    return static_cast<float>(count_) / sampleRate_;
  }

  int capturedFrames() const noexcept { return count_; }
  double captureStartTime() const noexcept { return captureStartTime_; }
  bool captureValid() const noexcept { return captureStartValid_; }

  // Copy the captured audio in chronological order.  Intended for the UI
  // thread after freeze(); not RT-safe.  Returns the number of frames copied.
  int copyCaptured(std::vector<float>* left,
                   std::vector<float>* right) const {
    if (!left || !right) {
      return 0;
    }
    left->resize(static_cast<size_t>(count_));
    right->resize(static_cast<size_t>(count_));
    const int start = (write_ - count_ + capacity_) % capacity_;
    for (int i = 0; i < count_; ++i) {
      const int at = (start + i) % capacity_;
      (*left)[static_cast<size_t>(i)] = left_[static_cast<size_t>(at)];
      (*right)[static_cast<size_t>(i)] = right_[static_cast<size_t>(at)];
    }
    return count_;
  }

  bool addEdit(double startTime, double endTime, const float* left,
               const float* right, int frames) {
    if (!left || !right || frames <= 0 || endTime <= startTime) {
      return false;
    }
    Edit edit;
    edit.startTime = startTime;
    edit.endTime = endTime;
    edit.left.assign(left, left + frames);
    edit.right.assign(right, right + frames);
    edits_.push_back(std::move(edit));
    return true;
  }

  void clearEdits() noexcept { edits_.clear(); }
  int editCount() const noexcept { return static_cast<int>(edits_.size()); }
  const std::vector<Edit>& edits() const noexcept { return edits_; }

  // Replace live samples with stored edit audio.  blockStartTime is the host
  // time of the first input sample.  Edits are crossfaded in/out over a few
  // milliseconds so boundaries do not click.
  void applyOverlay(float* left, float* right, int frames,
                    double blockStartTime, bool transportValid) const noexcept {
    if (!left || !right || frames <= 0 || edits_.empty() || !transportValid) {
      return;
    }
    const double fadeSec = 0.005;
    for (int i = 0; i < frames; ++i) {
      const double t = blockStartTime + static_cast<double>(i) / sampleRate_;
      for (const Edit& edit : edits_) {
        if (t < edit.startTime || t >= edit.endTime) {
          continue;
        }
        const double local = t - edit.startTime;
        const int index = static_cast<int>(local * sampleRate_ + 0.5);
        if (index < 0 ||
            index >= static_cast<int>(edit.left.size())) {
          continue;
        }
        const double fromStart = local;
        const double fromEnd = edit.endTime - t;
        const double fade = std::min({1.0, fromStart / fadeSec,
                                      fromEnd / fadeSec});
        const float g = static_cast<float>(std::clamp(fade, 0.0, 1.0));
        left[i] += g * (edit.left[static_cast<size_t>(index)] - left[i]);
        right[i] += g * (edit.right[static_cast<size_t>(index)] - right[i]);
        break;
      }
    }
  }

  double playbackTime() const noexcept { return playbackTime_; }
  bool playbackValid() const noexcept { return playbackValid_; }

private:
  void updatePlaybackTime(double seconds, bool valid) noexcept {
    playbackTime_ = valid ? seconds : playbackTime_;
    playbackValid_ = valid;
  }

  float sampleRate_ = 48000.0f;
  int capacity_ = 1;
  std::vector<float> left_, right_;
  int write_ = 0;
  int count_ = 0;
  double captureStartTime_ = 0.0;
  bool captureStartValid_ = false;
  bool capturing_ = false;
  bool frozen_ = false;
  double playbackTime_ = 0.0;
  bool playbackValid_ = false;
  std::vector<Edit> edits_;
};

} // namespace repair
