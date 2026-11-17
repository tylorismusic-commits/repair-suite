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
#include <hdsp/envelope.hpp>
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
    frameIndex_ = 2;
    denoise_.prepare(sr_, stft_.bins(), stft_.hopSize());
    dereverb_.prepare(sr_, stft_.bins(), stft_.hopSize());
    capture_.prepare(sr_, 30.0f);
    inputGain_.setTime(15.0f, sr_);
    outputGain_.setTime(15.0f, sr_);
    inputGain_.reset(1.0f);
    outputGain_.reset(1.0f);
    for (auto& f : dc_) {
      f.reset();
    }
    updateDryDelay();
    dryBlockL_.assign(16384, 0.0f);
    dryBlockR_.assign(16384, 0.0f);
    stageL_.assign(16384, 0.0f);
    stageR_.assign(16384, 0.0f);
    stftDryReL_.assign(static_cast<size_t>(stft_.bins()), 0.0f);
    stftDryImL_.assign(static_cast<size_t>(stft_.bins()), 0.0f);
    stftDryReR_.assign(static_cast<size_t>(stft_.bins()), 0.0f);
    stftDryImR_.assign(static_cast<size_t>(stft_.bins()), 0.0f);
    reset();
  }

  void setControls(const Controls& c) noexcept {
    controls_ = c;
    inputGain_.setTarget(hdsp::dbToGain(c.inputDb));
    outputGain_.setTarget(hdsp::dbToGain(c.outputDb));
    if (c.analyze) {
      analyzer_.start();
    }
    const int frameIndex = std::clamp(c.noiseFrame, 0, 3);
    if (frameIndex != frameIndex_) {
      frameIndex_ = frameIndex;
      const int frame = 512 << frameIndex_;
      stft_.prepare(sr_, frame, frame / 4);
      denoise_.retune(sr_, stft_.bins(), stft_.hopSize());
      dereverb_.prepare(sr_, stft_.bins(), stft_.hopSize());
      updateDryDelay();
    }
    declip_.setControls(c);
    declick_.setControls(c);
    dehum_.setControls(c);
    for (auto& f : dc_) {
      f.set(c.dcHz, sr_);
    }
    denoise_.setControls(c);
    dereverb_.setControls(c);
    capture_.setArmed(c.captureArm);
    if (c.captureFreeze) {
      capture_.requestFreeze();
    }
    if (c.clearCapture) {
      capture_.reset();
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
    for (auto& f : dc_) {
      f.reset();
    }
    updateDryDelay();
    dryBlockL_.assign(16384, 0.0f);
    dryBlockR_.assign(16384, 0.0f);
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

    // Capture the dry signal for Delta monitoring.  The ring is read one
    // sample ahead so its output is delayed by the full chain latency.
    const int dryFrames = std::min(frames, static_cast<int>(dryBlockL_.size()));
    for (int i = 0; i < dryFrames; ++i) {
      dryBlockL_[static_cast<size_t>(i)] = left[i];
      dryBlockR_[static_cast<size_t>(i)] = right[i];
    }

    // Per-stage audition: copy the stage input, run the stage, then replace
    // its output with the removed difference.  Downstream modules still run,
    // so the audition is heard through the same tail as the main chain.
    const int n = std::min(frames, static_cast<int>(stageL_.size()));
    auto copyStageInput = [&]() {
      for (int i = 0; i < n; ++i) {
        stageL_[static_cast<size_t>(i)] = left[i];
        stageR_[static_cast<size_t>(i)] = right[i];
      }
    };
    auto makeStageDifference = [&]() {
      for (int i = 0; i < n; ++i) {
        left[i] -= stageL_[static_cast<size_t>(i)];
        right[i] -= stageR_[static_cast<size_t>(i)];
      }
    };

    // De-Clip runs first, directly on the input, before any filtering.  It
    // always runs, even when bypassed, so the reported latency stays exact.
    copyStageInput();
    declip_.process(left, right, frames);
    if (controls_.audition == 0) {
      makeStageDifference();
    }

    // DC / rumble filter, after De-Clip and before the detectors.
    if (controls_.dcOn && controls_.power) {
      for (int i = 0; i < frames; ++i) {
        left[i] = dc_[0].process(left[i]);
        right[i] = dc_[1].process(right[i]);
      }
    }

    // De-Click runs next, before hum and broadband cleanup.
    copyStageInput();
    declick_.process(left, right, frames);
    if (controls_.audition == 1) {
      makeStageDifference();
    }

    // De-Hum is zero latency, so it does not add to the reported total.
    copyStageInput();
    dehum_.process(left, right, frames);
    if (controls_.audition == 2) {
      makeStageDifference();
    }

    // De-Noise and De-Reverb share the STFT pass.
    stft_.process(left, right, frames,
                  [this](float* reL, float* imL, float* reR, float* imR,
                         int bins) {
                    const size_t nb = static_cast<size_t>(bins);
                    if (stftDryReL_.size() < nb) {
                      stftDryReL_.resize(nb);
                      stftDryImL_.resize(nb);
                      stftDryReR_.resize(nb);
                      stftDryImR_.resize(nb);
                    }
                    if (controls_.audition == 3) {
                      std::copy(reL, reL + bins, stftDryReL_.begin());
                      std::copy(imL, imL + bins, stftDryImL_.begin());
                      std::copy(reR, reR + bins, stftDryReR_.begin());
                      std::copy(imR, imR + bins, stftDryImR_.begin());
                      denoise_.processBins(reL, imL, reR, imR, bins);
                      for (int k = 0; k < bins; ++k) {
                        reL[k] = stftDryReL_[static_cast<size_t>(k)] - reL[k];
                        imL[k] = stftDryImL_[static_cast<size_t>(k)] - imL[k];
                        reR[k] = stftDryReR_[static_cast<size_t>(k)] - reR[k];
                        imR[k] = stftDryImR_[static_cast<size_t>(k)] - imR[k];
                      }
                      return;
                    }
                    denoise_.processBins(reL, imL, reR, imR, bins);
                    if (controls_.audition == 4) {
                      std::copy(reL, reL + bins, stftDryReL_.begin());
                      std::copy(imL, imL + bins, stftDryImL_.begin());
                      std::copy(reR, reR + bins, stftDryReR_.begin());
                      std::copy(imR, imR + bins, stftDryImR_.begin());
                      dereverb_.processBins(reL, imL, reR, imR, bins);
                      for (int k = 0; k < bins; ++k) {
                        reL[k] = stftDryReL_[static_cast<size_t>(k)] - reL[k];
                        imL[k] = stftDryImL_[static_cast<size_t>(k)] - imL[k];
                        reR[k] = stftDryReR_[static_cast<size_t>(k)] - reR[k];
                        imR[k] = stftDryImR_[static_cast<size_t>(k)] - imR[k];
                      }
                      return;
                    }
                    dereverb_.processBins(reL, imL, reR, imR, bins);
                  });

    // The spectral tap is after De-Reverb and before Output trim, so the
    // editor sees exactly what the final chain produces.
    capture_.push(left, right, frames, hostTime, hostTimeValid);
    copyStageInput();
    if (controls_.specOn && controls_.power) {
      capture_.applyOverlay(left, right, frames, hostTime, hostTimeValid);
    }
    if (controls_.audition == 5) {
      makeStageDifference();
    }

    const float outGain = outputGain_.next();
    const bool monitorInput = controls_.monitor == 1 || controls_.compare;
    const bool monitorDelta = !monitorInput && controls_.audition < 0 &&
                              (controls_.delta || controls_.monitor == 2);
    for (int i = 0; i < frames; ++i) {
      left[i] *= outGain;
      right[i] *= outGain;
      if (drySize_ > 0 && i < static_cast<int>(dryBlockL_.size())) {
        const int read = (dryWrite_ + 1) % drySize_;
        const float dryL = dryL_[static_cast<size_t>(read)];
        const float dryR = dryR_[static_cast<size_t>(read)];
        dryL_[static_cast<size_t>(dryWrite_)] = dryBlockL_[static_cast<size_t>(i)];
        dryR_[static_cast<size_t>(dryWrite_)] = dryBlockR_[static_cast<size_t>(i)];
        dryWrite_ = (dryWrite_ + 1) % drySize_;
        if (monitorInput && controls_.power) {
          left[i] = dryL * outGain;
          right[i] = dryR * outGain;
        } else if (monitorDelta && controls_.power) {
          left[i] -= dryL * outGain;
          right[i] -= dryR * outGain;
        }
      }
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
    meters.clipActivity = declip_.lastClipCount() > 0 ? 1.0f : 0.0f;
    meters.clickEvents = declick_.lastClicks();
    meters.humDetectedHz = dehum_.detectedHz();
    const auto& clicks = declick_.lastEvents();
    meters.clickEventCount = std::min(static_cast<int>(clicks.size()), 8);
    for (int i = 0; i < meters.clickEventCount; ++i) {
      meters.clickEventList[i].timeSec =
          static_cast<float>(hostTime) +
          static_cast<float>(clicks[static_cast<size_t>(i)].offsetSamples) / sr_;
      meters.clickEventList[i].widthMs =
          clicks[static_cast<size_t>(i)].widthMs;
      meters.clickEventList[i].peakDb =
          clicks[static_cast<size_t>(i)].peakDb;
    }
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
  void updateDryDelay() noexcept {
    dryLatency_ = static_cast<int>(latencySamples());
    drySize_ = dryLatency_ + 1;
    dryL_.assign(static_cast<size_t>(drySize_), 0.0f);
    dryR_.assign(static_cast<size_t>(drySize_), 0.0f);
    dryWrite_ = 0;
  }

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
  hdsp::HighPass2 dc_[2];
  int dryLatency_ = 0;
  int drySize_ = 1;
  std::vector<float> dryL_, dryR_;
  std::vector<float> dryBlockL_, dryBlockR_;
  std::vector<float> stageL_, stageR_;
  std::vector<float> stftDryReL_, stftDryImL_, stftDryReR_, stftDryImR_;
  int dryWrite_ = 0;
  int frameIndex_ = 2;
};

} // namespace repair
