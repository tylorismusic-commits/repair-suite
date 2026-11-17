// Repair Suite — panel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/panel.hpp"

#include "presets.hpp"
#include "dsp/fft.hpp"
#include "dsp/spectral_bridge.hpp"

#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace repair {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kSpecX = 56.0;
constexpr double kSpecY = 190.0;
constexpr double kSpecW = 700.0;
constexpr double kSpecH = 360.0;

struct Rgb {
  double r, g, b;
};

const Rgb kBgTop{0.075, 0.095, 0.105};
const Rgb kBgBottom{0.025, 0.035, 0.040};
const Rgb kPanel{0.105, 0.135, 0.145};
const Rgb kPanelLight{0.150, 0.185, 0.195};
const Rgb kEdge{0.020, 0.030, 0.035};
const Rgb kText{0.880, 0.910, 0.915};
const Rgb kDim{0.500, 0.590, 0.610};
const Rgb kAccent{0.230, 0.760, 0.700};
const Rgb kGold{0.965, 0.790, 0.320};
const Rgb kGreen{0.300, 0.820, 0.500};
const Rgb kAmber{0.950, 0.700, 0.260};

const char* const kPageNames[7] = {"CLIP", "CLICK", "HUM", "NOISE",
                                   "VERB", "SPEC", "OUT"};
const uint32_t kPageOnParam[7] = {P_CLIP_ON,  P_CLICK_ON, P_HUM_ON,
                                  P_NOISE_ON, P_VERB_ON,  P_SPEC_ON,
                                  P_POWER};

void setRgb(cairo_t* cr, Rgb c, double alpha = 1.0) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

double clamp01(double v) { return std::min(std::max(v, 0.0), 1.0); }
double knobAngle(double f) { return (135.0 + 270.0 * f) * kPiD / 180.0; }

void roundRect(cairo_t* cr, double x, double y, double w, double h, double r) {
  r = std::min(r, std::min(w, h) * 0.5);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -kPiD / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, kPiD / 2);
  cairo_arc(cr, x + r, y + h - r, r, kPiD / 2, kPiD);
  cairo_arc(cr, x + r, y + r, r, kPiD, 3 * kPiD / 2);
  cairo_close_path(cr);
}

std::string formatValue(const ParamSpec& s, double value) {
  char buf[64];
  if (s.labels.names) {
    const int n = std::clamp(static_cast<int>(value + 0.5), 0,
                             s.labels.count - 1);
    return s.labels.names[n];
  }
  if (s.toggled) {
    return value > 0.5 ? "On" : "Off";
  }
  if (s.unit) {
    if (std::strcmp(s.unit, "dB") == 0) {
      std::snprintf(buf, sizeof(buf), "%+.1f dB", value);
    } else if (std::strcmp(s.unit, "Hz") == 0) {
      std::snprintf(buf, sizeof(buf), "%.0f Hz", value);
    } else if (std::strcmp(s.unit, "ms") == 0) {
      std::snprintf(buf, sizeof(buf), "%.1f ms", value);
    } else if (std::strcmp(s.unit, "s") == 0) {
      std::snprintf(buf, sizeof(buf), "%.2f s", value);
    } else if (std::strcmp(s.unit, "%") == 0) {
      std::snprintf(buf, sizeof(buf), "%.0f%%", value);
    } else {
      std::snprintf(buf, sizeof(buf), "%.1f", value);
    }
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f", value);
  }
  return buf;
}

} // namespace

Panel::Panel() = default;

Panel::~Panel() {
  if (font_) {
    cairo_font_face_destroy(font_);
  }
  if (ftFace_) {
    FT_Done_Face(static_cast<FT_Face>(ftFace_));
  }
  if (ft_) {
    FT_Done_FreeType(static_cast<FT_Library>(ft_));
  }
}

void Panel::loadFont(const std::string& dir) {
  if (ft_) {
    return;
  }
  std::string path = dir;
  if (!path.empty() && path.back() != '/') {
    path += '/';
  }
  path += "Jost-ExtraBold.ttf";
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib) != 0) {
    return;
  }
  FT_Face face = nullptr;
  if (FT_New_Face(lib, path.c_str(), 0, &face) != 0) {
    FT_Done_FreeType(lib);
    return;
  }
  cairo_font_face_t* font = cairo_ft_font_face_create_for_ft_face(face, 0);
  if (cairo_font_face_status(font) != CAIRO_STATUS_SUCCESS) {
    cairo_font_face_destroy(font);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return;
  }
  ft_ = lib;
  ftFace_ = face;
  font_ = font;
}

void Panel::text(cairo_t* cr, const char* s, double x, double y, double size,
                 int align, double r, double g, double b) const {
  if (font_) {
    cairo_set_font_face(cr, font_);
  } else {
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
  }
  cairo_set_font_size(cr, size);
  cairo_set_source_rgb(cr, r, g, b);
  cairo_text_extents_t e;
  cairo_text_extents(cr, s, &e);
  double tx = x;
  if (align == 1) {
    tx = x - e.x_advance * 0.5;
  } else if (align == 2) {
    tx = x - e.x_advance;
  }
  cairo_move_to(cr, tx, y);
  cairo_show_text(cr, s);
}

void Panel::addKnob(uint32_t param, double x, double y, double r) {
  Widget w{};
  w.kind = Kind::Knob;
  w.param = param;
  w.x = x;
  w.y = y;
  w.r = r;
  widgets_.push_back(w);
}

