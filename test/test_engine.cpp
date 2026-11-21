// Repair Suite — DSP smoke tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/declick.hpp"
#include "dsp/dehum.hpp"
#include "dsp/denoise.hpp"
#include "dsp/dereverb.hpp"
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

double rmsRange(const std::vector<float>& x, int start, int count) {
  double sum = 0.0;
  for (int i = start; i < start + count; ++i) {
    sum += static_cast<double>(x[i]) * x[i];
  }
  return std::sqrt(sum / std::max(1, count));
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

  // Hosts without a transport timeline must still fill the capture buffer so
  // the spectrogram can show the input.
  {
    repair::SpectralCapture noTransport;
    noTransport.prepare(kSr, 1.0f);
    noTransport.setArmed(true);
    noTransport.push(l.data(), r.data(), n, 0.0, false);
    assert(noTransport.capturedFrames() == n);
  }

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

  // A later edit must win in an overlap, matching preview/diff playback.
  std::vector<float> laterL(4800, 0.5f), laterR(4800, 0.5f);
  assert(cap.addEdit(10.1, 10.2, laterL.data(), laterR.data(), 4800));
  std::vector<float> overlapL(4800, 0.25f), overlapR(4800, 0.25f);
  cap.applyOverlay(overlapL.data(), overlapR.data(), 4800, 10.1, true);
  assert(std::fabs(overlapL[2400] - 0.5f) < 1.0e-6f);
  assert(cap.editCount() == 2);

  std::vector<repair::SpectralCapture::Edit> stateEdits;
  cap.copyEditsForState(&stateEdits);
  assert(stateEdits.size() == 2);
  repair::SpectralCapture restoredCapture;
  restoredCapture.prepare(kSr, 1.0f);
  restoredCapture.replaceEditsFromState(stateEdits);
  assert(restoredCapture.editCount() == 2);
  std::vector<repair::SpectralCapture::Edit> restoredEdits;
  restoredCapture.copyEditsForState(&restoredEdits);
  assert(restoredEdits.front().left.size() == stateEdits.front().left.size());
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


double toneMag(const std::vector<float>& x, float hz, int start, int count) {
  double re = 0.0;
  double im = 0.0;
  for (int i = 0; i < count; ++i) {
    const double phase = 2.0 * kPi * hz * (start + i) / kSr;
    re += x[static_cast<size_t>(start + i)] * std::cos(phase);
    im -= x[static_cast<size_t>(start + i)] * std::sin(phase);
  }
  return std::sqrt(re * re + im * im) / (0.5 * count);
}


void testDeclickModes() {
  const int n = 12000;
  std::vector<float> clean(n), in(n);
  for (int i = 0; i < n; ++i) {
    clean[i] = 0.2f * sine(i, 1000.0);
    in[i] = clean[i];
  }
  for (int i = 0; i < 10; ++i) {
    in[static_cast<size_t>(5000 + i)] = 0.0f;
  }
  repair::Declick dropout;
  dropout.prepare(kSr);
  repair::Controls c;
  c.clickOn = true;
  c.clickLink = true;
  c.clickSensitivity = 5.0f;
  c.clickWidthMs = 20.0f;
  c.clickMode = 3;
  dropout.setControls(c);
  std::vector<float> l = in;
  std::vector<float> r = in;
  dropout.process(l.data(), r.data(), n);
  assert(dropout.clickCount() > 0);
  const int at = 5000 + dropout.latencySamples();
  for (int i = 0; i < 10; ++i) {
    assert(std::fabs(l[static_cast<size_t>(at + i)] -
                     clean[static_cast<size_t>(5000 + i)]) < 0.05f);
  }

  // Thump mode is a distinct low-frequency detector; it must stay finite and
  // detect the synthetic low-frequency event.
  std::vector<float> thump = clean;
  for (int i = 0; i < 200; ++i) {
    thump[static_cast<size_t>(3000 + i)] +=
        0.7f * std::exp(-static_cast<float>(i) / 50.0f);
  }
  repair::Declick thumpDeclick;
  thumpDeclick.prepare(kSr);
  c.clickMode = 2;
  thumpDeclick.setControls(c);
  std::vector<float> tl = thump;
  std::vector<float> tr = thump;
  thumpDeclick.process(tl.data(), tr.data(), n);
  assert(thumpDeclick.clickCount() > 0);
  for (float v : tl) {
    assert(std::isfinite(v));
  }
}

void testDehum() {
  const int n = 96000;
  std::vector<float> in(n), l, r;
  for (int i = 0; i < n; ++i) {
    in[i] = 0.30f * sine(i, 50.0) + 0.20f * sine(i, 150.0) +
            0.15f * sine(i, 250.0) + 0.20f * sine(i, 1000.0);
  }
  repair::Dehum dehum;
  dehum.prepare(kSr);
  repair::Controls c;
  c.humOn = true;
  c.humAuto = false;
  c.humHz = 50.0f;
  c.humHarmonics = 5;
  c.humDepthDb = 60.0f;
  c.humMix = 100.0f;
  dehum.setControls(c);
  l = in;
  r = in;
  dehum.process(l.data(), r.data(), n);
  const int start = 48000;
  const int count = 24000;
  const double humIn = toneMag(in, 50.0f, start, count);
  const double humOut = toneMag(l, 50.0f, start, count);
  const double wantedIn = toneMag(in, 1000.0f, start, count);
  const double wantedOut = toneMag(l, 1000.0f, start, count);
  std::printf("dehum 50 Hz %.1f dB, 1 kHz %.2f dB\n",
              20.0 * std::log10(humOut / humIn),
              20.0 * std::log10(wantedOut / wantedIn));
  assert(20.0 * std::log10(humOut / humIn) < -20.0);
  assert(std::fabs(20.0 * std::log10(wantedOut / wantedIn)) < 0.5);

  // Precision mode cancels the same harmonic series while preserving 1 kHz.
  {
    repair::Dehum precision;
    precision.prepare(kSr);
    c.humPrecision = true;
    precision.setControls(c);
    std::vector<float> pl = in;
    std::vector<float> pr = in;
    precision.process(pl.data(), pr.data(), n);
    const double pIn = toneMag(in, 50.0f, start, count);
    const double pOut = toneMag(pl, 50.0f, start, count);
    const double wIn = toneMag(in, 1000.0f, start, count);
    const double wOut = toneMag(pl, 1000.0f, start, count);
    std::printf("precision hum 50 Hz %.1f dB, 1 kHz %.2f dB\n",
                20.0 * std::log10(pOut / pIn),
                20.0 * std::log10(wOut / wIn));
    assert(20.0 * std::log10(pOut / pIn) < -20.0);
    assert(std::fabs(20.0 * std::log10(wOut / wIn)) < 1.0);
  }

  // Auto tune finds 60 Hz and rejects a clean signal as "no hum".
  std::vector<float> hum(n);
  for (int i = 0; i < n; ++i) {
    hum[i] = 0.3f * sine(i, 60.0) + 0.2f * sine(i, 180.0);
  }
  repair::Dehum autoDehum;
  autoDehum.prepare(kSr);
  c.humAuto = true;
  autoDehum.setControls(c);
  std::vector<float> al = hum;
  std::vector<float> ar = hum;
  autoDehum.process(al.data(), ar.data(), n);
  assert(std::fabs(autoDehum.detectedHz() - 60.0f) < 0.5f);
  const double hIn = toneMag(hum, 60.0f, 64000, 24000);
  const double hOut = toneMag(al, 60.0f, 64000, 24000);
  assert(20.0 * std::log10(hOut / hIn) < -20.0);
}


void testDenoise() {
  const int n = 5 * static_cast<int>(kSr);
  auto noise = [](uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(state >> 8)) / 8388608.0f;
  };
  repair::Stft stft;
  stft.prepare(kSr, 2048, 512);
  repair::Denoise denoise;
  denoise.prepare(kSr, stft.bins(), stft.hopSize());
  repair::Controls c;
  c.noiseOn = true;
  c.noiseReductionDb = 24.0f;
  c.noiseSmoothing = 5.0f;
  c.noiseTransient = 5.0f;
  c.noiseMix = 100.0f;
  c.noiseLearn = true;
  denoise.setControls(c);

  uint32_t seed = 1;
  std::vector<float> l(n), r(n);
  for (int i = 0; i < n; ++i) {
    l[i] = r[i] = 0.05f * noise(seed);
  }
  for (int start = 0; start < n; start += 512) {
    const int len = std::min(512, n - start);
    stft.process(l.data() + start, r.data() + start, len,
                 [&](float* reL, float* imL, float* reR, float* imR, int bins) {
                   denoise.processBins(reL, imL, reR, imR, bins);
                 });
  }
  assert(denoise.learned());
  c.noiseLearn = false;
  denoise.setControls(c);

  seed = 2;
  std::vector<float> nl(n), nr(n), orig(n);
  for (int i = 0; i < n; ++i) {
    orig[i] = 0.05f * noise(seed);
    nl[i] = nr[i] = orig[i];
  }
  for (int start = 0; start < n; start += 512) {
    const int len = std::min(512, n - start);
    stft.process(nl.data() + start, nr.data() + start, len,
                 [&](float* reL, float* imL, float* reR, float* imR, int bins) {
                   denoise.processBins(reL, imL, reR, imR, bins);
                 });
  }
  const int start = 3 * static_cast<int>(kSr);
  const int count = static_cast<int>(kSr);
  const double inRms = rmsRange(orig, start, count);
  const double outRms = rmsRange(nl, start, count);
  std::printf("denoise noise %.4f -> %.4f (%.1f dB)\n", inRms, outRms,
              20.0 * std::log10(outRms / inRms));
  assert(outRms < inRms * 0.5);

  // Adaptive mode bootstraps a profile without pressing Learn.
  {
    repair::Stft adaptiveStft;
    adaptiveStft.prepare(kSr, 2048, 512);
    repair::Denoise adaptive;
    adaptive.prepare(kSr, adaptiveStft.bins(), adaptiveStft.hopSize());
    repair::Controls ac;
    ac.noiseOn = true;
    ac.noiseAdaptive = true;
    ac.noiseReductionDb = 20.0f;
    ac.noiseSmoothing = 5.0f;
    ac.noiseMix = 100.0f;
    adaptive.setControls(ac);
    seed = 9;
    std::vector<float> al(n), ar(n), orig(n);
    for (int i = 0; i < n; ++i) {
      orig[i] = 0.05f * noise(seed);
      al[i] = ar[i] = orig[i];
    }
    for (int startBlock = 0; startBlock < n; startBlock += 512) {
      const int len = std::min(512, n - startBlock);
      adaptiveStft.process(
          al.data() + startBlock, ar.data() + startBlock, len,
          [&](float* reL, float* imL, float* reR, float* imR, int bins) {
            adaptive.processBins(reL, imL, reR, imR, bins);
          });
    }
    const double inRms = rmsRange(orig, start, count);
    const double outRms = rmsRange(al, start, count);
    std::printf("adaptive denoise %.1f dB\n",
                20.0 * std::log10(outRms / inRms));
    assert(outRms < inRms * 0.8);
  }

  // A wanted tone survives the learned noise profile.
  seed = 3;
  std::vector<float> tl(n), tr(n), tone(n);
  for (int i = 0; i < n; ++i) {
    tone[i] = 0.2f * sine(i, 1000.0);
    tl[i] = tr[i] = tone[i] + 0.05f * noise(seed);
  }
  for (int startBlock = 0; startBlock < n; startBlock += 512) {
    const int len = std::min(512, n - startBlock);
    stft.process(tl.data() + startBlock, tr.data() + startBlock, len,
                 [&](float* reL, float* imL, float* reR, float* imR, int bins) {
                   denoise.processBins(reL, imL, reR, imR, bins);
                 });
  }
  const double toneIn = toneMag(tone, 1000.0f, start, count);
  const double toneOut = toneMag(tl, 1000.0f, start, count);
  std::printf("denoise tone %.4f -> %.4f\n", toneIn, toneOut);
  assert(std::fabs(20.0 * std::log10(toneOut / toneIn)) < 1.0);
}


