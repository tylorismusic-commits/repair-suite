// Repair Suite — panel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/panel.hpp"

#include "presets.hpp"

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
      addToggle(P_SPEC_ON, 140, 160, 110, 40, "ON");
      addToggle(P_SPEC_PRESERVE, 270, 160, 180, 40, "PRESERVE FLOOR");
      addChoice(P_SPEC_OP, 470, 160, 170, 40, "OPERATION");
      addButton(660, 160, 130, 40, "CAPTURE", 1);
      addButton(800, 160, 110, 40, "FREEZE", 3);
      addKnob(P_SPEC_AMOUNT, x0, y1, 56);
      addKnob(P_SPEC_MIX, x0 + step, y1, 56);
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

void Panel::tick(Shared& shared, PanelState& state) const {
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
  state.inHistory[state.inHistoryHead] = state.inLevelL;
  state.inHistoryHead = (state.inHistoryHead + 1) % PanelState::kHistory;
  state.outHistory[state.outHistoryHead] = state.outLevelL;
  state.outHistoryHead = (state.outHistoryHead + 1) % PanelState::kHistory;
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

  for (const Widget& w : widgets_) {
    drawWidget(cr, w, shared, state);
  }

  // Footer / status
  const double f = kPanelHeight - 46;
  setRgb(cr, kEdge, 0.55);
  cairo_rectangle(cr, 24, f, kPanelWidth - 48, 1);
  cairo_fill(cr);
  const double latencyMs = 2047.0 / 48.0;  // 2048 frame, 48 kHz
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
        } else if (w.action == 2) {
          const bool learn =
              shared.noiseLearnActive.load(std::memory_order_relaxed);
          shared.noiseLearnActive.store(!learn, std::memory_order_relaxed);
        } else if (w.action == 3) {
          shared.captureFreeze.store(true, std::memory_order_relaxed);
        }
        return true;
    }
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
  (void)state;
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

void Panel::pointerUp(Shared& shared) {
  (void)shared;
  dragParam_ = -1;
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