void Panel::addToggle(uint32_t param, double x, double y, double w, double h,
                      const char* label) {
  Widget widget{};
  widget.kind = Kind::Toggle;
  widget.param = param;
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.label = label;
  widgets_.push_back(widget);
}

void Panel::addChoice(uint32_t param, double x, double y, double w, double h,
                      const char* label) {
  Widget widget{};
  widget.kind = Kind::Choice;
  widget.param = param;
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.label = label;
  widgets_.push_back(widget);
}

void Panel::addButton(double x, double y, double w, double h, const char* label,
                      int action) {
  Widget widget{};
  widget.kind = Kind::Button;
  widget.x = x;
  widget.y = y;
  widget.w = w;
  widget.h = h;
  widget.label = label;
  widget.action = action;
  widgets_.push_back(widget);
}

void Panel::layout(const Shared& shared, const PanelState& state) {
  (void)shared;
  widgets_.clear();
  {
    Widget p{};
    p.kind = Kind::Power;
    p.param = P_POWER;
    p.x = 44;
    p.y = 36;
    p.r = 16;
    widgets_.push_back(p);
  }
  const double tabW = (kPanelWidth - 80.0 - 6 * 8.0) / 7.0;
  for (int i = 0; i < 7; ++i) {
    Widget t{};
    t.kind = Kind::PageTab;
    t.value = i;
    t.x = 40 + i * (tabW + 8.0);
    t.y = 78;
    t.w = tabW;
    t.h = 46;
    t.label = kPageNames[i];
    widgets_.push_back(t);
  }

  const double y1 = 300;
  const double y2 = 510;
  const double x0 = 210;
  const double step = 160;
  switch (state.tab) {
    case 0:  // CLIP
      addToggle(P_CLIP_ON, 140, 160, 110, 40, "ON");
      addToggle(P_CLIP_AUTO, 270, 160, 150, 40, "AUTO");
      addChoice(P_CLIP_MODE, 440, 160, 170, 40, "MODE");
      addKnob(P_CLIP_UPPER, x0, y1, 56);
      addKnob(P_CLIP_LOWER, x0 + step, y1, 56);
      addKnob(P_CLIP_ITER, x0 + 2 * step, y1, 56);
      addKnob(P_CLIP_HEADROOM, x0 + 3 * step, y1, 56);
      addKnob(P_CLIP_MIX, x0 + 4 * step, y1, 56);
      break;
    case 1:  // CLICK
      addToggle(P_CLICK_ON, 140, 160, 110, 40, "ON");
      addChoice(P_CLICK_MODE, 270, 160, 170, 40, "MODE");
      addToggle(P_CLICK_LINK, 460, 160, 170, 40, "LINK");
      addKnob(P_CLICK_SENS, x0, y1, 56);
      addKnob(P_CLICK_WIDTH, x0 + step, y1, 56);
      addKnob(P_CLICK_MIX, x0 + 2 * step, y1, 56);
      break;
    case 2:  // HUM
      addToggle(P_HUM_ON, 140, 160, 110, 40, "ON");
      addToggle(P_HUM_AUTO, 270, 160, 140, 40, "AUTO");
      addToggle(P_HUM_ODD, 430, 160, 140, 40, "ODD");
      addToggle(P_HUM_PRECISION, 590, 160, 160, 40, "PRECISION");
      addKnob(P_HUM_FREQ, x0, y1, 56);
      addKnob(P_HUM_HARMONICS, x0 + step, y1, 56);
      addKnob(P_HUM_DEPTH, x0 + 2 * step, y1, 56);
      addKnob(P_HUM_MIX, x0 + 3 * step, y1, 56);
      break;
    case 3:  // NOISE
      addToggle(P_NOISE_ON, 140, 160, 110, 40, "ON");
      addToggle(P_NOISE_ADAPTIVE, 270, 160, 160, 40, "ADAPTIVE");
      addChoice(P_NOISE_FRAME, 450, 160, 130, 40, "FRAME");
      addButton(620, 160, 150, 40, "LEARN NOISE", 2);
      addKnob(P_NOISE_REDUCTION, x0, y1, 56);
      addKnob(P_NOISE_TONAL, x0 + step, y1, 56);
      addKnob(P_NOISE_SMOOTH, x0 + 2 * step, y1, 56);
      addKnob(P_NOISE_TRANSIENT, x0 + 3 * step, y1, 56);
      addKnob(P_NOISE_MIX, x0 + 4 * step, y1, 56);
      break;
    case 4:  // VERB
      addToggle(P_VERB_ON, 140, 160, 110, 40, "ON");
      addKnob(P_VERB_AMOUNT, x0, y1, 56);
      addKnob(P_VERB_TAIL, x0 + step, y1, 56);
      addKnob(P_VERB_PREDELAY, x0 + 2 * step, y1, 56);
      addKnob(P_VERB_TRANSIENT, x0 + 3 * step, y1, 56);
      addKnob(P_VERB_TILT, x0 + 4 * step, y1, 56);
      addKnob(P_VERB_MIX, x0 + 2 * step, y2, 56);
      break;
    case 5:  // SPECTRAL
      addToggle(P_SPEC_ON, 790, 170, 140, 40, "ON");
      addButton(790, 228, 150, 42, "CAPTURE", 1);
      addButton(956, 228, 150, 42, "FREEZE", 3);
      addButton(790, 282, 150, 42, "APPLY", 5);
      addButton(956, 282, 150, 42, "CLEAR EDITS", 4);
      addChoice(P_SPEC_OP, 790, 344, 316, 48, "OPERATION");
      addKnob(P_SPEC_AMOUNT, 858, 500, 48);
      addKnob(P_SPEC_MIX, 1010, 500, 48);
      addToggle(P_SPEC_PRESERVE, 790, 600, 200, 40, "PRESERVE FLOOR");
      break;
    default:  // OUT
      addKnob(P_INPUT, x0, y1, 56);
      addKnob(P_OUTPUT, x0 + step, y1, 56);
      addKnob(P_DC_FREQ, x0 + 2 * step, y1, 56);
      addToggle(P_DELTA, 140, 160, 120, 40, "DELTA");
      addToggle(P_DC_ON, 280, 160, 150, 40, "DC FILTER");
      break;
  }
}

