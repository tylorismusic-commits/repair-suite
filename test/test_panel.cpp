// Repair Suite — panel smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cairo.h>

#include <cassert>
#include <cmath>
#include <cstdio>

#include "presets.hpp"
#include "ui/panel.hpp"

int main() {
  using namespace repair;
  Panel panel;
  panel.loadFont("./assets/fonts");
  Shared shared;
  PanelState state;

  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
  cairo_t* cr = cairo_create(surface);
  for (int tab = 0; tab < 7; ++tab) {
    state.tab = tab;
    panel.draw(cr, shared, state);
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  // Page tabs select pages.
  const double tabW = (kPanelWidth - 80.0 - 6 * 8.0) / 7.0;
  for (int tab = 0; tab < 7; ++tab) {
    state.tab = 0;
    panel.pointerDown(shared, state, 40 + tab * (tabW + 8.0) + tabW * 0.5,
                      100, false);
    assert(state.tab == tab);
  }

  // A knob drag changes the value.
  state.tab = 0;
  cairo_surface_t* clipSurface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
  cairo_t* clipCr = cairo_create(clipSurface);
  panel.draw(clipCr, shared, state);
  cairo_destroy(clipCr);
  cairo_surface_destroy(clipSurface);
  const double before = shared.get(P_CLIP_UPPER);
  panel.pointerDown(shared, state, 210, 300, false);
  panel.pointerMove(shared, state, 210, 360, false);
  panel.pointerUp(shared);
  assert(shared.get(P_CLIP_UPPER) != before);

  // Preset apply changes parameters.
  panel.applyPreset(shared, state, 0);
  assert(state.presetIndex == 0);
  assert(shared.get(P_CLICK_MODE) == 1.0);
  assert(std::fabs(shared.get(P_NOISE_REDUCTION) - 6.0) < 1.0e-6);

  // Capture arm toggles the UI action.
  cairo_surface_t* specSurface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
  cairo_t* specCr = cairo_create(specSurface);
  state.tab = 5;
  panel.draw(specCr, shared, state);
  cairo_destroy(specCr);
  cairo_surface_destroy(specSurface);
  panel.pointerDown(shared, state, 725, 180, false);
  assert(shared.captureActive.load());
  panel.pointerDown(shared, state, 725, 180, false);
  assert(!shared.captureActive.load());

  std::printf("panel tests passed\n");
  return 0;
}