void testDereverb() {
  const int sr = static_cast<int>(kSr);
  const int n = 3 * sr;
  std::vector<float> in(static_cast<size_t>(n), 0.0f);
  uint32_t seed = 1;
  auto rnd = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(seed >> 8)) / 8388608.0f;
  };
  for (int b = 0; b < 4; ++b) {
    const int start = b * sr * 3 / 4;
    if (start >= n) {
      break;
    }
    for (int i = 0; i < 960 && start + i < n; ++i) {
      in[static_cast<size_t>(start + i)] += 0.4f * sine(i, 1000.0);
    }
    for (int i = 0; i < 14400 && start + 960 + i < n; ++i) {
      const float env = std::exp(-static_cast<float>(i) / (0.15f * sr));
      in[static_cast<size_t>(start + 960 + i)] += 0.08f * env * rnd();
    }
  }
  repair::Stft stft;
  stft.prepare(kSr, 2048, 512);
  repair::Dereverb dereverb;
  dereverb.prepare(kSr, stft.bins(), stft.hopSize());
  repair::Controls c;
  c.verbOn = true;
  c.verbAmountDb = 12.0f;
  c.verbTailSec = 0.8f;
  c.verbPredelayMs = 30.0f;
  c.verbTransient = 6.0f;
  c.verbTiltDb = 0.0f;
  c.verbMix = 100.0f;
  dereverb.setControls(c);
  std::vector<float> l = in;
  std::vector<float> r = in;
  for (int start = 0; start < n; start += 512) {
    const int len = std::min(512, n - start);
    stft.process(l.data() + start, r.data() + start, len,
                 [&](float* reL, float* imL, float* reR, float* imR, int bins) {
                   dereverb.processBins(reL, imL, reR, imR, bins);
                 });
  }
  const int latency = stft.latencySamples();
  for (int b = 1; b < 3; ++b) {
    const int start = b * sr * 3 / 4;
    const int at = start + latency;
    const double directIn = rmsRange(in, start, 960);
    const double directOut = rmsRange(l, at, 960);
    const double tailIn = rmsRange(in, start + 2000, 10000);
    const double tailOut = rmsRange(l, at + 2000, 10000);
    std::printf("dereverb burst %d direct %.2f dB tail %.2f dB\n", b,
                20.0 * std::log10(directOut / directIn),
                20.0 * std::log10(tailOut / tailIn));
    assert(std::fabs(20.0 * std::log10(directOut / directIn)) < 1.5);
    assert(20.0 * std::log10(tailOut / tailIn) < -3.0);
  }
}