void Panel::tick(Shared& shared, PanelState& state) {
  state.inLevelL = shared.inPeakLDb.load(std::memory_order_relaxed);
  state.inLevelR = shared.inPeakRDb.load(std::memory_order_relaxed);
  state.outLevelL = shared.outPeakLDb.load(std::memory_order_relaxed);
  state.outLevelR = shared.outPeakRDb.load(std::memory_order_relaxed);
  state.grDb = shared.grDb.load(std::memory_order_relaxed);
  state.noiseGrDb = shared.noiseGrDb.load(std::memory_order_relaxed);
  state.verbGrDb = shared.verbGrDb.load(std::memory_order_relaxed);
  state.captureSeconds = shared.captureSeconds.load(std::memory_order_relaxed);
  state.transportSeconds = shared.transportSeconds.load(std::memory_order_relaxed);
  state.transportValid = shared.transportValid.load(std::memory_order_relaxed);
  state.editCount = shared.editCount.load(std::memory_order_relaxed);
  state.sampleRate = shared.sampleRate.load(std::memory_order_relaxed);
  state.inHistory[state.inHistoryHead] = state.inLevelL;
  state.inHistoryHead = (state.inHistoryHead + 1) % PanelState::kHistory;
  state.outHistory[state.outHistoryHead] = state.outLevelL;
  state.outHistoryHead = (state.outHistoryHead + 1) % PanelState::kHistory;
  loadCapture(shared, state);
}


bool Panel::plotHit(double x, double y) const {
  return x >= kSpecX && x <= kSpecX + kSpecW && y >= kSpecY &&
         y <= kSpecY + kSpecH;
}

void Panel::loadCapture(Shared& shared, PanelState& state) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge || !bridge->frozenReady()) {
    if (captureLoaded_) {
      captureLoaded_ = false;
      state.specValid = false;
      state.hasSelection = false;
    }
    return;
  }
  if (captureLoaded_) {
    return;
  }
  if (bridge->copyFrozen(&captureL_, &captureR_, &captureStart_)) {
    captureLoaded_ = true;
    state.hasSelection = false;
    buildSpectrogram(state);
  }
}

void Panel::buildSpectrogram(PanelState& state) {
  constexpr int kCols = 240;
  constexpr int kRows = 150;
  constexpr int kN = 1024;
  state.specW = kCols;
  state.specH = kRows;
  state.specPixels.assign(static_cast<size_t>(kCols) * kRows, 0xff0a0e12u);
  state.specValid = false;
  state.specStartTime = captureStart_;
  const int n = static_cast<int>(captureL_.size());
  if (n < 64) {
    return;
  }
  Fft fft;
  fft.prepare(kN);
  std::vector<float> window(kN);
  for (int i = 0; i < kN; ++i) {
    window[static_cast<size_t>(i)] =
        0.5f - 0.5f * std::cos(2.0f * static_cast<float>(kPiD) * i / (kN - 1));
  }
  std::vector<float> frame(kN), re(kN), im(kN);
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  for (int col = 0; col < kCols; ++col) {
    const int center = (n - 1) * col / (kCols - 1);
    const int start = center - kN / 2;
    for (int i = 0; i < kN; ++i) {
      const int at = start + i;
      float v = 0.0f;
      if (at >= 0 && at < n) {
        v = 0.5f * (captureL_[static_cast<size_t>(at)] +
                    captureR_[static_cast<size_t>(at)]);
      }
      frame[static_cast<size_t>(i)] = v * window[static_cast<size_t>(i)];
    }
    fft.forward(frame.data(), re.data(), im.data());
    for (int row = 0; row < kRows; ++row) {
      const float t = static_cast<float>(row) / (kRows - 1);
      const float hz = 20000.0f * std::pow(0.001f, t);
      int bin = static_cast<int>(hz * kN / sr);
      bin = std::clamp(bin, 1, kN / 2 - 1);
      const float mag = std::sqrt(re[static_cast<size_t>(bin)] *
                                      re[static_cast<size_t>(bin)] +
                                  im[static_cast<size_t>(bin)] *
                                      im[static_cast<size_t>(bin)]) /
                        static_cast<float>(kN);
      float db = 20.0f * std::log10(mag + 1.0e-9f);
      float e = std::clamp((db + 70.0f) / 60.0f, 0.0f, 1.0f);
      e = std::pow(e, 1.25f);
      double r = 0.02;
      double g = 0.035;
      double b = 0.055;
      if (e < 0.5) {
        const double u = e / 0.5;
        r += (0.055 - r) * u;
        g += (0.42 - g) * u;
        b += (0.46 - b) * u;
      } else {
        const double u = (e - 0.5) / 0.5;
        r = 0.055 + (0.98 - 0.055) * u;
        g = 0.42 + (0.78 - 0.42) * u;
        b = 0.46 + (0.30 - 0.46) * u;
      }
      const uint32_t rr = static_cast<uint32_t>(std::clamp(r, 0.0, 1.0) * 255.0);
      const uint32_t gg = static_cast<uint32_t>(std::clamp(g, 0.0, 1.0) * 255.0);
      const uint32_t bb = static_cast<uint32_t>(std::clamp(b, 0.0, 1.0) * 255.0);
      state.specPixels[static_cast<size_t>(row * kCols + col)] =
          0xff000000u | (rr << 16) | (gg << 8) | bb;
    }
  }
  state.specValid = true;
}

