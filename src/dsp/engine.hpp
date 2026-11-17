// Repair Suite — engine (Phase 0 scaffold)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chain is scaffolded but only the shared STFT pass-through, the spectral
// capture prototype and the gain stages are active.  Each module will be
// inserted in the order documented in docs/RESEARCH_AND_PLAN.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <hdsp/db.hpp>
#include <hdsp/smoother.hpp>

#include "controls.hpp"
#include "declip.hpp"
#include "spectral_capture.hpp"
#include "stft.hpp"

namespace repair {

class Engine {
public:
  void prepare(float sampleRate, int /*maxBlock*/ = 0) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    declip_.prepare(sr_);
    stft_.prepare(sr_, 2048, 512);
    capture_.prepare(sr_, 30.0f);
    inputGain_.setTime(15.0f, sr_);
    outputGain_.setTime(15.0f, sr_);
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
    captureArmed_ = false;
    noiseLearn_ = false;
    reset();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    inputGain_.setTarget(hdsp::dbToGain(c.inputDb));
    outputGain_.setTarget(hdsp::dbToGain(c.outputDb));
    declip_.setControls(c);
    capture_.setArmed(c.captureArm);
    if (c.captureFreeze) {
      capture_.freeze();
    }
    if (c.clearCapture) {
      capture_.reset();
      declip_.setControls(c);
    capture_.setArmed(c.captureArm);
    }
    if (c.clearEdits) {
      capture_.clearEdits();
    }
    captureArmed_ = c.captureArm;
    noiseLearn_ = c.noiseLearn;
  }

  // CLAP/LV2 compatibility.  The scaffold has no tempo-synced module yet.
  void setTempo(float /*bpm*/, bool /*host*/) noexcept {}

  void reset() noexcept {
    declip_.reset();
    stft_.reset();
    capture_.reset();
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
  }

  uint32_t latencySamples() const noexcept {
    return static_cast<uint32_t>(stft_.latencySamples() + declip_.latencySamples());
  }

  void process(float* left, float* right, int frames, Meters& meters,
               double hostTime = 0.0, bool hostTimeValid = false) noexcept {
    if (!left || !right || frames <= 0) {
      return;
    }
    meters = Meters{};
    float peakIn = 0.0f;
    float peakOut = 0.0f;
    for (int i = 0; i < frames; ++i) {
      peakIn = std::max(peakIn, std::max(std::fabs(left[i]),
                                         std::fabs(right[i])));
    }

    const float inGain = inputGain_.next();
    for (int i = 0; i < frames; ++i) {
      left[i] *= inGain;
      right[i] *= inGain;
    }

    // De-Clip runs first, directly on the input, before any filtering.  It
    // always runs, even when bypassed, so the reported latency stays exact.
    declip_.process(left, right, frames);

    // Capture is a Phase 0 placeholder at the input tap.  The production tap
    // is after De-Reverb, before Output trim.
    capture_.push(left, right, frames, hostTime, hostTimeValid);
    capture_.applyOverlay(left, right, frames, hostTime, hostTimeValid);

    // The scaffold chain is an identity STFT.  Modules will be inserted here;
    // running it now locks in latency reporting and reconstruction tests.
    stft_.process(left, right, frames);

    const float outGain = outputGain_.next();
    for (int i = 0; i < frames; ++i) {
      left[i] *= outGain;
      right[i] *= outGain;
      peakOut = std::max(peakOut, std::max(std::fabs(left[i]),
                                           std::fabs(right[i])));
    }

    meters.inPeakDb = hdsp::gainToDb(peakIn);
    meters.outPeakDb = hdsp::gainToDb(peakOut);
    meters.inPeakLDb = meters.inPeakDb;
    meters.inPeakRDb = meters.inPeakDb;
    meters.outPeakLDb = meters.outPeakDb;
    meters.outPeakRDb = meters.outPeakDb;
    meters.grDb = 0.0f;
    meters.noiseGrDb = 0.0f;
    meters.verbGrDb = 0.0f;
    meters.specActive = capture_.editCount() > 0 ? 1.0f : 0.0f;
    meters.noiseFloorDb = meters.inPeakDb;
    meters.captureSeconds = capture_.capturedSeconds();
    meters.captureComplete = capture_.frozen() ? 1.0f : 0.0f;
    meters.transportSeconds = hostTime;
    meters.transportValid = hostTimeValid;
    meters.editCount = capture_.editCount();
  }

  SpectralCapture& capture() noexcept { return capture_; }
  const SpectralCapture& capture() const noexcept { return capture_; }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  bool captureArmed_ = false;
  bool noiseLearn_ = false;
  Declip declip_;
  Stft stft_;
  SpectralCapture capture_;
  hdsp::Smoothed inputGain_;
  hdsp::Smoothed outputGain_;
};

} // namespace repair