void testProfilePersistence() {
  repair::Engine engine;
  engine.prepare(kSr);
  repair::Controls c;
  c.noiseOn = true;
  c.noiseLearn = true;
  c.noiseReductionDb = 18.0f;
  engine.setControls(c);
  repair::Meters meters;
  const int n = 3 * static_cast<int>(kSr);
  std::vector<float> l(1024), r(1024);
  uint32_t seed = 7;
  for (int start = 0; start < n; start += 1024) {
    const int len = std::min(1024, n - start);
    for (int i = 0; i < len; ++i) {
      seed = seed * 1664525u + 1013904223u;
      l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
          0.04f * static_cast<float>(static_cast<int32_t>(seed >> 8)) /
          8388608.0f;
    }
    engine.process(l.data(), r.data(), len, meters,
                   start / static_cast<double>(kSr), true);
  }
  assert(engine.takeProfileReady());
  float profile[repair::kProfileBins] = {};
  const int bins = engine.copyNoiseProfile(profile, repair::kProfileBins);
  assert(bins > 0);
  double sum = 0.0;
  for (int i = 0; i < bins; ++i) {
    sum += profile[i];
  }
  assert(sum > 0.0);

  repair::Engine restored;
  restored.prepare(kSr);
  assert(restored.loadNoiseProfile(profile, bins));
  float roundTrip[repair::kProfileBins] = {};
  assert(restored.copyNoiseProfile(roundTrip, repair::kProfileBins) == bins);
  for (int i = 0; i < bins; ++i) {
    assert(std::fabs(roundTrip[i] - profile[i]) < 1.0e-6f);
  }
}