void Panel::selectionTimes(const PanelState& state, double* t0, double* t1,
                           double* f0, double* f1) const {
  const double x0 = std::min(state.selX0, state.selX1);
  const double x1 = std::max(state.selX0, state.selX1);
  const double y0 = std::min(state.selY0, state.selY1);
  const double y1 = std::max(state.selY0, state.selY1);
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double duration = static_cast<double>(captureL_.size()) / sr;
  *t0 = state.specStartTime + (x0 - kSpecX) / kSpecW * duration;
  *t1 = state.specStartTime + (x1 - kSpecX) / kSpecW * duration;
  auto yToHz = [](double y) {
    const double t = std::clamp((y - kSpecY) / kSpecH, 0.0, 1.0);
    return 20000.0 * std::pow(0.001, t);
  };
  const double fa = yToHz(y0);
  const double fb = yToHz(y1);
  *f0 = std::min(fa, fb);
  *f1 = std::max(fa, fb);
}

bool Panel::applySpectralEdit(Shared& shared, PanelState& state) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge || !state.hasSelection || captureL_.empty()) {
    return false;
  }
  double t0 = 0.0;
  double t1 = 0.0;
  double f0 = 0.0;
  double f1 = 0.0;
  selectionTimes(state, &t0, &t1, &f0, &f1);
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const int n = static_cast<int>(captureL_.size());
  int i0 = static_cast<int>((t0 - captureStart_) * sr + 0.5);
  int i1 = static_cast<int>((t1 - captureStart_) * sr + 0.5);
  i0 = std::clamp(i0, 0, n);
  i1 = std::clamp(i1, i0, n);
  const int len = i1 - i0;
  if (len < 8) {
    return false;
  }
  const int margin = 512;
  const int workStart = i0 - margin;
  const int workLen = len + 2 * margin;
  std::vector<float> workL(static_cast<size_t>(workLen), 0.0f);
  std::vector<float> workR(static_cast<size_t>(workLen), 0.0f);
  std::vector<float> accL(static_cast<size_t>(workLen), 0.0f);
  std::vector<float> accR(static_cast<size_t>(workLen), 0.0f);
  for (int i = 0; i < workLen; ++i) {
    const int at = workStart + i;
    if (at >= 0 && at < n) {
      workL[static_cast<size_t>(i)] = captureL_[static_cast<size_t>(at)];
      workR[static_cast<size_t>(i)] = captureR_[static_cast<size_t>(at)];
    }
  }

  constexpr int kN = 512;
  constexpr int kHop = 128;
  Fft fft;
  fft.prepare(kN);
  std::vector<float> window(kN);
  for (int i = 0; i < kN; ++i) {
    window[static_cast<size_t>(i)] =
        0.5f - 0.5f * std::cos(2.0f * static_cast<float>(kPiD) * i / (kN - 1));
  }
  std::vector<float> reL(kN), imL(kN), reR(kN), imR(kN);
  const int op = static_cast<int>(shared.get(P_SPEC_OP) + 0.5);
  const float amount = shared.get(P_SPEC_AMOUNT);
  const float mix = static_cast<float>(shared.get(P_SPEC_MIX) * 0.01);
  int lo = static_cast<int>(f0 * kN / sr);
  int hi = static_cast<int>(f1 * kN / sr);
  lo = std::clamp(lo, 1, kN / 2 - 2);
  hi = std::clamp(hi, lo + 1, kN / 2 - 1);

  for (int start = 0; start + kN <= workLen; start += kHop) {
    for (int i = 0; i < kN; ++i) {
      const float w = window[static_cast<size_t>(i)];
      reL[static_cast<size_t>(i)] =
          workL[static_cast<size_t>(start + i)] * w;
      reR[static_cast<size_t>(i)] =
          workR[static_cast<size_t>(start + i)] * w;
      imL[static_cast<size_t>(i)] = 0.0f;
      imR[static_cast<size_t>(i)] = 0.0f;
    }
    fft.forward(reL.data(), reL.data(), imL.data());
    fft.forward(reR.data(), reR.data(), imR.data());
    for (int k = lo; k <= hi; ++k) {
      float gain = 1.0f;
      if (op == 1) {
        gain = 0.0f;
      } else if (op == 5) {
        gain = std::pow(10.0f, std::clamp(amount, 0.0f, 20.0f) / 20.0f);
      } else if (op == 0 || op == 4) {
        gain = std::pow(10.0f, -std::clamp(amount, 0.0f, 96.0f) / 20.0f);
      } else {
        // Interpolate (also used as the Patch fallback) from the bordering
        // bins, preserving phase.
        const int bl = std::max(1, lo - 1);
        const int bh = std::min(kN / 2 - 1, hi + 1);
        const float t = static_cast<float>(k - bl) /
                        static_cast<float>(std::max(1, bh - bl));
        reL[static_cast<size_t>(k)] =
            (1.0f - t) * reL[static_cast<size_t>(bl)] +
            t * reL[static_cast<size_t>(bh)];
        imL[static_cast<size_t>(k)] =
            (1.0f - t) * imL[static_cast<size_t>(bl)] +
            t * imL[static_cast<size_t>(bh)];
        reR[static_cast<size_t>(k)] =
            (1.0f - t) * reR[static_cast<size_t>(bl)] +
            t * reR[static_cast<size_t>(bh)];
        imR[static_cast<size_t>(k)] =
            (1.0f - t) * imR[static_cast<size_t>(bl)] +
            t * imR[static_cast<size_t>(bh)];
        gain = 1.0f;
      }
      const float applied = 1.0f + mix * (gain - 1.0f);
      reL[static_cast<size_t>(k)] *= applied;
      imL[static_cast<size_t>(k)] *= applied;
      reR[static_cast<size_t>(k)] *= applied;
      imR[static_cast<size_t>(k)] *= applied;
      const int mirror = kN - k;
      reL[static_cast<size_t>(mirror)] = reL[static_cast<size_t>(k)];
      imL[static_cast<size_t>(mirror)] = -imL[static_cast<size_t>(k)];
      reR[static_cast<size_t>(mirror)] = reR[static_cast<size_t>(k)];
      imR[static_cast<size_t>(mirror)] = -imR[static_cast<size_t>(k)];
    }
    fft.inverse(reL.data(), imL.data());
    fft.inverse(reR.data(), imR.data());
    for (int i = 0; i < kN; ++i) {
      const float w = window[static_cast<size_t>(i)] / 1.5f;
      accL[static_cast<size_t>(start + i)] += reL[static_cast<size_t>(i)] * w;
      accR[static_cast<size_t>(start + i)] += reR[static_cast<size_t>(i)] * w;
    }
  }

  std::vector<float> outL(static_cast<size_t>(len));
  std::vector<float> outR(static_cast<size_t>(len));
  for (int i = 0; i < len; ++i) {
    const int at = margin + i;
    outL[static_cast<size_t>(i)] = accL[static_cast<size_t>(at)];
    outR[static_cast<size_t>(i)] = accR[static_cast<size_t>(at)];
  }
  const double startTime = captureStart_ + static_cast<double>(i0) / sr;
  const double endTime = captureStart_ + static_cast<double>(i1) / sr;
  return bridge->addEdit(startTime, endTime, outL.data(), outR.data(), len);
}

