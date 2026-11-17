// Repair Suite — DSP smoke tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/declick.hpp"
#include "dsp/engine.hpp"
#include "dsp/spectral_capture.hpp"
#include "dsp/stft.hpp"
#include "params.hpp"
#include "presets.hpp"

namespace {

constexpr float kSr = 48000.0f;
constexpr double kPi = 3.14159265358979323846;

float sine(int n, double hz, float amp = 1.0f) {
  return amp * static_cast<float>(std::sin(2.0 * kPi * hz * n / kSr));
}

void testStftIdentity() {
  repair::Stft stft;
  stft.prepare(kSr, 1024, 256);
  const int latency = stft.latencySamples();
  assert(latency == 1023);

  const int n = 12000;
  std::vector<float> inL(n), inR(n), outL(n), outR(n);
  for (int i = 0; i < n; ++i) {
    inL[i] = inR[i] = sine(i, 440.0, 0.5f);
  }
  std::copy(inL.begin(), inL.end(), outL.begin());
  std::copy(inR.begin(), inR.end(), outR.begin());
  for (int start = 0; start < n; start += 128) {
    const int len = std::min(128, n - start);
    stft.process(outL.data() + start, outR.data() + start, len);
  }
  double err = 0.0;
  int count = 0;
  for (int i = latency + 2 * 1024; i < n; ++i) {
    err += std::fabs(outL[i] - inL[i - latency]);
    ++count;
  }
  const double mean = count > 0 ? err / count : 1.0;
  std::printf("STFT identity mean error %.6f\n", mean);
  assert(mean < 2.0e-4);
}

void testStftMask() {
  repair::Stft stft;
  stft.prepare(kSr, 1024, 256);
  const int latency = stft.latencySamples();
  const int n = 12000;
  std::vector<float> l(n), r(n);
  for (int i = 0; i < n; ++i) {
    l[i] = r[i] = sine(i, 1000.0, 0.5f);
  }
  auto mask = [](float* reL, float* imL, float* reR, float* imR, int bins) {
    for (int k = 0; k < bins; ++k) {
      reL[k] = imL[k] = 0.0f;
      reR[k] = imR[k] = 0.0f;
    }
  };
  for (int start = 0; start < n; start += 128) {
    const int len = std::min(128, n - start);
    stft.process(l.data() + start, r.data() + start, len, mask);
  }
  double peak = 0.0;
  for (int i = latency + 2 * 1024; i < n; ++i) {
    peak = std::max(peak, static_cast<double>(std::fabs(l[i])));
  }
  std::printf("STFT masked peak %.6f\n", peak);
  assert(peak < 1.0e-3);
}

void testCaptureOverlay() {
  repair::SpectralCapture cap;
  cap.prepare(kSr, 1.0f);
  cap.setArmed(true);

  const int n = 4800;
  std::vector<float> l(n), r(n);
  for (int i = 0; i < n; ++i) {
    l[i] = r[i] = 0.25f;
  }
  cap.push(l.data(), r.data(), n, 10.0, true);
  assert(cap.capturedFrames() == n);
  assert(std::fabs(cap.captureStartTime() - 10.0) < 1.0e-9);

  std::vector<float> cl, cr;
  assert(cap.copyCaptured(&cl, &cr) == n);
  assert(std::fabs(cl[0] - 0.25f) < 1.0e-6f);

  // Replace 10.1..10.2 s with zeros and check the overlay.
  std::vector<float> editL(4800, 0.0f), editR(4800, 0.0f);
  assert(cap.addEdit(10.1, 10.2, editL.data(), editR.data(), 4800));
  std::vector<float> playL(4800, 0.25f), playR(4800, 0.25f);
  cap.applyOverlay(playL.data(), playR.data(), 4800, 10.1, true);
  assert(std::fabs(playL[2400]) < 1.0e-6f);
  // Before the edit the live signal is untouched.
  std::vector<float> beforeL(4800, 0.25f), beforeR(4800, 0.25f);
  cap.applyOverlay(beforeL.data(), beforeR.data(), 4800, 10.0, true);
  assert(std::fabs(beforeL[100] - 0.25f) < 1.0e-6f);
  assert(cap.editCount() == 1);
}


double sdrAt(const std::vector<float>& out, const std::vector<float>& orig,
             int delay) {
  double sig = 0.0;
  double err = 0.0;
  for (int i = delay + 4096; i < static_cast<int>(out.size()); ++i) {
    sig += static_cast<double>(orig[static_cast<size_t>(i - delay)]) *
           orig[static_cast<size_t>(i - delay)];
    const double e = out[static_cast<size_t>(i)] -
                     orig[static_cast<size_t>(i - delay)];
    err += e * e;
  }
  return 10.0 * std::log10(sig / (err + 1.0e-20));
}

void testDeclip() {
  const int n = 24000;
  std::vector<float> orig(n), clipped(n);
  for (int i = 0; i < n; ++i) {
    orig[i] = 0.42f * sine(i, 220.0) + 0.28f * sine(i, 440.0) +
              0.18f * sine(i, 1330.0);
  }
  const float threshold = 0.45f;
  for (int i = 0; i < n; ++i) {
    clipped[i] = std::clamp(orig[i], -threshold, threshold);
  }
  const double thresholdDb = 20.0 * std::log10(threshold);
  double sdr[3] = {};
  for (int mode = 0; mode < 3; ++mode) {
    repair::Engine engine;
    engine.prepare(kSr);
    repair::Meters meters;
    repair::Controls c;
    c.clipOn = true;
    c.clipAuto = false;
    c.clipUpperDb = static_cast<float>(thresholdDb);
    c.clipLowerDb = static_cast<float>(thresholdDb);
    c.clipMode = mode;
    c.clipIterations = 12;
    engine.setControls(c);
    std::vector<float> l = clipped;
    std::vector<float> r = clipped;
    for (int start = 0; start < n; start += 256) {
      const int len = std::min(256, n - start);
      engine.process(l.data() + start, r.data() + start, len, meters,
                     start / static_cast<double>(kSr), true);
    }
    sdr[mode] = sdrAt(l, orig, 4221);
  }
  std::printf("declip SDR High %.2f Balanced %.2f Fast %.2f dB\n", sdr[0],
              sdr[1], sdr[2]);
  assert(sdr[0] > sdr[2] + 1.0);
  assert(sdr[0] > 12.0);
}

void testEngine() {
  repair::Engine engine;
  engine.prepare(kSr);
  assert(engine.latencySamples() == 4221);
  repair::Meters meters;
  repair::Controls c;
  c.inputDb = 0.0f;
  c.outputDb = 0.0f;
  engine.setControls(c);

  const int n = 16000;
  std::vector<float> l(n), inL(n), r(n), inR(n);
  for (int i = 0; i < n; ++i) {
    inL[i] = inR[i] = sine(i, 220.0, 0.2f);
    l[i] = inL[i];
    r[i] = inR[i];
  }
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    engine.process(l.data() + start, r.data() + start, len, meters,
                   start / static_cast<double>(kSr), true);
  }
  bool finite = true;
  double err = 0.0;
  for (int i = 4221 + 4096; i < n; ++i) {
    finite = finite && std::isfinite(l[i]);
    err += std::fabs(l[i] - inL[i - 4221]);
  }
  std::printf("engine finite=%d mean error %.6f\n", finite,
              err / (n - 4221 - 4096));
  assert(finite);
  assert(err / (n - 4221 - 4096) < 2.0e-4);
  assert(std::isfinite(meters.outPeakDb));
}