void testDeltaAndDc() {
  const int n = 16000;
  std::vector<float> clean(n), clipped(n);
  for (int i = 0; i < n; ++i) {
    clean[i] = 0.2f * sine(i, 220.0);
    clipped[i] = std::clamp(0.7f * sine(i, 220.0), -0.4f, 0.4f);
  }
  repair::Controls off;
  off.clipOn = false;
  off.clickOn = false;
  off.humOn = false;
  off.noiseOn = false;
  off.verbOn = false;
  off.specOn = false;
  off.dcOn = false;
  off.delta = true;

  repair::Engine engine;
  engine.prepare(kSr);
  engine.setControls(off);
  repair::Meters meters;
  std::vector<float> l = clean;
  std::vector<float> r = clean;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    engine.process(l.data() + start, r.data() + start, len, meters,
                   start / static_cast<double>(kSr), true);
  }
  double deltaRms = 0.0;
  for (int i = 4221 + 4096; i < n; ++i) {
    deltaRms += static_cast<double>(l[static_cast<size_t>(i)]) *
                l[static_cast<size_t>(i)];
  }
  deltaRms = std::sqrt(deltaRms / (n - 4221 - 4096));
  std::printf("delta clean residual %.6f\n", deltaRms);
  assert(deltaRms < 2.0e-4);

  repair::Controls clip = off;
  clip.clipOn = true;
  clip.clipAuto = false;
  clip.clipUpperDb = -7.96f;
  clip.clipLowerDb = -7.96f;
  clip.delta = true;
  repair::Engine clipEngine;
  clipEngine.prepare(kSr);
  clipEngine.setControls(clip);
  l = clipped;
  r = clipped;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    clipEngine.process(l.data() + start, r.data() + start, len, meters,
                       start / static_cast<double>(kSr), true);
  }
  double removed = 0.0;
  for (int i = 4221 + 4096; i < n; ++i) {
    removed += static_cast<double>(l[static_cast<size_t>(i)]) *
               l[static_cast<size_t>(i)];
  }
  removed = std::sqrt(removed / (n - 4221 - 4096));
  std::printf("delta clip residual %.6f\n", removed);
  assert(removed > 1.0e-3);

  // DC/rumble filter removes a 10 Hz component and preserves 1 kHz.
  std::vector<float> dcIn(n), dcOut(n);
  for (int i = 0; i < n; ++i) {
    dcIn[i] = 0.2f * sine(i, 10.0) + 0.2f * sine(i, 1000.0);
  }
  repair::Controls dc = off;
  dc.delta = false;
  dc.dcOn = true;
  dc.dcHz = 30.0f;
  repair::Engine dcEngine;
  dcEngine.prepare(kSr);
  dcEngine.setControls(dc);
  dcOut = dcIn;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    dcEngine.process(dcOut.data() + start, dcOut.data() + start, len, meters,
                     start / static_cast<double>(kSr), true);
  }
  const double lowIn = toneMag(dcIn, 10.0f, 4221 + 4096, n - 4221 - 4096);
  const double lowOut = toneMag(dcOut, 10.0f, 4221 + 4096, n - 4221 - 4096);
  const double highIn = toneMag(dcIn, 1000.0f, 4221 + 4096, n - 4221 - 4096);
  const double highOut = toneMag(dcOut, 1000.0f, 4221 + 4096, n - 4221 - 4096);
  std::printf("dc 10 Hz %.1f dB, 1 kHz %.2f dB\n",
              20.0 * std::log10(lowOut / lowIn),
              20.0 * std::log10(highOut / highIn));
  assert(lowOut < lowIn * 0.5);
  assert(std::fabs(20.0 * std::log10(highOut / highIn)) < 0.5);
}