void Panel::drawSpectralPage(cairo_t* cr, const Shared& shared,
                             const PanelState& state) const {
  (void)shared;
  // Title
  text(cr, "SPECTRAL REPAIR", kSpecX, 172, 18, 0, kText.r, kText.g, kText.b);

  // Plot frame
  roundRect(cr, kSpecX, kSpecY, kSpecW, kSpecH, 6);
  setRgb(cr, Rgb{0.02, 0.03, 0.04});
  cairo_fill(cr);
  if (state.specValid && state.specW > 0 && state.specH > 0) {
    cairo_surface_t* image = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(
            const_cast<uint32_t*>(state.specPixels.data())),
        CAIRO_FORMAT_ARGB32, state.specW, state.specH, state.specW * 4);
    cairo_save(cr);
    cairo_translate(cr, kSpecX, kSpecY);
    cairo_scale(cr, kSpecW / state.specW, kSpecH / state.specH);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(image);
  } else {
    text(cr, "Capture a section, then Freeze",
         kSpecX + kSpecW * 0.5, kSpecY + kSpecH * 0.5, 15, 1, kDim.r, kDim.g,
         kDim.b);
  }

  // Frequency guide lines.
  cairo_set_line_width(cr, 1.0);
  for (float hz : {50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f}) {
    const double t = std::log(hz / 20000.0) / std::log(0.001);
    const double y = kSpecY + kSpecH * std::clamp(t, 0.0, 1.0);
    setRgb(cr, Rgb{0.8, 0.9, 0.9}, 0.10);
    cairo_move_to(cr, kSpecX, y);
    cairo_line_to(cr, kSpecX + kSpecW, y);
    cairo_stroke(cr);
    char label[16];
    std::snprintf(label, sizeof(label), "%.0f", hz);
    text(cr, label, kSpecX - 8, y + 4, 9, 2, kDim.r, kDim.g, kDim.b);
  }

  // Selection.
  if (state.selecting || state.hasSelection) {
    const double x0 = std::min(state.selX0, state.selX1);
    const double x1 = std::max(state.selX0, state.selX1);
    const double y0 = std::min(state.selY0, state.selY1);
    const double y1 = std::max(state.selY0, state.selY1);
    setRgb(cr, kAccent, 0.18);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_fill(cr);
    setRgb(cr, kAccent, 0.9);
    cairo_set_line_width(cr, 1.4);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_stroke(cr);
  }

  // Status line under the plot.
  char status[160];
  std::snprintf(status, sizeof(status),
                "capture %.1f s   ·   edits %d   ·   %.0f Hz – %.0f Hz",
                captureL_.empty()
                    ? 0.0
                    : static_cast<double>(captureL_.size()) /
                          (state.sampleRate > 0.0f ? state.sampleRate : 48000.0f),
                state.editCount, 20.0, 20000.0);
  text(cr, status, kSpecX, kSpecY + kSpecH + 24, 11, 0, kDim.r, kDim.g,
       kDim.b);
  text(cr, "drag a box on the spectrogram", kSpecX + kSpecW,
       kSpecY + kSpecH + 24, 11, 2, kDim.r, kDim.g, kDim.b);
}

