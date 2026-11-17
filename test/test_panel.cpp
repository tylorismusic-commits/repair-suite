// Repair Suite — panel smoke test
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cairo.h>

#include <cassert>
#include <cmath>
#include <vector>
#include <cstdio>

#include "dsp/spectral_capture.hpp"
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
  panel.pointerUp(shared, state);
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
  panel.pointerDown(shared, state, 865, 249, false);
  assert(shared.captureActive.load());
  panel.pointerDown(shared, state, 865, 249, false);
  assert(!shared.captureActive.load());


  {
    // Spectral page: freeze a capture, select a box, apply an erase edit.
    Shared spectralShared;
    SpectralCapture capture;
    capture.prepare(48000.0f, 1.0f);
    capture.setArmed(true);
    std::vector<float> l(48000), r(48000);
    for (int i = 0; i < 48000; ++i) {
      l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
          0.2f * std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
    }
    capture.push(l.data(), r.data(), 48000, 0.0, true);
    capture.requestFreeze();
    std::vector<float> zeros(512, 0.0f);
    capture.push(zeros.data(), zeros.data(), 512, 1.0, true);
    assert(capture.frozenReady());
    spectralShared.spectralBridge.store(&capture);
    spectralShared.sampleRate.store(48000.0f);

    Panel spectralPanel;
    spectralPanel.loadFont("./assets/fonts");
    PanelState spectralState;
    spectralState.tab = 5;
    cairo_surface_t* ss =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
    cairo_t* sctx = cairo_create(ss);
    spectralPanel.draw(sctx, spectralShared, spectralState);
    spectralPanel.tick(spectralShared, spectralState);
    spectralPanel.draw(sctx, spectralShared, spectralState);
    assert(spectralState.specValid);
    assert(spectralState.specW > 0 && spectralState.specH > 0);

    spectralPanel.pointerDown(spectralShared, spectralState, 100, 300, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 250, 420, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(spectralState.hasSelection);

    spectralShared.set(P_SPEC_OP, 1.0);  // Erase
    spectralShared.set(P_SPEC_MIX, 100.0);
    spectralPanel.pointerDown(spectralShared, spectralState, 865, 303, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() == 1);
    const auto& edit = capture.edits().front();
    double energy = 0.0;
    for (float v : edit.left) {
      energy += static_cast<double>(v) * v;
    }
    const double editRms =
        std::sqrt(energy / std::max<size_t>(1, edit.left.size()));
    assert(editRms < 0.02);

    spectralPanel.pointerDown(spectralShared, spectralState, 345, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() == 0);
    spectralPanel.pointerDown(spectralShared, spectralState, 423, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() == 1);

    spectralPanel.pointerDown(spectralShared, spectralState, 1052, 303, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() > 1);

    spectralPanel.pointerDown(spectralShared, spectralState, 706, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.previewActive());
    {
      std::vector<float> pl(128), pr(128);
      assert(capture.readPreview(pl.data(), pr.data(), 128));
    }
    spectralPanel.pointerDown(spectralShared, spectralState, 706, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(!capture.previewActive());

    // Lasso and brush tools fill a selection mask.
    spectralPanel.pointerDown(spectralShared, spectralState, 174, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    spectralPanel.pointerDown(spectralShared, spectralState, 100, 300, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 200, 310, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 180, 410, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 90, 390, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(spectralState.hasSelection);
    spectralPanel.pointerDown(spectralShared, spectralState, 262, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    spectralPanel.pointerDown(spectralShared, spectralState, 300, 320, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 340, 360, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(spectralState.hasSelection);

    // Zoom and diff toggle do not disturb the capture.
    spectralPanel.wheel(spectralShared, spectralState, 400, 350, 1.0);
    assert(spectralState.specZoom > 1.0);
    spectralPanel.pointerDown(spectralShared, spectralState, 628, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(spectralState.diffMode);
    spectralPanel.draw(sctx, spectralShared, spectralState);

    const int beforeZoomEdit = capture.editCount();
    spectralPanel.pointerDown(spectralShared, spectralState, 91, 607, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    spectralPanel.pointerDown(spectralShared, spectralState, 350, 300, false);
    spectralPanel.pointerMove(spectralShared, spectralState, 450, 400, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    spectralShared.set(P_SPEC_OP, 0.0);
    spectralPanel.pointerDown(spectralShared, spectralState, 865, 303, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() > beforeZoomEdit);
    const int beforePatch = capture.editCount();
    spectralShared.set(P_SPEC_OP, 3.0);  // Patch
    spectralPanel.pointerDown(spectralShared, spectralState, 865, 303, false);
    spectralPanel.pointerUp(spectralShared, spectralState);
    assert(capture.editCount() > beforePatch);
    cairo_destroy(sctx);
    cairo_surface_destroy(ss);
  }

  {
    // Preset bar: next arrow applies a preset, center opens the list.
    Shared presetShared;
    PanelState presetState;
    Panel presetPanel;
    presetPanel.loadFont("./assets/fonts");
    cairo_surface_t* psurf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
    cairo_t* pcr = cairo_create(psurf);
    presetPanel.draw(pcr, presetShared, presetState);
    presetPanel.pointerDown(presetShared, presetState, 72 + 330 - 16, 32,
                            false);
    presetPanel.pointerUp(presetShared, presetState);
    assert(presetState.presetIndex == 0);
    presetPanel.pointerDown(presetShared, presetState, 72 + 165, 32, false);
    presetPanel.pointerUp(presetShared, presetState);
    assert(presetState.presetOpen == 1);
    presetPanel.draw(pcr, presetShared, presetState);
    presetPanel.pointerDown(presetShared, presetState, 72 + 165, 90, false);
    presetPanel.pointerUp(presetShared, presetState);
    assert(presetState.presetOpen == 0);
    cairo_destroy(pcr);
    cairo_surface_destroy(psurf);
  }

  {
    // Auto Analyze apply sets the modules from the published result.
    Shared a;
    a.analysisReady.store(true);
    a.analysisHumHz.store(60.0f);
    a.analysisHumDb.store(-30.0f);
    a.analysisNoiseDb.store(-55.0f);
    a.analysisClipPct.store(0.5f);
    a.analysisClickRate.store(2.0f);
    a.analysisReverbSec.store(0.8f);
    PanelState st;
    st.tab = 6;
    Panel p2;
    p2.loadFont("./assets/fonts");
    cairo_surface_t* as =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth, kPanelHeight);
    cairo_t* ac = cairo_create(as);
    p2.draw(ac, a, st);
    p2.pointerDown(a, st, 675, 180, false);
    p2.pointerUp(a, st);
    assert(a.get(P_HUM_ON) == 1.0);
    assert(std::fabs(a.get(P_HUM_FREQ) - 60.0) < 1.0e-6);
    assert(a.get(P_CLIP_ON) == 1.0);
    assert(a.get(P_VERB_ON) == 1.0);
    cairo_destroy(ac);
    cairo_surface_destroy(as);
  }


  {
    // Workflow controls: monitor, typed entry, double-click reset, module
    // preset/reset, histogram drag and harmonic-table toggles.
    Shared w;
    PanelState ws;
    Panel wp;
    wp.loadFont("./assets/fonts");
    cairo_surface_t* wsurf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth,
                                   kPanelHeight);
    cairo_t* wcr = cairo_create(wsurf);
    ws.tab = 0;
    wp.draw(wcr, w, ws);

    // Persistent monitor strip: Input segment.
    wp.pointerDown(w, ws, 56 + 66 + 88 + 20, 134, false);
    wp.pointerUp(w, ws);
    assert(w.get(P_MONITOR) == 1.0);

    // Drag the upper clip threshold handle.
    w.set(P_CLIP_UPPER, -12.0);
    const double thresholdY = 420 + 190 - 18 - ((-12.0 + 60.0) / 60.0) * 142.0;
    const double oldUpper = w.get(P_CLIP_UPPER);
    wp.pointerDown(w, ws, 200, thresholdY, false);
    wp.pointerMove(w, ws, 200, thresholdY - 20, false);
    wp.pointerUp(w, ws);
    assert(w.get(P_CLIP_UPPER) > oldUpper);

    // Module preset and reset buttons.
    const double oldIter = w.get(P_CLIP_ITER);
    wp.pointerDown(w, ws, 1040, 180, false);
    wp.pointerUp(w, ws);
    assert(ws.modulePreset[0] == 1);
    wp.pointerDown(w, ws, 940, 180, false);
    wp.pointerUp(w, ws);
    assert(std::fabs(w.get(P_CLIP_ITER) - kParamSpecs[P_CLIP_ITER].def) <
           1.0e-6);
    (void)oldIter;

    // Right-click typed entry and double-click reset.
    ws.tab = 6;
    wp.draw(wcr, w, ws);
    w.set(P_INPUT, 0.0);
    wp.contextDown(w, ws, 210, 350);
    const char negative[] = "-6";
    for (const char* c = negative; *c; ++c) {
      const char one[2] = {*c, 0};
      wp.keyEvent(w, ws, 0, one);
    }
    wp.keyEvent(w, ws, 13, nullptr);
    assert(std::fabs(w.get(P_INPUT) + 6.0) < 1.0e-6);

    w.set(P_INPUT, 3.0);
    wp.pointerDown(w, ws, 210, 350, false);
    wp.pointerUp(w, ws);
    wp.pointerDown(w, ws, 210, 350, false);
    wp.pointerUp(w, ws);
    std::printf("double reset input %.3f def %.3f\n", w.get(P_INPUT),
                kParamSpecs[P_INPUT].def);
    assert(std::fabs(w.get(P_INPUT) - kParamSpecs[P_INPUT].def) < 1.0e-6);

    // Analyze review row checkbox toggles independently of APPLY.
    ws.tab = 6;
    wp.draw(wcr, w, ws);
    const bool beforeApply = ws.analysisApply[0];
    wp.pointerDown(w, ws, 148, 256.0, false);
    wp.pointerUp(w, ws);
    assert(ws.analysisApply[0] == !beforeApply);

    // Hum table row toggles a harmonic bit.
    ws.tab = 2;
    wp.draw(wcr, w, ws);
    const uint64_t beforeMask = w.humMask.load();
    wp.pointerDown(w, ws, 360, 464, false);
    wp.pointerUp(w, ws);
    assert(w.humMask.load() != beforeMask);

    cairo_destroy(wcr);
    cairo_surface_destroy(wsurf);
  }

  {
    // Spectral harmonic selection and named history.
    Shared h;
    SpectralCapture cap;
    cap.prepare(48000.0f, 1.0f);
    cap.setArmed(true);
    std::vector<float> l(48000), r(48000);
    for (int i = 0; i < 48000; ++i) {
      l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
          0.2f * std::sin(2.0 * 3.141592653589793 * 220.0 * i / 48000.0);
    }
    cap.push(l.data(), r.data(), 48000, 0.0, true);
    cap.requestFreeze();
    std::vector<float> zeros(512, 0.0f);
    cap.push(zeros.data(), zeros.data(), 512, 1.2, true);
    h.spectralBridge.store(&cap);
    h.sampleRate.store(48000.0f);
    h.set(P_HUM_FREQ, 50.0);
    h.set(P_HUM_HARMONICS, 8);
    Panel sp;
    sp.loadFont("./assets/fonts");
    PanelState ss;
    ss.tab = 5;
    cairo_surface_t* hs =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kPanelWidth,
                                   kPanelHeight);
    cairo_t* hc = cairo_create(hs);
    sp.tick(h, ss);
    sp.draw(hc, h, ss);
    sp.pointerDown(h, ss, 92, 650, false);
    sp.pointerUp(h, ss);
    assert(ss.specTool == 3);
    sp.pointerDown(h, ss, 120, 250, false);
    sp.pointerMove(h, ss, 360, 250, false);
    sp.pointerUp(h, ss);
    assert(ss.hasSelection);
    int selected = 0;
    for (uint8_t v : ss.specMask) {
      selected += v ? 1 : 0;
    }
    assert(selected > 0);
    h.set(P_SPEC_OP, 0.0);
    sp.pointerDown(h, ss, 865, 303, false);
    sp.pointerUp(h, ss);
    assert(cap.editCount() == 1);
    sp.draw(hc, h, ss);
    const bool hb = sp.pointerDown(h, ss, 190, 650, false);
    sp.pointerUp(h, ss);
    std::printf("history click %d open %d\n", hb ? 1 : 0,
                ss.historyOpen ? 1 : 0);
    assert(ss.historyOpen);
    cairo_destroy(hc);
    cairo_surface_destroy(hs);
  }

  std::printf("panel tests passed\n");
  return 0;
}