void testStageAuditionAlignment() {
  const int n = 32000;
  std::vector<float> clean(n);
  for (int i = 0; i < n; ++i) {
    clean[i] = 0.2f * sine(i, 220.0);
  }
  repair::Controls c;
  c.clipOn = true;
  c.clipAuto = true;
  c.clickOn = false;
  c.humOn = false;
  c.noiseOn = false;
  c.verbOn = false;
  c.specOn = false;
  c.dcOn = false;
  c.delta = false;
  c.audition = 0;
  repair::Engine engine;
  engine.prepare(kSr);
  engine.setControls(c);
  repair::Meters meters;
  std::vector<float> l = clean;
  std::vector<float> r = clean;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    engine.process(l.data() + start, r.data() + start, len, meters,
                   start / static_cast<double>(kSr), true);
  }
  double err = 0.0;
  const int first = 10000;
  for (int i = first; i < n; ++i) {
    err += static_cast<double>(l[static_cast<size_t>(i)]) *
           l[static_cast<size_t>(i)];
  }
  err = std::sqrt(err / (n - first));
  std::printf("clip audition residual %.6f\n", err);
  assert(err < 2.0e-3);

  // The same delay alignment applies to the De-Click stage.
  repair::Controls click = c;
  click.clipOn = false;
  click.clickOn = true;
  click.audition = 1;
  repair::Engine clickEngine;
  clickEngine.prepare(kSr);
  clickEngine.setControls(click);
  l = clean;
  r = clean;
  for (int start = 0; start < n; start += 256) {
    const int len = std::min(256, n - start);
    clickEngine.process(l.data() + start, r.data() + start, len, meters,
                        start / static_cast<double>(kSr), true);
  }
  err = 0.0;
  for (int i = first; i < n; ++i) {
    err += static_cast<double>(l[static_cast<size_t>(i)]) *
           l[static_cast<size_t>(i)];
  }
  err = std::sqrt(err / (n - first));
  std::printf("click audition residual %.6f\n", err);
  assert(err < 3.0e-3);
}