void Panel::draw(cairo_t* cr, Shared& shared, const PanelState& state) {
  layout(shared, state);
  cairo_save(cr);
  cairo_pattern_t* bg = cairo_pattern_create_linear(0, 0, 0, kPanelHeight);
  cairo_pattern_add_color_stop_rgb(bg, 0.0, kBgTop.r, kBgTop.g, kBgTop.b);
  cairo_pattern_add_color_stop_rgb(bg, 1.0, kBgBottom.r, kBgBottom.g,
                                   kBgBottom.b);
  cairo_set_source(cr, bg);
  cairo_paint(cr);
  cairo_pattern_destroy(bg);

  // Faceplate
  roundRect(cr, 20, 20, kPanelWidth - 40, kPanelHeight - 40, 12);
  setRgb(cr, kPanel);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, kEdge, 0.8);
  roundRect(cr, 20.5, 20.5, kPanelWidth - 41, kPanelHeight - 41, 12);
  cairo_stroke(cr);

  drawHeader(cr, shared, state);
  if (state.tab == 5) {
    drawSpectralPage(cr, shared, state);
  }

  for (const Widget& w : widgets_) {
    drawWidget(cr, w, shared, state);
  }

  // Footer / status
  const double f = kPanelHeight - 46;
  setRgb(cr, kEdge, 0.55);
  cairo_rectangle(cr, 24, f, kPanelWidth - 48, 1);
  cairo_fill(cr);
  const double latencyMs = 4221.0 / 48.0;  // declip + STFT, 48 kHz
  char status[160];
  std::snprintf(status, sizeof(status),
                "STFT 2048 / 75%%  ·  latency %.1f ms  ·  capture %.1f s  ·  "
                "edits %d",
                latencyMs, state.captureSeconds, state.editCount);
  text(cr, status, 44, f + 28, 12, 0, kDim.r, kDim.g, kDim.b);
  if (state.transportValid) {
    char t[64];
    std::snprintf(t, sizeof(t), "host %8.3f s", state.transportSeconds);
    text(cr, t, kPanelWidth - 44, f + 28, 12, 2, kAccent.r, kAccent.g,
         kAccent.b);
  } else {
    text(cr, "no transport", kPanelWidth - 44, f + 28, 12, 2, kAmber.r,
         kAmber.g, kAmber.b);
  }

  if (editing_) {
    drawTextEntry(cr, shared, state);
  }
  cairo_restore(cr);
}

void Panel::drawHeader(cairo_t* cr, const Shared& shared,
                       const PanelState& state) const {
  (void)shared;
  (void)state;
  text(cr, "REPAIR SUITE", 90, 50, 26, 0, kText.r, kText.g, kText.b);
  text(cr, "restoration strip", 92, 68, 11, 0, kDim.r, kDim.g, kDim.b);
  text(cr, "CLIP → CLICK → HUM → NOISE → VERB → SPEC → OUT", 1160, 45, 11, 2,
       kDim.r, kDim.g, kDim.b);
}

void Panel::drawWidget(cairo_t* cr, const Widget& w, const Shared& shared,
                       const PanelState& state) const {
  switch (w.kind) {
    case Kind::Power: {
      const bool on = shared.get(P_POWER) > 0.5;
      cairo_arc(cr, w.x, w.y, w.r, 0, 2 * kPiD);
      setRgb(cr, on ? kAccent : Rgb{0.25, 0.29, 0.30});
      cairo_fill(cr);
      cairo_set_line_width(cr, 2.0);
      setRgb(cr, kEdge);
      cairo_arc(cr, w.x, w.y, w.r - 4, -kPiD / 2 + 0.5,
                -kPiD / 2 + 2 * kPiD - 0.5);
      cairo_stroke(cr);
      break;
    }
    case Kind::PageTab: {
      const int page = static_cast<int>(w.value);
      const bool active = state.tab == page;
      const bool on = shared.get(kPageOnParam[page]) > 0.5;
      drawPageTab(cr, w, active, on);
      break;
    }
    case Kind::Knob: {
      const ParamSpec& spec = kParamSpecs[w.param];
      drawKnob(cr, w, shared.get(w.param), spec);
      break;
    }
    case Kind::Toggle:
      drawToggle(cr, w, shared.get(w.param) > 0.5);
      break;
    case Kind::Choice:
      drawChoice(cr, w, static_cast<int>(shared.get(w.param) + 0.5));
      break;
    case Kind::Button: {
      roundRect(cr, w.x, w.y, w.w, w.h, 5);
      setRgb(cr, kPanelLight);
      cairo_fill(cr);
      setRgb(cr, kEdge, 0.8);
      cairo_set_line_width(cr, 1.0);
      roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 5);
      cairo_stroke(cr);
      text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 4, 11, 1,
           kText.r, kText.g, kText.b);
      break;
    }
  }
}

void Panel::drawPageTab(cairo_t* cr, const Widget& w, bool active,
                        bool moduleOn) const {
  roundRect(cr, w.x, w.y, w.w, w.h, 5);
  if (active) {
    setRgb(cr, kAccent);
  } else {
    setRgb(cr, moduleOn ? Rgb{0.16, 0.22, 0.23} : Rgb{0.10, 0.13, 0.14});
  }
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 5);
  cairo_stroke(cr);
  const Rgb tc = active ? Rgb{0.02, 0.05, 0.05} : kText;
  text(cr, w.label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 5, 15, 1, tc.r, tc.g,
       tc.b);
  if (moduleOn) {
    cairo_arc(cr, w.x + 12, w.y + 12, 3.5, 0, 2 * kPiD);
    setRgb(cr, kGreen);
    cairo_fill(cr);
  }
}

