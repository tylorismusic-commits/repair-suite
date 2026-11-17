// Repair Suite — render a panel preview PNG
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cairo.h>

#include <string>
#include <vector>
#include <cmath>

#include "dsp/spectral_capture.hpp"
#include "presets.hpp"
#include "ui/panel.hpp"

int main(int argc, char** argv) {
  using namespace repair;
  const int tab = argc > 1 ? std::stoi(argv[1]) : 0;
  const std::string out = argc > 2 ? argv[2] : "repair-suite.png";

  Panel panel;
  panel.loadFont("./assets/fonts");
  Shared shared;
  PanelState state;
  state.tab = tab;
  state.presetOpen = argc > 3 ? std::stoi(argv[3]) : 0;
  panel.applyPreset(shared, state, 0);
  if (argc > 4 && std::stoi(argv[4]) != 0) {
    shared.compare.store(true, std::memory_order_relaxed);
  }
  if (argc > 5 && std::stoi(argv[5]) != 0) {
    state.editParam = static_cast<int>(P_INPUT);
    state.editText = "12.5";
  }
  if (argc > 6 && std::stoi(argv[6]) != 0) {
    shared.bpm.store(90.0f, std::memory_order_relaxed);
    shared.hostTempo.store(true, std::memory_order_relaxed);
  }

  if (tab == 5) {
    SpectralCapture capture;
    capture.prepare(48000.0f, 2.0f);
    capture.setArmed(true);
    std::vector<float> l(72000), r(72000);
    uint32_t seed = 1;
    for (int i = 0; i < 72000; ++i) {
      seed = seed * 1664525u + 1013904223u;
      const float noise = static_cast<float>(static_cast<int32_t>(seed >> 8)) /
                          8388608.0f * 0.01f;
      l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
          0.22f * std::sin(2.0 * 3.141592653589793 * 220.0 * i / 48000.0) +
          0.12f * std::sin(2.0 * 3.141592653589793 * 880.0 * i / 48000.0) +
          0.05f * std::sin(2.0 * 3.141592653589793 * 2500.0 * i / 48000.0) +
          noise;
    }
    capture.push(l.data(), r.data(), 72000, 0.0, true);
    capture.requestFreeze();
    std::vector<float> zeros(512, 0.0f);
    capture.push(zeros.data(), zeros.data(), 512, 1.5, true);
    shared.spectralBridge.store(&capture);
    shared.sampleRate.store(48000.0f);
    panel.tick(shared, state);
    panel.pointerDown(shared, state, 300, 270, false);
    panel.pointerMove(shared, state, 430, 380, false);
    panel.pointerUp(shared, state);
  }
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
  cairo_t* cr = cairo_create(surface);
  panel.draw(cr, shared, state);
  cairo_destroy(cr);
  cairo_surface_write_to_png(surface, out.c_str());
  cairo_surface_destroy(surface);
  return 0;
}
