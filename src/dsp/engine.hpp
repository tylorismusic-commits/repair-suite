// Repair Suite — engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fixed chain: De-Clip -> De-Click -> De-Hum -> De-Noise -> De-Reverb ->
// shared STFT -> Output.  The spectral capture/overlay and preview live in
// SpectralCapture; the editor talks to it through the SpectralBridge pointer.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <hdsp/db.hpp>
#include <hdsp/smoother.hpp>

#include "analyze.hpp"
#include "controls.hpp"
#include "declip.hpp"
#include "declick.hpp"
#include "dehum.hpp"
#include "denoise.hpp"
#include "dereverb.hpp"
#include "spectral_capture.hpp"
#include "stft.hpp"

namespace repair {

class Engine {
public:
  void prepare(float sampleRate, int /*maxBlock*/ = 0) noexcept {
    sr_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    analyzer_.prepare(sr_);
    declip_.prepare(sr_);
    declick_.prepare(sr_);
    dehum_.prepare(sr_);
    stft_.prepare(sr_, 2048, 512);
    denoise_.prepare(sr_, stft_.bins(), stft_.hopSize());
    dereverb_.prepare(sr_, stft_.bins(), stft_.hopSize());
    capture_.prepare(sr_, 30.0f);
    inputGain_.setTime(15.0f, sr_);
    outputGain_.setTime(15.0f, sr_);
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
    reset();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    inputGain_.setTarget(hdsp::dbToGain(c.inputDb));
    outputGain_.setTarget(hdsp::dbToGain(c.outputDb));
    if (c.analyze) {
      analyzer_.start();
    }
    declip_.setControls(c);
    declick_.setControls(c);
    dehum_.setControls(c);
    denoise_.setControls(c);
    dereverb_.setControls(c);
    capture_.setArmed(c.captureArm);
    if (c.captureFreeze) {
      capture_.requestFreeze();
    }
    if (c.clearCapture) {
      capture_.reset();
      if (c.analyze) {
      analyzer_.start();
    }
    declip_.setControls(c);
    declick_.setControls(c);
    dehum_.setControls(c);
    denoise_.setControls(c);
    dereverb_.setControls(c);
    capture_.setArmed(c.captureArm);
    }
    if (c.clearEdits) {
      capture_.clearEdits();
    }
  }

  // CLAP/LV2 compatibility.  There is no tempo-synced module yet.
  void setTempo(float /*bpm*/, bool /*host*/) noexcept {}

  void reset() noexcept {
    analyzer_.reset();
    declip_.reset();
    declick_.reset();
    dehum_.reset();
    denoise_.reset();
    dereverb_.reset();
    stft_.reset();
    capture_.reset();
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
  }

  uint32_t latencySamples() const noexcept {
    return static_cast<uint32_t>(stft_.latencySamples() +
                                 declip_.latencySamples() +
                                 declick_.latencySamples());
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

    analyzer_.process(left, right, frames);
    const float inGain = inputGain_.next();
    for (int i = 0; i < frames; ++i) {
      left[i] *= inGain;
      right[i] *= inGain;
    }

    // De-Clip runs first, directly on the input, before any filtering.  It
    // always runs, even when bypassed, so the reported latency stays exact.
    declip_.process(left, right, frames);

    // De-Click runs next, before hum and broadband cleanup.
    declick_.process(left, right, frames);

    // De-Hum is zero latency, so it does not add to the reported total.
    dehum_.process(left, right, frames);

    // De-Noise and De-Reverb share the STFT pass.
    stft_.process(left, right, frames,
                  [this](float* reL, float* imL, float* reR, float* imR,
                         int bins) {
                    denoise_.processBins(reL, imL, reR, imR, bins);
                    dereverb_.processBins(reL, imL, reR, imR, bins);
                  });

    // The spectral tap is after De-Reverb and before Output trim, so the
    // editor sees exactly what the final chain produces.
    capture_.push(left, right, frames, hostTime, hostTimeValid);
    capture_.applyOverlay(left, right, frames, hostTime, hostTimeValid);

    const float outGain = outputGain_.next();
    for (int i = 0; i < frames; ++i) {
      left[i] *= outGain;
      right[i] *= outGain;
      peakOut = std::max(peakOut, std::max(std::fabs(left[i]),
                                           std::fabs(right[i])));
    }

    if (capture_.readPreview(left, right, frames)) {
      peakOut = 0.0f;
      for (int i = 0; i < frames; ++i) {
        peakOut = std::max(peakOut, std::max(std::fabs(left[i]),
                                             std::fabs(right[i])));
      }
    }

    meters.inPeakDb = hdsp::gainToDb(peakIn);
    meters.outPeakDb = hdsp::gainToDb(peakOut);
    meters.inPeakLDb = meters.inPeakDb;
    meters.inPeakRDb = meters.inPeakDb;
    meters.outPeakLDb = meters.outPeakDb;
    meters.outPeakRDb = meters.outPeakDb;
    meters.grDb = 0.0f;
    meters.noiseGrDb = denoise_.lastReductionDb();
    meters.verbGrDb = dereverb_.lastReductionDb();
    meters.specActive = capture_.editCount() > 0 ? 1.0f : 0.0f;
    meters.noiseFloorDb = meters.inPeakDb;
    meters.captureSeconds = capture_.capturedSeconds();
    meters.captureComplete = capture_.frozen() ? 1.0f : 0.0f;
    meters.transportSeconds = hostTime;
    meters.transportValid = hostTimeValid;
    meters.editCount = capture_.editCount();
    const auto& ar = analyzer_.result();
    meters.analysisReady = analyzer_.ready();
    meters.analysisProgress = analyzer_.progress();
    meters.analysisHumHz = ar.humHz;
    meters.analysisHumDb = ar.humDb;
    meters.analysisNoiseDb = ar.noiseDb;
    meters.analysisClipPct = ar.clipPct;
    meters.analysisClickRate = ar.clickRate;
    meters.analysisReverbSec = ar.reverbSec;
  }

  SpectralCapture& capture() noexcept { return capture_; }
  const SpectralCapture& capture() const noexcept { return capture_; }

  bool takeProfileReady() noexcept { return denoise_.takeProfileReady(); }
  int copyNoiseProfile(float* out, int maxBins) const noexcept {
    return denoise_.copyProfile(out, maxBins);
  }
  bool loadNoiseProfile(const float* data, int bins) noexcept {
    return denoise_.loadProfile(data, bins);
  }

private:
  float sr_ = 48000.0f;
  Controls controls_;
  Analyzer analyzer_;
  Declip declip_;
  Declick declick_;
  Dehum dehum_;
  Denoise denoise_;
  Dereverb dereverb_;
  Stft stft_;
  SpectralCapture capture_;
  hdsp::Smoothed inputGain_;
  hdsp::Smoothed outputGain_;
};

} // namespace repair