void testAnalysis() {
  repair::Engine engine;
  engine.prepare(kSr);
  repair::Controls c;
  c.analyze = true;
  engine.setControls(c);
  repair::Meters meters;
  const int n = 3 * static_cast<int>(kSr);
  std::vector<float> l(512), r(512);
  for (int start = 0; start < n; start += 512) {
    const int len = std::min(512, n - start);
    for (int i = 0; i < len; ++i) {
      const int at = start + i;
      float v = 0.2f * sine(at, 50.0) + 0.1f * sine(at, 1000.0) +
                0.01f * sine(at, 7311.0);
      if (at >= 1000 && at < 1100) {
        v = 1.0f;
      }
      l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = v;
    }
    engine.process(l.data(), r.data(), len, meters,
                   start / static_cast<double>(kSr), true);
  }
  assert(meters.analysisReady);
  std::printf("analysis hum %.0f Hz %.1f dB noise %.1f dB clip %.2f%% "
              "clicks %.1f/s reverb %.2f s\n",
              meters.analysisHumHz, meters.analysisHumDb,
              meters.analysisNoiseDb, meters.analysisClipPct,
              meters.analysisClickRate, meters.analysisReverbSec);
  assert(meters.analysisHumHz > 49.0f && meters.analysisHumHz < 61.0f);
  assert(meters.analysisHumDb > -30.0f);
  assert(meters.analysisClipPct > 0.0f);
}


void testFrameSizes() {
  repair::Engine engine;
  engine.prepare(kSr);
  repair::Controls c;
  c.noiseFrame = 0;  // 512-sample STFT
  c.noiseOn = true;
  engine.setControls(c);
  assert(engine.latencySamples() < 4221);
  repair::Meters meters;
  std::vector<float> l(512), r(512);
  for (int i = 0; i < 512; ++i) {
    l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = 0.1f * sine(i, 440.0);
  }
  engine.process(l.data(), r.data(), 512, meters, 0.0, true);
  for (float v : l) {
    assert(std::isfinite(v));
  }
  std::printf("frame 512 latency %u\n", engine.latencySamples());
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
  testDeclickModes();
  testDehum();
  testDenoise();
  testDereverb();
  testProfilePersistence();
  testAnalysis();
  testDeltaAndDc();
  testStageAuditionAlignment();
  testFrameSizes();
  testPresets();
  std::printf("DSP tests passed\n");
  return 0;
}