void testDeclick() {
  const int n = 12000;
  std::vector<float> clean(n), noisy(n);
  for (int i = 0; i < n; ++i) {
    clean[i] = 0.2f * sine(i, 220.0);
    noisy[i] = clean[i];
  }
  for (int p : {2000, 5000, 8000}) {
    noisy[static_cast<size_t>(p)] += 0.8f;
  }
  repair::Declick declick;
  declick.prepare(kSr);
  repair::Controls c;
  c.clickOn = true;
  c.clickLink = true;
  c.clickSensitivity = 5.0f;
  c.clickWidthMs = 5.0f;
  declick.setControls(c);
  std::vector<float> l = noisy;
  std::vector<float> r = noisy;
  declick.process(l.data(), r.data(), n);
  const int latency = declick.latencySamples();
  assert(latency == 639);
  std::printf("declick latency %d count %d\n", latency, declick.clickCount());
  assert(declick.clickCount() >= 3);
  for (int p : {2000, 5000, 8000}) {
    const int at = p + latency;
    assert(std::fabs(l[static_cast<size_t>(at)] -
                     clean[static_cast<size_t>(p)]) < 0.06f);
  }

  // A clean sine must not be treated as clicks.
  repair::Declick cleanDeclick;
  cleanDeclick.prepare(kSr);
  cleanDeclick.setControls(c);
  std::vector<float> cl = clean;
  std::vector<float> cr = clean;
  cleanDeclick.process(cl.data(), cr.data(), n);
  assert(cleanDeclick.clickCount() == 0);
  const int at = latency + 2000;
  assert(std::fabs(cl[static_cast<size_t>(at)] -
                   clean[static_cast<size_t>(at - latency)]) < 1.0e-5f);
}

void testPresets() {
  for (const repair::Preset& p : repair::factoryPresets()) {
    repair::Shared s;
    for (uint32_t i = 0; i < repair::P_COUNT; ++i) {
      s.setFromHost(i, p.values[i]);
    }
    repair::Engine engine;
    engine.prepare(kSr);
    engine.setControls(repair::controlsFrom(s));
    repair::Meters meters;
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 256; ++i) {
      l[i] = r[i] = sine(i, 440.0, 0.2f);
    }
    engine.process(l.data(), r.data(), 256, meters);
    for (int i = 0; i < 256; ++i) {
      assert(std::isfinite(l[i]) && std::isfinite(r[i]));
    }
  }
}

} // namespace

int main() {
  testStftIdentity();
  testStftMask();
  testCaptureOverlay();
  testEngine();
  testDeclip();
  testDeclick();
  testPresets();
  std::printf("DSP tests passed\n");
  return 0;
}