void Panel::drawKnob(cairo_t* cr, const Widget& w, double value,
                     const ParamSpec& spec) const {
  const double cx = w.x;
  const double cy = w.y;
  const double r = w.r;
  const double span = spec.max - spec.min;
  const double frac = span > 0.0 ? clamp01((value - spec.min) / span) : 0.0;
  const double a = knobAngle(frac);

  // Arc track.  Start a fresh path: text() leaves a current point behind and
  // cairo_arc would otherwise connect it to the first arc.
  cairo_new_path(cr);
  cairo_set_line_width(cr, 3.0);
  setRgb(cr, kEdge, 0.75);
  cairo_arc(cr, cx, cy, r + 9, knobAngle(0), knobAngle(1));
  cairo_stroke(cr);
  cairo_new_path(cr);
  cairo_set_line_width(cr, 3.0);
  setRgb(cr, kAccent);
  cairo_arc(cr, cx, cy, r + 9, knobAngle(0), a);
  cairo_stroke(cr);

  // Body
  cairo_pattern_t* body = cairo_pattern_create_linear(0, cy - r, 0, cy + r);
  cairo_pattern_add_color_stop_rgb(body, 0.0, 0.30, 0.35, 0.36);
  cairo_pattern_add_color_stop_rgb(body, 1.0, 0.08, 0.10, 0.11);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r, 0, 2 * kPiD);
  cairo_set_source(cr, body);
  cairo_fill(cr);
  cairo_pattern_destroy(body);
  cairo_new_path(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  cairo_arc(cr, cx, cy, r - 0.5, 0, 2 * kPiD);
  cairo_stroke(cr);

  // Indicator
  cairo_new_path(cr);
  cairo_set_line_width(cr, 2.2);
  setRgb(cr, kText);
  cairo_move_to(cr, cx + std::cos(a) * r * 0.25, cy + std::sin(a) * r * 0.25);
  cairo_line_to(cr, cx + std::cos(a) * r * 0.80, cy + std::sin(a) * r * 0.80);
  cairo_stroke(cr);

  text(cr, spec.name, cx, cy + r + 28, 13, 1, kText.r, kText.g, kText.b);
  const std::string v = formatValue(spec, value);
  text(cr, v.c_str(), cx, cy + r + 46, 11, 1, kDim.r, kDim.g, kDim.b);
}

void Panel::drawToggle(cairo_t* cr, const Widget& w, bool on) const {
  roundRect(cr, w.x, w.y, w.w, w.h, 5);
  setRgb(cr, on ? kPanelLight : Rgb{0.075, 0.095, 0.100});
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 5);
  cairo_stroke(cr);
  cairo_arc(cr, w.x + 18, w.y + w.h * 0.5, 5, 0, 2 * kPiD);
  setRgb(cr, on ? kAccent : Rgb{0.22, 0.26, 0.27});
  cairo_fill(cr);
  text(cr, w.label, w.x + 34, w.y + w.h * 0.5 + 4, 11, 0, kText.r, kText.g,
       kText.b);
}

void Panel::drawChoice(cairo_t* cr, const Widget& w, int selected) const {
  roundRect(cr, w.x, w.y, w.w, w.h, 5);
  setRgb(cr, Rgb{0.095, 0.120, 0.128});
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 5);
  cairo_stroke(cr);
  text(cr, w.label, w.x + 12, w.y + 15, 9, 0, kDim.r, kDim.g, kDim.b);
  const ParamSpec& spec = kParamSpecs[w.param];
  const char* name = "—";
  if (spec.labels.names && spec.labels.count > 0) {
    name = spec.labels.names[std::clamp(selected, 0, spec.labels.count - 1)];
  }
  text(cr, name, w.x + w.w - 12, w.y + w.h * 0.5 + 6, 13, 2, kText.r, kText.g,
       kText.b);
}

void Panel::drawTextEntry(cairo_t* cr, const Shared& shared,
                          const PanelState& state) const {
  (void)shared;
  roundRect(cr, editBoxX_, editBoxY_, 170, 40, 5);
  setRgb(cr, Rgb{0.02, 0.03, 0.035});
  cairo_fill(cr);
  setRgb(cr, kAccent, 0.9);
  cairo_set_line_width(cr, 1.2);
  roundRect(cr, editBoxX_ + 0.5, editBoxY_ + 0.5, 169, 39, 5);
  cairo_stroke(cr);
  text(cr, state.editText.c_str(), editBoxX_ + 10, editBoxY_ + 26, 14, 0,
       kGold.r, kGold.g, kGold.b);
}

bool Panel::hit(const Widget& w, double x, double y) const {
  if (w.kind == Kind::Knob || w.kind == Kind::Power) {
    const double dx = x - w.x;
    const double dy = y - w.y;
    const double rr = (w.kind == Kind::Knob ? w.r + 12 : w.r);
    return dx * dx + dy * dy <= rr * rr;
  }
  return x >= w.x && x <= w.x + w.w && y >= w.y && y <= w.y + w.h;
}

const Panel::Widget* Panel::find(uint32_t param, Kind kind) const {
  for (const Widget& w : widgets_) {
    if (w.param == param && w.kind == kind) {
      return &w;
    }
  }
  return nullptr;
}

bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  if (handlePresetClick(shared, state, x, y)) {
    return true;
  }
  for (const Widget& w : widgets_) {
    if (!hit(w, x, y)) {
      continue;
    }
    switch (w.kind) {
      case Kind::Power:
        shared.set(P_POWER, shared.get(P_POWER) > 0.5 ? 0.0 : 1.0);
        state.presetDirty = true;
        return true;
      case Kind::PageTab:
        state.tab = static_cast<int>(w.value);
        return true;
      case Kind::Knob:
        dragParam_ = static_cast<int>(w.param);
        dragStartY_ = y;
        dragStartValue_ = shared.get(w.param);
        (void)fine;
        return false;
      case Kind::Toggle:
        shared.set(w.param, shared.get(w.param) > 0.5 ? 0.0 : 1.0);
        state.presetDirty = true;
        return true;
      case Kind::Choice: {
        const ParamSpec& spec = kParamSpecs[w.param];
        int v = static_cast<int>(shared.get(w.param) + 0.5) + 1;
        const int count = spec.labels.names ? spec.labels.count
                                            : static_cast<int>(spec.max) + 1;
        if (v >= count) {
          v = 0;
        }
        shared.set(w.param, static_cast<double>(v));
        state.presetDirty = true;
        return true;
      }
      case Kind::Button:
        if (w.action == 1) {
          const bool armed = shared.captureActive.load(std::memory_order_relaxed);
          shared.captureActive.store(!armed, std::memory_order_relaxed);
          if (!armed) {
            captureLoaded_ = false;
            state.specValid = false;
            state.hasSelection = false;
          }
        } else if (w.action == 2) {
          const bool learn =
              shared.noiseLearnActive.load(std::memory_order_relaxed);
          shared.noiseLearnActive.store(!learn, std::memory_order_relaxed);
        } else if (w.action == 3) {
          shared.captureFreeze.store(true, std::memory_order_relaxed);
        } else if (w.action == 4) {
          shared.clearEdits.store(true, std::memory_order_relaxed);
          if (auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed)) {
            bridge->clearEdits();
          }
          state.hasSelection = false;
        } else if (w.action == 5) {
          applySpectralEdit(shared, state);
        }
        return true;
    }
  }
  if (state.tab == 5 && plotHit(x, y)) {
    selectionDrag_ = true;
    selectionStartX_ = x;
    selectionStartY_ = y;
    state.selX0 = state.selX1 = x;
    state.selY0 = state.selY1 = y;
    state.selecting = true;
    state.hasSelection = false;
    return true;
  }
  return false;
}

bool Panel::contextDown(Shared& shared, PanelState& state, double x, double y) {
  (void)shared;
  (void)state;
  (void)x;
  (void)y;
  return false;
}

bool Panel::keyEvent(Shared& shared, PanelState& state, int key,
                     const char* text) {
  (void)shared;
  (void)state;
  (void)key;
  (void)text;
  return false;
}

bool Panel::pointerMove(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  if (selectionDrag_) {
    state.selX1 = std::clamp(x, kSpecX, kSpecX + kSpecW);
    state.selY1 = std::clamp(y, kSpecY, kSpecY + kSpecH);
    state.selecting = true;
    return true;
  }
  (void)x;
  if (dragParam_ < 0) {
    return false;
  }
  const uint32_t param = static_cast<uint32_t>(dragParam_);
  const ParamSpec& spec = kParamSpecs[param];
  const double perPixel = (spec.max - spec.min) / (fine ? 800.0 : 240.0);
  double v = dragStartValue_ + (dragStartY_ - y) * perPixel;
  if (spec.stepped) {
    v = std::round(v);
  }
  shared.set(param, v);
  state.presetDirty = true;
  return true;
}

void Panel::pointerUp(Shared& shared, PanelState& state) {
  (void)shared;
  dragParam_ = -1;
  if (selectionDrag_) {
    selectionDrag_ = false;
    state.selecting = false;
    state.hasSelection = std::fabs(state.selX1 - state.selX0) > 4.0 &&
                         std::fabs(state.selY1 - state.selY0) > 4.0;
  }
}

bool Panel::wheel(Shared& shared, PanelState& state, double x, double y,
                  double dy) {
  for (const Widget& w : widgets_) {
    if (w.kind != Kind::Knob || !hit(w, x, y)) {
      continue;
    }
    const ParamSpec& spec = kParamSpecs[w.param];
    double v = shared.get(w.param);
    if (spec.stepped) {
      v += dy > 0 ? 1.0 : -1.0;
    } else {
      v += dy * (spec.max - spec.min) * 0.01;
    }
    shared.set(w.param, v);
    state.presetDirty = true;
    return true;
  }
  return false;
}

void Panel::applyPreset(Shared& shared, PanelState& state, int index) const {
  const auto& presets = factoryPresets();
  if (index < 0 || index >= static_cast<int>(presets.size())) {
    return;
  }
  for (uint32_t p = 0; p < P_COUNT; ++p) {
    shared.set(p, presets[static_cast<size_t>(index)].values[p]);
  }
  state.presetIndex = index;
  state.presetDirty = false;
}

bool Panel::handlePresetClick(Shared& shared, PanelState& state, double x,
                              double y) {
  (void)shared;
  (void)state;
  (void)x;
  (void)y;
  return false;
}

} // namespace repair
