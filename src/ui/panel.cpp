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
#include <chrono>
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
constexpr double kPresetX = 72.0;
constexpr double kPresetY = 14.0;
constexpr double kPresetW = 330.0;
constexpr double kPresetH = 36.0;
constexpr double kListY = 56.0;
constexpr double kListRow = 22.0;
constexpr double kListVisible = 14.0;
constexpr double kMonX = 56.0;
constexpr double kMonY = 122.0;
constexpr double kMonH = 28.0;
constexpr double kMonSegW = 88.0;
constexpr double kMonABX = kMonX + 66.0 + 3 * kMonSegW + 8.0;
constexpr double kMonABW = 58.0;
constexpr double kAnalysisRowY = 252.0;
constexpr double kAnalysisRowH = 30.0;

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

double nowMs() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string lowerText(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string trimText(std::string s) {
  const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

bool parseParamValue(const ParamSpec& spec, const std::string& raw,
                     double* out) {
  if (!out) {
    return false;
  }
  const std::string text = lowerText(trimText(raw));
  if (text.empty()) {
    return false;
  }
  if (spec.labels.names) {
    for (int i = 0; i < spec.labels.count; ++i) {
      if (text == lowerText(spec.labels.names[i])) {
        *out = spec.min + i;
        return true;
      }
    }
  }
  if (text == "on" || text == "true" || text == "yes") {
    *out = 1.0;
    return true;
  }
  if (text == "off" || text == "false" || text == "no") {
    *out = 0.0;
    return true;
  }
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) {
    return false;
  }
  const std::string suffix = trimText(std::string(end));
  double v = value;
  if (suffix == "khz" || suffix == "k") {
    v *= 1000.0;
  } else if (suffix == "ms") {
    v = value;
  } else if (suffix == "s" || suffix == "sec") {
    v = value;
  } else if (suffix == "db" || suffix == "hz" || suffix == "%") {
    v = value;
  } else if (!suffix.empty()) {
    return false;
  }
  if (spec.stepped) {
    v = std::round(v);
  }
  *out = v;
  return true;
}

double presetResetValue(const PanelState& state, uint32_t param) {
  double value = kParamSpecs[param].def;
  if (state.presetIndex >= 0 &&
      state.presetIndex < static_cast<int>(factoryPresets().size())) {
    value = factoryPresets()[static_cast<size_t>(state.presetIndex)]
                .values[param];
  }
  return value;
}

struct ModulePresetValue {
  uint32_t param = 0;
  float value = 0.0f;
};

struct ModulePreset {
  const char* name = "Init";
  std::vector<ModulePresetValue> values;
};

const std::vector<std::vector<ModulePreset>>& modulePresets() {
  static const std::vector<std::vector<ModulePreset>> presets = {
      // CLIP
      {{"Digital", {{P_CLIP_MODE, 0}, {P_CLIP_AUTO, 1}, {P_CLIP_ITER, 12},
                     {P_CLIP_HEADROOM, 0}, {P_CLIP_MIX, 100}}},
       {"Vinyl", {{P_CLIP_MODE, 1}, {P_CLIP_AUTO, 0}, {P_CLIP_UPPER, -3},
                   {P_CLIP_LOWER, -3}, {P_CLIP_ITER, 16},
                   {P_CLIP_HEADROOM, 2}}},
       {"Fast", {{P_CLIP_MODE, 2}, {P_CLIP_AUTO, 1}, {P_CLIP_ITER, 8},
                  {P_CLIP_HEADROOM, 1}}}},
      // CLICK
      {{"Vinyl", {{P_CLICK_MODE, 1}, {P_CLICK_SENS, 5}, {P_CLICK_WIDTH, 4},
                   {P_CLICK_LINK, 1}}},
       {"Dialogue", {{P_CLICK_MODE, 0}, {P_CLICK_SENS, 4},
                      {P_CLICK_WIDTH, 2}, {P_CLICK_LINK, 1}}},
       {"Percussive", {{P_CLICK_MODE, 0}, {P_CLICK_SENS, 6},
                        {P_CLICK_WIDTH, 1}, {P_CLICK_LINK, 0}}},
       {"Dropout", {{P_CLICK_MODE, 3}, {P_CLICK_SENS, 5},
                     {P_CLICK_WIDTH, 3}, {P_CLICK_LINK, 1}}}},
      // HUM
      {{"50 Hz", {{P_HUM_AUTO, 0}, {P_HUM_FREQ, 50}, {P_HUM_HARMONICS, 12},
                   {P_HUM_DEPTH, 30}, {P_HUM_ODD, 0}}},
       {"60 Hz", {{P_HUM_AUTO, 0}, {P_HUM_FREQ, 60}, {P_HUM_HARMONICS, 12},
                   {P_HUM_DEPTH, 30}, {P_HUM_ODD, 0}}},
       {"Wide", {{P_HUM_AUTO, 1}, {P_HUM_HARMONICS, 24}, {P_HUM_DEPTH, 42},
                  {P_HUM_PRECISION, 1}}},
       {"Odd Only", {{P_HUM_AUTO, 1}, {P_HUM_HARMONICS, 16}, {P_HUM_ODD, 1},
                      {P_HUM_DEPTH, 36}}}},
      // NOISE
      {{"Hiss", {{P_NOISE_REDUCTION, 12}, {P_NOISE_TONAL, 8},
                  {P_NOISE_SMOOTH, 5}, {P_NOISE_TRANSIENT, 5}}},
       {"Room", {{P_NOISE_REDUCTION, 18}, {P_NOISE_TONAL, 12},
                  {P_NOISE_SMOOTH, 7}, {P_NOISE_TRANSIENT, 6}}},
       {"Tape", {{P_NOISE_REDUCTION, 8}, {P_NOISE_TONAL, 18},
                  {P_NOISE_SMOOTH, 4}, {P_NOISE_TRANSIENT, 4}}},
       {"Adaptive", {{P_NOISE_ADAPTIVE, 1}, {P_NOISE_REDUCTION, 14},
                      {P_NOISE_SMOOTH, 6}}}},
      // VERB
      {{"Room", {{P_VERB_AMOUNT, 6}, {P_VERB_TAIL, 0.6}, {P_VERB_PREDELAY, 25},
                  {P_VERB_TRANSIENT, 6}}},
       {"Hall", {{P_VERB_AMOUNT, 10}, {P_VERB_TAIL, 1.4},
                  {P_VERB_PREDELAY, 40}, {P_VERB_TRANSIENT, 7}}},
       {"Dialogue", {{P_VERB_AMOUNT, 5}, {P_VERB_TAIL, 0.5},
                      {P_VERB_PREDELAY, 20}, {P_VERB_TRANSIENT, 8}}}},
      // SPECTRAL
      {{"Dialogue", {{P_SPEC_OP, 2}, {P_SPEC_AMOUNT, 24},
                      {P_SPEC_PRESERVE, 1}}},
       {"Music", {{P_SPEC_OP, 3}, {P_SPEC_AMOUNT, 18},
                   {P_SPEC_PRESERVE, 1}}},
       {"Erase", {{P_SPEC_OP, 1}, {P_SPEC_PRESERVE, 1}}}},
      // OUT
      {{"Safe", {{P_INPUT, 0}, {P_OUTPUT, 0}, {P_DC_ON, 0}, {P_DC_FREQ, 20}}},
       {"Rumble", {{P_DC_ON, 1}, {P_DC_FREQ, 30}, {P_INPUT, 0},
                    {P_OUTPUT, 0}}}}};
  return presets;
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

std::vector<int> presetOrder() {
  static const std::vector<int> order = [] {
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(factoryPresets().size()); ++i) {
      v.push_back(i);
    }
    std::stable_sort(v.begin(), v.end(), [](int a, int b) {
      const int c = std::strcmp(factoryPresets()[a].category,
                                factoryPresets()[b].category);
      if (c != 0) {
        return c < 0;
      }
      return std::strcmp(factoryPresets()[a].name,
                         factoryPresets()[b].name) < 0;
    });
    return v;
  }();
  return order;
}

int presetCategoryCount() {
  int count = 0;
  const char* last = nullptr;
  for (int i : presetOrder()) {
    const char* category = factoryPresets()[static_cast<size_t>(i)].category;
    if (!last || std::strcmp(last, category) != 0) {
      ++count;
      last = category;
    }
  }
  return count;
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
    p.x = 34;
    p.y = 30;
    p.r = 14;
    widgets_.push_back(p);
  }
  const double tabW = (kPanelWidth - 80.0 - 6 * 8.0) / 7.0;
  for (int i = 0; i < 7; ++i) {
    Widget t{};
    t.kind = Kind::PageTab;
    t.value = i;
    t.x = 40 + i * (tabW + 8.0);
    t.y = 70;
    t.w = tabW;
    t.h = 40;
    t.label = kPageNames[i];
    widgets_.push_back(t);
  }

  const double y1 = 300;
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
      addKnob(P_VERB_MIX, x0 + 5 * step, y1, 56);
      break;
    case 5:  // SPECTRAL
      addToggle(P_SPEC_ON, 790, 170, 140, 40, "ON");
      addButton(946, 170, 92, 40, "GAMMA", 24);
      addButton(1046, 170, 92, 40, "RANGE", 25);
      addButton(790, 228, 150, 42, "CAPTURE", 1);
      addButton(956, 228, 150, 42, "FREEZE", 3);
      addButton(790, 282, 96, 42, "APPLY", 5);
      addButton(894, 282, 100, 42, "CLEAR", 4);
      addButton(1002, 282, 100, 42, "MATCH", 19);
      addChoice(P_SPEC_OP, 790, 344, 316, 48, "OPERATION");
      addKnob(P_SPEC_AMOUNT, 858, 500, 48);
      addKnob(P_SPEC_MIX, 1010, 500, 48);
      addToggle(P_SPEC_PRESERVE, 790, 600, 200, 40, "PRESERVE FLOOR");
      {
        const double by = 590;
        const double bh = 34;
        double bx = 56;
        addButton(bx, by, 70, bh, "RECT", 10);
        bx += 78;
        addButton(bx, by, 80, bh, "LASSO", 11);
        bx += 88;
        addButton(bx, by, 80, bh, "BRUSH", 12);
        bx += 88;
        addButton(bx, by, 70, bh, "UNDO", 13);
        bx += 78;
        addButton(bx, by, 70, bh, "REDO", 14);
        bx += 78;
        addButton(bx, by, 58, bh, "ZOOM-", 15);
        bx += 66;
        addButton(bx, by, 58, bh, "ZOOM+", 16);
        bx += 66;
        addButton(bx, by, 60, bh, "DIFF", 17);
        bx += 68;
        addButton(bx, by, 80, bh, "PREVIEW", 18);
        addButton(56, 636, 88, 34, "HARM", 22);
        addButton(152, 636, 104, 34, "HISTORY", 23);
      }
      break;
    default:  // OUT
      addKnob(P_INPUT, x0, 350, 56);
      addKnob(P_OUTPUT, x0 + step, 350, 56);
      addKnob(P_DC_FREQ, x0 + 2 * step, 350, 56);
      addToggle(P_DC_ON, 140, 160, 150, 40, "DC FILTER");
      addButton(460, 160, 130, 40, "ANALYZE", 20);
      addButton(610, 160, 130, 40, "APPLY", 21);
      break;
  }
  if (state.tab != 5) {
    addButton(902, 160, 80, 40, "RESET", 60);
    addButton(990, 160, 96, 40, "PRESET", 70);
    if (state.tab != 6) {
      static const char* kAuditionLabelsShort[6] = {
          "DELTA", "DELTA", "SOLO HUM", "RESIDUAL", "SOLO VERB", "DELTA"};
      addButton(1090, 160, 84, 40, kAuditionLabelsShort[state.tab], 80);
    }
  } else {
    addButton(790, 650, 120, 38, "RESET", 60);
    addButton(918, 650, 120, 38, "PRESET", 70);
    addButton(1046, 650, 128, 38, "SPECTRAL Δ", 80);
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
  state.captureArmed = shared.captureActive.load(std::memory_order_relaxed);
  state.transportSeconds = shared.transportSeconds.load(std::memory_order_relaxed);
  state.transportValid = shared.transportValid.load(std::memory_order_relaxed);
  state.editCount = shared.editCount.load(std::memory_order_relaxed);
  state.sampleRate = shared.sampleRate.load(std::memory_order_relaxed);
  for (int& v : state.clipHist) {
    v = static_cast<int>(static_cast<double>(v) * 0.992);
  }
  int clipBin = static_cast<int>(
      std::clamp((state.inLevelL + 60.0f) / 60.0f, 0.0f, 1.0f) * 63.0f);
  state.clipHist[clipBin] += 1;
  const double thr = shared.get(P_CLIP_UPPER);
  state.clipRestoredPeakDb = static_cast<float>(
      std::min(0.0, thr + shared.get(P_CLIP_HEADROOM)));
  if (state.transportValid) {
    if (transportSeen_ && state.editCount > 0 &&
        std::fabs(state.transportSeconds - lastTransport_) > 2.0) {
      state.timelineStale = true;
    }
    lastTransport_ = state.transportSeconds;
    transportSeen_ = true;
  }
  if (state.editCount == 0) {
    state.timelineStale = false;
  } else if (!state.specValid) {
    state.timelineStale = true;
  }
  if (state.diffMode && state.editCount != diffEditCount_) {
    rebuildDiff(shared, state);
  }
  state.inHistory[state.inHistoryHead] = state.inLevelL;
  state.inHistoryHead = (state.inHistoryHead + 1) % PanelState::kHistory;
  state.outHistory[state.outHistoryHead] = state.outLevelL;
  state.outHistoryHead = (state.outHistoryHead + 1) % PanelState::kHistory;
  state.clipHistory[state.stageHead] =
      shared.clipActivity.load(std::memory_order_relaxed);
  state.clickHistory[state.stageHead] =
      static_cast<float>(shared.clickEvents.load(std::memory_order_relaxed));
  state.noiseHistory[state.stageHead] =
      shared.noiseGrDb.load(std::memory_order_relaxed);
  state.verbHistory[state.stageHead] =
      shared.verbGrDb.load(std::memory_order_relaxed);
  state.stageHead = (state.stageHead + 1) % PanelState::kHistory;
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
    state.specZoom = 1.0;
    state.specScroll = 0.0;
    lastEditL_.clear();
    lastEditR_.clear();
    undoStack_.clear();
    redoStack_.clear();
    diffValid_ = false;
    state.timelineStale = false;
    transportSeen_ = false;
    buildSpectrogram(state);
  }
}

void Panel::buildMagnitude(const std::vector<float>& left,
                           const std::vector<float>& right,
                           std::vector<float>* mag, float sampleRate) const {
  constexpr int kCols = 240;
  constexpr int kRows = 150;
  constexpr int kN = 1024;
  if (!mag) {
    return;
  }
  mag->assign(static_cast<size_t>(kCols) * kRows, -120.0f);
  const int n = static_cast<int>(left.size());
  if (n < 64) {
    return;
  }
  const float sr = sampleRate > 0.0f ? sampleRate : 48000.0f;
  Fft fft;
  fft.prepare(kN);
  std::vector<float> window(kN);
  for (int i = 0; i < kN; ++i) {
    window[static_cast<size_t>(i)] =
        0.5f - 0.5f * std::cos(2.0f * static_cast<float>(kPiD) * i / (kN - 1));
  }
  std::vector<float> frame(kN), re(kN), im(kN);
  for (int col = 0; col < kCols; ++col) {
    const int center = (n - 1) * col / (kCols - 1);
    const int start = center - kN / 2;
    for (int i = 0; i < kN; ++i) {
      const int at = start + i;
      float v = 0.0f;
      if (at >= 0 && at < n) {
        v = 0.5f * (left[static_cast<size_t>(at)] +
                    (right.empty() ? left[static_cast<size_t>(at)]
                                   : right[static_cast<size_t>(at)]));
      }
      frame[static_cast<size_t>(i)] = v * window[static_cast<size_t>(i)];
    }
    fft.forward(frame.data(), re.data(), im.data());
    for (int row = 0; row < kRows; ++row) {
      const float t = static_cast<float>(row) / (kRows - 1);
      const float hz = 20000.0f * std::pow(0.001f, t);
      int bin = static_cast<int>(hz * kN / sr);
      bin = std::clamp(bin, 1, kN / 2 - 1);
      const float m = std::sqrt(re[static_cast<size_t>(bin)] *
                                    re[static_cast<size_t>(bin)] +
                                im[static_cast<size_t>(bin)] *
                                    im[static_cast<size_t>(bin)]) /
                      static_cast<float>(kN);
      (*mag)[static_cast<size_t>(row * kCols + col)] =
          20.0f * std::log10(m + 1.0e-9f);
    }
  }
}

void Panel::buildSpectrogram(PanelState& state) {
  constexpr int kCols = 240;
  constexpr int kRows = 150;
  state.specW = kCols;
  state.specH = kRows;
  state.specPixels.assign(static_cast<size_t>(kCols) * kRows, 0xff0a0e12u);
  state.specMask.assign(static_cast<size_t>(kCols) * kRows, 0);
  state.specValid = false;
  state.specStartTime = captureStart_;
  if (captureL_.size() < 64) {
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  buildMagnitude(captureL_, captureR_, &specMag_, sr);
  for (int row = 0; row < kRows; ++row) {
    for (int col = 0; col < kCols; ++col) {
      const float db = specMag_[static_cast<size_t>(row * kCols + col)];
      const float range = std::max(12.0f, state.specRangeDb);
      float e = std::clamp((db + range) / range, 0.0f, 1.0f);
      e = std::pow(e, static_cast<float>(state.specGamma));
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
      const uint32_t rr =
          static_cast<uint32_t>(std::clamp(r, 0.0, 1.0) * 255.0);
      const uint32_t gg =
          static_cast<uint32_t>(std::clamp(g, 0.0, 1.0) * 255.0);
      const uint32_t bb =
          static_cast<uint32_t>(std::clamp(b, 0.0, 1.0) * 255.0);
      state.specPixels[static_cast<size_t>(row * kCols + col)] =
          0xff000000u | (rr << 16) | (gg << 8) | bb;
    }
  }
  state.specValid = true;
  diffValid_ = false;
}

void Panel::selectionTimes(const PanelState& state, double* t0, double* t1,
                           double* f0, double* f1) const {
  const double x0 = std::min(state.selX0, state.selX1);
  const double x1 = std::max(state.selX0, state.selX1);
  const double y0 = std::min(state.selY0, state.selY1);
  const double y1 = std::max(state.selY0, state.selY1);
  *t0 = plotTime(state, x0);
  *t1 = plotTime(state, x1);
  const double fa = plotHz(y0);
  const double fb = plotHz(y1);
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
  const bool preserve = shared.get(P_SPEC_PRESERVE) > 0.5;
  int lo = static_cast<int>(f0 * kN / sr);
  int hi = static_cast<int>(f1 * kN / sr);
  lo = std::clamp(lo, 1, kN / 2 - 2);
  hi = std::clamp(hi, lo + 1, kN / 2 - 1);

  // Patch copies adjacent material instead of doing a band edit.
  if (op == 3) {
    const int source = (i0 - len >= 0) ? i0 - len
                                       : ((i1 + len <= n) ? i1 : 0);
    std::vector<float> outL(static_cast<size_t>(len));
    std::vector<float> outR(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
      const int at = source + i;
      if (at >= 0 && at < n) {
        outL[static_cast<size_t>(i)] = captureL_[static_cast<size_t>(at)];
        outR[static_cast<size_t>(i)] = captureR_[static_cast<size_t>(at)];
      }
    }
    pushUndo(shared, "Patch");
    lastEditL_ = outL;
    lastEditR_ = outR;
    const double startTime = captureStart_ + static_cast<double>(i0) / sr;
    const double endTime = captureStart_ + static_cast<double>(i1) / sr;
    const bool ok = bridge->addEdit(startTime, endTime, outL.data(),
                                    outR.data(), len);
    state.editCount = bridge->editCount();
    diffValid_ = false;
    return ok;
  }

  const double totalDuration =
      static_cast<double>(n) / static_cast<double>(sr);
  const int maskW = state.specW;
  const int maskH = state.specH;
  const bool useMask = maskW > 0 && maskH > 0 &&
                       state.specMask.size() ==
                           static_cast<size_t>(maskW) * maskH;
  for (int start = 0; start + kN <= workLen; start += kHop) {
    int frameCol = -1;
    if (useMask && totalDuration > 0.0) {
      const double frameTime =
          captureStart_ +
          static_cast<double>(workStart + start + kN / 2) / sr;
      const double norm =
          (frameTime - state.specStartTime) / totalDuration;
      frameCol = static_cast<int>(norm * maskW);
      if (frameCol < 0 || frameCol >= maskW) {
        continue;
      }
    }
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
      if (useMask) {
        const float hz = static_cast<float>(k) * sr / kN;
        const double norm =
            std::log(std::max(20.0, static_cast<double>(hz)) / 20000.0) /
            std::log(0.001);
        const int row = static_cast<int>(std::clamp(norm, 0.0, 1.0) * maskH);
        const int rr = std::clamp(row, 0, maskH - 1);
        if (!state.specMask[static_cast<size_t>(rr * maskW + frameCol)]) {
          continue;
        }
      }
      float gain = 1.0f;
      if (op == 1) {
        gain = preserve ? std::pow(10.0f, -60.0f / 20.0f) : 0.0f;
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
  pushUndo(shared, kSpectralOpLabels[std::clamp(op, 0, 5)]);
  lastEditL_ = outL;
  lastEditR_ = outR;
  const double startTime = captureStart_ + static_cast<double>(i0) / sr;
  const double endTime = captureStart_ + static_cast<double>(i1) / sr;
  const bool ok = bridge->addEdit(startTime, endTime, outL.data(), outR.data(),
                                  len);
  state.editCount = bridge->editCount();
  diffValid_ = false;
  if (state.previewMode) {
    setPreview(shared, state);
  }
  return ok;
}


double Panel::plotHz(double y) const {
  const double t = std::clamp((y - kSpecY) / kSpecH, 0.0, 1.0);
  return 20000.0 * std::pow(0.001, t);
}

double Panel::hzToY(double hz) const {
  const double t = std::log(std::max(20.0, hz) / 20000.0) / std::log(0.001);
  return kSpecY + kSpecH * std::clamp(t, 0.0, 1.0);
}

double Panel::plotTime(const PanelState& state, double x) const {
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  const double zoom = std::max(1.0, state.specZoom);
  const double view = total / zoom;
  const double start = state.specStartTime +
                       state.specScroll * std::max(0.0, total - view);
  return start + (x - kSpecX) / kSpecW * view;
}

double Panel::timeToX(const PanelState& state, double t) const {
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  const double zoom = std::max(1.0, state.specZoom);
  const double view = total / zoom;
  const double start = state.specStartTime +
                       state.specScroll * std::max(0.0, total - view);
  return kSpecX + (t - start) / std::max(1.0e-9, view) * kSpecW;
}

void Panel::pushUndo(Shared& shared, const char* name) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge) {
    return;
  }
  std::vector<SpectralEdit> current;
  bridge->copyEdits(&current);
  undoStack_.push_back(std::move(current));
  undoNames_.emplace_back(name ? name : "Edit");
  if (undoStack_.size() > 20) {
    undoStack_.erase(undoStack_.begin());
    if (!undoNames_.empty()) {
      undoNames_.erase(undoNames_.begin());
    }
  }
  redoStack_.clear();
  redoNames_.clear();
}

void Panel::undo(Shared& shared, PanelState& state) {
  if (undoStack_.empty()) {
    return;
  }
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge) {
    return;
  }
  std::vector<SpectralEdit> current;
  bridge->copyEdits(&current);
  redoStack_.push_back(std::move(current));
  redoNames_.push_back(undoNames_.empty() ? "Edit" : undoNames_.back());
  auto previous = std::move(undoStack_.back());
  undoStack_.pop_back();
  if (!undoNames_.empty()) {
    undoNames_.pop_back();
  }
  bridge->replaceEdits(previous);
  state.editCount = bridge->editCount();
  state.hasSelection = false;
  diffValid_ = false;
  if (state.previewMode) {
    setPreview(shared, state);
  }
}

void Panel::redo(Shared& shared, PanelState& state) {
  if (redoStack_.empty()) {
    return;
  }
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge) {
    return;
  }
  std::vector<SpectralEdit> current;
  bridge->copyEdits(&current);
  undoStack_.push_back(std::move(current));
  undoNames_.push_back(redoNames_.empty() ? "Edit" : redoNames_.back());
  auto next = std::move(redoStack_.back());
  redoStack_.pop_back();
  if (!redoNames_.empty()) {
    redoNames_.pop_back();
  }
  bridge->replaceEdits(next);
  state.editCount = bridge->editCount();
  state.hasSelection = false;
  diffValid_ = false;
  if (state.previewMode) {
    setPreview(shared, state);
  }
}

void Panel::setPreview(Shared& shared, PanelState& state) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge) {
    state.previewMode = false;
    return;
  }
  if (!state.previewMode) {
    bridge->clearPreview();
    return;
  }
  if (captureL_.empty()) {
    state.previewMode = false;
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  int i0 = 0;
  int i1 = static_cast<int>(captureL_.size());
  if (state.hasSelection) {
    double t0 = 0.0;
    double t1 = 0.0;
    double f0 = 0.0;
    double f1 = 0.0;
    selectionTimes(state, &t0, &t1, &f0, &f1);
    (void)f0;
    (void)f1;
    i0 = static_cast<int>((t0 - captureStart_) * sr + 0.5);
    i1 = static_cast<int>((t1 - captureStart_) * sr + 0.5);
  }
  i0 = std::clamp(i0, 0, static_cast<int>(captureL_.size()));
  i1 = std::clamp(i1, i0, static_cast<int>(captureL_.size()));
  if (i1 - i0 < 16) {
    state.previewMode = false;
    return;
  }
  const int len = i1 - i0;
  std::vector<float> pl(static_cast<size_t>(len));
  std::vector<float> pr(static_cast<size_t>(len));
  for (int i = 0; i < len; ++i) {
    pl[static_cast<size_t>(i)] = captureL_[static_cast<size_t>(i0 + i)];
    pr[static_cast<size_t>(i)] = captureR_[static_cast<size_t>(i0 + i)];
  }
  // Apply stored edits to the preview copy.
  std::vector<SpectralEdit> edits;
  bridge->copyEdits(&edits);
  for (const auto& edit : edits) {
    const int start = static_cast<int>((edit.startTime - captureStart_) * sr);
    const int count = static_cast<int>(edit.left.size());
    for (int i = 0; i < count; ++i) {
      const int at = start + i - i0;
      if (at >= 0 && at < len) {
        pl[static_cast<size_t>(at)] = edit.left[static_cast<size_t>(i)];
        pr[static_cast<size_t>(at)] = edit.right[static_cast<size_t>(i)];
      }
    }
  }
  // Short fades make the loop boundary inaudible.
  const int fade = std::min(len / 2, static_cast<int>(0.005f * sr));
  for (int i = 0; i < fade; ++i) {
    const float g = static_cast<float>(i) / std::max(1, fade);
    pl[static_cast<size_t>(i)] *= g;
    pr[static_cast<size_t>(i)] *= g;
    pl[static_cast<size_t>(len - 1 - i)] *= g;
    pr[static_cast<size_t>(len - 1 - i)] *= g;
  }
  bridge->setPreview(pl.data(), pr.data(), len);
}

void Panel::matchEdits(Shared& shared, PanelState& state) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge || lastEditL_.empty() || !state.specValid ||
      state.specW <= 0 || state.specH <= 0) {
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total = static_cast<double>(captureL_.size()) / sr;
  const int durationFrames = static_cast<int>(lastEditL_.size());
  const double duration = static_cast<double>(durationFrames) / sr;
  if (total <= 0.0 || duration <= 0.0) {
    return;
  }
  const int row0 = static_cast<int>((std::min(state.selY0, state.selY1) -
                                     kSpecY) / kSpecH * state.specH);
  const int row1 = static_cast<int>((std::max(state.selY0, state.selY1) -
                                     kSpecY) / kSpecH * state.specH);
  const int r0 = std::clamp(std::min(row0, row1), 0, state.specH - 1);
  const int r1 = std::clamp(std::max(row0, row1), 0, state.specH - 1);
  const int col0 = static_cast<int>(
      (plotTime(state, std::min(state.selX0, state.selX1)) -
       state.specStartTime) / std::max(1.0e-9, total) * state.specW);
  const int col1 = static_cast<int>(
      (plotTime(state, std::max(state.selX0, state.selX1)) -
       state.specStartTime) / std::max(1.0e-9, total) * state.specW);
  const int c0 = std::clamp(std::min(col0, col1), 0, state.specW - 1);
  const int c1 = std::clamp(std::max(col0, col1), 0, state.specW - 1);
  const int rows = r1 - r0 + 1;
  if (rows <= 0 || c1 < c0) {
    return;
  }

  // 2D spectral template: every selected column/row, not just an average
  // spectrum.  Correlation is normalised, so level differences do not matter.
  const int tw = c1 - c0 + 1;
  std::vector<float> tmpl(static_cast<size_t>(tw) * rows, 0.0f);
  float normT = 0.0f;
  for (int t = 0; t < tw; ++t) {
    for (int r = 0; r < rows; ++r) {
      const float v = std::pow(
          10.0, specMag_[static_cast<size_t>((r0 + r) * state.specW +
                                            (c0 + t))] /
                    20.0);
      tmpl[static_cast<size_t>(t * rows + r)] = v;
      normT += v * v;
    }
  }
  normT = std::sqrt(std::max(1.0e-12f, normT));

  struct Match {
    int col = 0;
    float score = 0.0f;
  };
  std::vector<Match> matches;
  if (tw > 0 && tw < state.specW) {
    std::vector<float> score(static_cast<size_t>(state.specW), 0.0f);
    for (int c = 0; c + tw <= state.specW; ++c) {
      float dot = 0.0f;
      float normC = 0.0f;
      for (int t = 0; t < tw; ++t) {
        for (int r = 0; r < rows; ++r) {
          const float v = std::pow(
              10.0, specMag_[static_cast<size_t>((r0 + r) * state.specW +
                                                (c + t))] /
                        20.0);
          dot += v * tmpl[static_cast<size_t>(t * rows + r)];
          normC += v * v;
        }
      }
      score[static_cast<size_t>(c)] =
          dot / (normT * std::sqrt(std::max(1.0e-12f, normC)));
    }
    for (int c = 2; c + tw < state.specW - 2; ++c) {
      const float v = score[static_cast<size_t>(c)];
      if (v < 0.88f || v < score[static_cast<size_t>(c - 1)] ||
          v < score[static_cast<size_t>(c + 1)]) {
        continue;
      }
      matches.push_back({c, v});
    }
  }
  std::sort(matches.begin(), matches.end(),
            [](const Match& a, const Match& b) { return a.score > b.score; });

  std::vector<SpectralEdit> added;
  double lastTime = -1.0e9;
  for (const Match& m : matches) {
    const double t = state.specStartTime +
                     static_cast<double>(m.col) / state.specW * total;
    if (t < state.specStartTime + duration * 1.2 ||
        t + duration > state.specStartTime + total ||
        t - lastTime < duration * 1.2) {
      continue;
    }
    SpectralEdit edit;
    edit.startTime = t;
    edit.endTime = t + duration;
    edit.left = lastEditL_;
    edit.right = lastEditR_;
    added.push_back(std::move(edit));
    lastTime = t;
    if (added.size() >= 8) {
      break;
    }
  }
  if (added.empty()) {
    return;
  }
  pushUndo(shared, "Match");
  for (const auto& edit : added) {
    bridge->addEdit(edit.startTime, edit.endTime, edit.left.data(),
                    edit.right.data(), static_cast<int>(edit.left.size()));
  }
  state.editCount = bridge->editCount();
  diffValid_ = false;
  if (state.previewMode) {
    setPreview(shared, state);
  }
}

void Panel::rebuildDiff(Shared& shared, PanelState& state) {
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge || !state.specValid || state.specW <= 0 || state.specH <= 0 ||
      captureL_.empty()) {
    diffValid_ = false;
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  editedL_ = captureL_;
  editedR_ = captureR_;
  std::vector<SpectralEdit> edits;
  bridge->copyEdits(&edits);
  for (const auto& edit : edits) {
    const int start = static_cast<int>((edit.startTime - captureStart_) * sr);
    const int count = static_cast<int>(edit.left.size());
    for (int i = 0; i < count; ++i) {
      const int at = start + i;
      if (at >= 0 && at < static_cast<int>(editedL_.size())) {
        editedL_[static_cast<size_t>(at)] = edit.left[static_cast<size_t>(i)];
        editedR_[static_cast<size_t>(at)] = edit.right[static_cast<size_t>(i)];
      }
    }
  }
  std::vector<float> editedMag;
  buildMagnitude(editedL_, editedR_, &editedMag, sr);
  if (editedMag.size() != specMag_.size()) {
    diffValid_ = false;
    return;
  }
  diffPixels_.assign(static_cast<size_t>(state.specW) * state.specH,
                     0xff080b0eu);
  for (size_t i = 0; i < specMag_.size(); ++i) {
    const float diff = std::fabs(specMag_[i] - editedMag[i]);
    float e = std::clamp(diff / 30.0f, 0.0f, 1.0f);
    e = std::pow(e, 0.7f);
    const uint32_t rr =
        static_cast<uint32_t>((0.02 + 0.93 * e) * 255.0);
    const uint32_t gg =
        static_cast<uint32_t>((0.03 + 0.58 * e) * 255.0);
    const uint32_t bb =
        static_cast<uint32_t>((0.04 + 0.12 * e) * 255.0);
    diffPixels_[i] = 0xff000000u | (rr << 16) | (gg << 8) | bb;
  }
  diffValid_ = true;
  diffEditCount_ = state.editCount;
}

void Panel::fillSelectionMask(PanelState& state) {
  if (state.specW <= 0 || state.specH <= 0) {
    return;
  }
  if (state.specMask.size() !=
      static_cast<size_t>(state.specW) * state.specH) {
    state.specMask.assign(static_cast<size_t>(state.specW) * state.specH, 0);
  } else {
    std::fill(state.specMask.begin(), state.specMask.end(), 0);
  }
  const double x0 = std::clamp(std::min(state.selX0, state.selX1), kSpecX,
                               kSpecX + kSpecW);
  const double x1 = std::clamp(std::max(state.selX0, state.selX1), kSpecX,
                               kSpecX + kSpecW);
  const double y0 = std::clamp(std::min(state.selY0, state.selY1), kSpecY,
                               kSpecY + kSpecH);
  const double y1 = std::clamp(std::max(state.selY0, state.selY1), kSpecY,
                               kSpecY + kSpecH);
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  const int c0 = static_cast<int>(
      (plotTime(state, x0) - state.specStartTime) / std::max(1.0e-9, total) *
      state.specW);
  const int c1 = static_cast<int>(
      (plotTime(state, x1) - state.specStartTime) / std::max(1.0e-9, total) *
      state.specW);
  const int r0 = static_cast<int>((y0 - kSpecY) / kSpecH * state.specH);
  const int r1 = static_cast<int>((y1 - kSpecY) / kSpecH * state.specH);
  for (int r = std::max(0, r0); r <= std::min(state.specH - 1, r1); ++r) {
    for (int c = std::max(0, c0); c <= std::min(state.specW - 1, c1); ++c) {
      state.specMask[static_cast<size_t>(r * state.specW + c)] = 1;
    }
  }
  state.hasSelection = true;
  updateSelectionBoundsFromMask(state);
}

void Panel::paintBrush(PanelState& state, double x, double y) {
  if (state.specW <= 0 || state.specH <= 0) {
    return;
  }
  if (state.specMask.size() !=
      static_cast<size_t>(state.specW) * state.specH) {
    state.specMask.assign(static_cast<size_t>(state.specW) * state.specH, 0);
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  const int cx = static_cast<int>(
      (plotTime(state, x) - state.specStartTime) / std::max(1.0e-9, total) *
      state.specW);
  const int cy = static_cast<int>((y - kSpecY) / kSpecH * state.specH);
  const int radius = 5;
  for (int r = cy - radius; r <= cy + radius; ++r) {
    if (r < 0 || r >= state.specH) {
      continue;
    }
    for (int c = cx - radius; c <= cx + radius; ++c) {
      if (c < 0 || c >= state.specW) {
        continue;
      }
      const int dx = c - cx;
      const int dy = r - cy;
      if (dx * dx + dy * dy <= radius * radius) {
        state.specMask[static_cast<size_t>(r * state.specW + c)] = 1;
      }
    }
  }
  state.hasSelection = true;
  state.selecting = true;
  updateSelectionBoundsFromMask(state);
}

void Panel::selectLasso(PanelState& state) {
  if (state.specW <= 0 || state.specH <= 0 || lassoPoints_.size() < 3) {
    return;
  }
  if (state.specMask.size() !=
      static_cast<size_t>(state.specW) * state.specH) {
    state.specMask.assign(static_cast<size_t>(state.specW) * state.specH, 0);
  }
  std::vector<std::pair<double, double>> pts;
  pts.reserve(lassoPoints_.size());
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  for (const auto& p : lassoPoints_) {
    const int col = static_cast<int>(
        (plotTime(state, p.first) - state.specStartTime) /
        std::max(1.0e-9, total) * state.specW);
    pts.emplace_back(col, (p.second - kSpecY) / kSpecH * state.specH);
  }
  int minC = state.specW;
  int maxC = 0;
  int minR = state.specH;
  int maxR = 0;
  for (const auto& p : pts) {
    minC = std::min(minC, static_cast<int>(p.first));
    maxC = std::max(maxC, static_cast<int>(p.first));
    minR = std::min(minR, static_cast<int>(p.second));
    maxR = std::max(maxR, static_cast<int>(p.second));
  }
  for (int r = std::max(0, minR); r <= std::min(state.specH - 1, maxR); ++r) {
    for (int c = std::max(0, minC); c <= std::min(state.specW - 1, maxC); ++c) {
      bool inside = false;
      for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const double xi = pts[i].first;
        const double yi = pts[i].second;
        const double xj = pts[j].first;
        const double yj = pts[j].second;
        if (((yi > r) != (yj > r)) &&
            (c < (xj - xi) * (r - yi) / (yj - yi + 1.0e-12) + xi)) {
          inside = !inside;
        }
      }
      if (inside) {
        state.specMask[static_cast<size_t>(r * state.specW + c)] = 1;
      }
    }
  }
  state.hasSelection = true;
  updateSelectionBoundsFromMask(state);
}

void Panel::updateSelectionBoundsFromMask(PanelState& state) {
  if (state.specW <= 0 || state.specH <= 0 ||
      state.specMask.size() !=
          static_cast<size_t>(state.specW) * state.specH) {
    return;
  }
  int minC = state.specW;
  int maxC = -1;
  int minR = state.specH;
  int maxR = -1;
  for (int r = 0; r < state.specH; ++r) {
    for (int c = 0; c < state.specW; ++c) {
      if (state.specMask[static_cast<size_t>(r * state.specW + c)]) {
        minC = std::min(minC, c);
        maxC = std::max(maxC, c);
        minR = std::min(minR, r);
        maxR = std::max(maxR, r);
      }
    }
  }
  if (maxC < 0) {
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total =
      static_cast<double>(captureL_.size()) / static_cast<double>(sr);
  const double t0 = state.specStartTime +
                    static_cast<double>(minC) / state.specW * total;
  const double t1 = state.specStartTime +
                    static_cast<double>(maxC + 1) / state.specW * total;
  state.selX0 = timeToX(state, t0);
  state.selX1 = timeToX(state, t1);
  state.selY0 = kSpecY + static_cast<double>(minR) / state.specH * kSpecH;
  state.selY1 = kSpecY + static_cast<double>(maxR + 1) / state.specH * kSpecH;
}



void Panel::drawStageVisual(cairo_t* cr, const Shared& shared,
                            const PanelState& state, int page) const {
  const double x = 56.0;
  const double y = page == 6 ? 470.0 : 420.0;
  const double w = kPanelWidth - 112.0;
  const double h = page == 6 ? 140.0 : 190.0;
  roundRect(cr, x, y, w, h, 6);
  setRgb(cr, Rgb{0.035, 0.055, 0.060});
  cairo_fill(cr);
  setRgb(cr, kEdge, 0.8);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 6);
  cairo_stroke(cr);

  const int head = state.stageHead;
  const int latest = (head + PanelState::kHistory - 1) % PanelState::kHistory;
  auto at = [&](const float* hist, int i) {
    return hist[(head + i) % PanelState::kHistory];
  };
  auto drawHistory = [&](const float* hist, double lo, double hi, Rgb color,
                         bool filled) {
    cairo_new_path(cr);
    for (int i = 0; i < PanelState::kHistory; ++i) {
      const double px = x + 12 + (w - 24) * i / (PanelState::kHistory - 1);
      const double t = std::clamp((at(hist, i) - lo) / (hi - lo), 0.0, 1.0);
      const double py = y + h - 18 - t * (h - 48);
      if (i == 0) {
        cairo_move_to(cr, px, py);
      } else {
        cairo_line_to(cr, px, py);
      }
    }
    if (filled) {
      cairo_line_to(cr, x + w - 12, y + h - 18);
      cairo_line_to(cr, x + 12, y + h - 18);
      cairo_close_path(cr);
      setRgb(cr, color, 0.18);
      cairo_fill_preserve(cr);
    }
    setRgb(cr, color, 0.85);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
  };

  const auto& presets = modulePresets()[static_cast<size_t>(page)];
  const int presetIdx = std::clamp(
      state.modulePreset[page], 0, static_cast<int>(presets.size()) - 1);
  char moduleName[80];
  std::snprintf(moduleName, sizeof(moduleName), "MODULE: %s",
                presets[static_cast<size_t>(presetIdx)].name);
  text(cr, moduleName, x + w - 14, y + 18, 10, 2, kDim.r, kDim.g, kDim.b);

  char label[220];
  if (page == 0) {
    text(cr, "CLIP HISTOGRAM / DRAG THRESHOLDS", x + 14, y + 18, 10, 0,
         kDim.r, kDim.g, kDim.b);
    int peakHist = 1;
    for (int v : state.clipHist) {
      peakHist = std::max(peakHist, v);
    }
    const double plotTop = y + 34;
    const double plotBottom = y + h - 30;
    for (int i = 0; i < 64; ++i) {
      const double bx = x + 14 + (w - 28) * i / 64.0;
      const double bh = (plotBottom - plotTop) *
                        static_cast<double>(state.clipHist[i]) / peakHist;
      setRgb(cr, i >= 48 ? kAmber : kAccent,
             i >= 48 ? 0.55 : 0.42);
      cairo_rectangle(cr, bx, plotBottom - bh, (w - 28) / 64.0 - 1.0,
                      std::max(0.5, bh));
      cairo_fill(cr);
    }
    auto thresholdY = [&](double db) {
      return plotBottom -
             std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * (plotBottom - plotTop);
    };
    auto handle = [&](double db, Rgb color, const char* name) {
      const double ty = thresholdY(db);
      setRgb(cr, color, 0.9);
      cairo_set_line_width(cr, 1.4);
      cairo_move_to(cr, x + 14, ty);
      cairo_line_to(cr, x + w - 14, ty);
      cairo_stroke(cr);
      setRgb(cr, color);
      cairo_rectangle(cr, x + 4, ty - 5, 8, 10);
      cairo_fill(cr);
      char t[40];
      std::snprintf(t, sizeof(t), "%s %+.1f", name, db);
      text(cr, t, x + w - 18, ty - 4, 9, 2, color.r, color.g, color.b);
    };
    const double clipUpper = shared.get(P_CLIP_UPPER);
    const double clipLower = shared.get(P_CLIP_LOWER);
    const bool sameThreshold = std::fabs(clipUpper - clipLower) < 0.05;
    handle(clipUpper, kGold, sameThreshold ? "CLIP" : "UP");
    if (!sameThreshold) {
      handle(clipLower, kAmber, "LO");
    }
    std::snprintf(label, sizeof(label),
                  "clip %.2f%%   target peak %.1f dB   %s",
                  shared.analysisClipPct.load(std::memory_order_relaxed),
                  state.clipRestoredPeakDb,
                  state.clipHistory[latest] > 0.5f ? "DETECTED" : "CLEAR");
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
  } else if (page == 1) {
    text(cr, "CLICK EVENTS", x + 14, y + 18, 10, 0, kDim.r, kDim.g, kDim.b);
    setRgb(cr, kAccent, 0.75);
    for (int i = 0; i < PanelState::kHistory; ++i) {
      const double px = x + 12 + 470.0 * i / (PanelState::kHistory - 1);
      const double t = std::clamp(at(state.clickHistory, i) / 12.0, 0.0, 1.0);
      const double py = y + h - 18 - t * (h - 48);
      cairo_move_to(cr, px, y + h - 18);
      cairo_line_to(cr, px, py);
      cairo_stroke(cr);
    }
    const int count = shared.clickEventCount.load(std::memory_order_relaxed);
    text(cr, "TIME       WIDTH     PEAK", x + 530, y + 38, 9, 0, kDim.r,
         kDim.g, kDim.b);
    for (int i = 0; i < std::min(count, 6); ++i) {
      const float t =
          shared.clickEventTime[i].load(std::memory_order_relaxed);
      const float wdt =
          shared.clickEventWidth[i].load(std::memory_order_relaxed);
      const float pk = shared.clickEventPeak[i].load(std::memory_order_relaxed);
      std::snprintf(label, sizeof(label), "%8.3f s   %5.1f ms   %+6.1f dB",
                    t, wdt, pk);
      text(cr, label, x + 530, y + 56 + i * 18, 10, 0, kText.r, kText.g,
           kText.b);
    }
    if (count == 0) {
      text(cr, "no events in the last block", x + 530, y + 56, 10, 0,
           kDim.r, kDim.g, kDim.b);
    }
    std::snprintf(label, sizeof(label), "latest %d event%s",
                  static_cast<int>(state.clickHistory[latest]),
                  state.clickHistory[latest] == 1 ? "" : "s");
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
    text(cr, "click a row to solo removed clicks", x + w - 14, y + h - 6, 10,
         2, kDim.r, kDim.g, kDim.b);
  } else if (page == 2) {
    text(cr, "HUM HARMONIC TABLE", x + 14, y + 18, 10, 0, kDim.r, kDim.g,
         kDim.b);
    float hz = shared.humDetectedHz.load(std::memory_order_relaxed);
    if (hz <= 0.0f) {
      hz = static_cast<float>(shared.get(P_HUM_FREQ));
    }
    const int harmonics = std::clamp(
        static_cast<int>(shared.get(P_HUM_HARMONICS) + 0.5), 1, 24);
    for (int k = 1; k <= harmonics; ++k) {
      const double f = hz * k;
      if (f > 1000.0) {
        break;
      }
      const double px =
          x + 20 + 270.0 * (std::log(f / 20.0) / std::log(50.0));
      setRgb(cr, kAccent, k == 1 ? 0.95 : 0.45);
      cairo_set_line_width(cr, k == 1 ? 2.0 : 1.0);
      cairo_move_to(cr, px, y + 34);
      cairo_line_to(cr, px, y + h - 26);
      cairo_stroke(cr);
    }
    const uint64_t mask = shared.humMask.load(std::memory_order_relaxed);
    for (int idx = 0; idx < harmonics; ++idx) {
      const int col = idx / 8;
      const int row = idx % 8;
      const double bx = x + 300 + col * 190.0;
      const double by = y + 34 + row * 16.0;
      const bool on = ((mask >> idx) & 1ull) != 0;
      roundRect(cr, bx, by, 12, 12, 2);
      setRgb(cr, on ? kAccent : Rgb{0.05, 0.07, 0.08});
      cairo_fill(cr);
      setRgb(cr, kEdge, 0.7);
      cairo_set_line_width(cr, 1.0);
      roundRect(cr, bx + 0.5, by + 0.5, 11, 11, 2);
      cairo_stroke(cr);
      std::snprintf(label, sizeof(label), "%2d  %6.1f Hz", idx + 1,
                    static_cast<double>(hz) * (idx + 1));
      text(cr, label, bx + 18, by + 10, 8.5, 0, on ? kText.r : kDim.r,
           on ? kText.g : kDim.g, on ? kText.b : kDim.b);
    }
    std::snprintf(label, sizeof(label),
                  "%.1f Hz   %d harmonics   %.0f dB deep   click a row to enable",
                  hz, harmonics, shared.get(P_HUM_DEPTH));
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
  } else if (page == 3) {
    text(cr, "NOISE PROFILE / TARGET REDUCTION", x + 14, y + 18, 10, 0,
         kDim.r, kDim.g, kDim.b);
    float profile[64] = {};
    const int bins = shared.profileBins.load(std::memory_order_relaxed);
    const int validBins = std::clamp(bins, 0, repair::kProfileBins);
    const int total = shared.profileValid.load(std::memory_order_relaxed)
                          ? std::min(validBins, 64)
                          : 0;
    if (total > 0) {
      for (int i = 0; i < total; ++i) {
        const int at = std::min(
            validBins - 1,
            i * (validBins - 1) / std::max(1, total - 1));
        profile[i] = shared.noiseProfile[static_cast<size_t>(at)].load(
            std::memory_order_relaxed);
      }
      auto curve = [&](float offsetDb, Rgb color, bool fill) {
        cairo_new_path(cr);
        for (int i = 0; i < total; ++i) {
          const double px = x + 14 + (w - 28) * i / std::max(1, total - 1);
          const double db = 20.0 * std::log10(profile[i] + 1.0e-12) + offsetDb;
          const double t = std::clamp((db + 120.0) / 100.0, 0.0, 1.0);
          const double py = y + h - 24 - t * (h - 54);
          if (i == 0) {
            cairo_move_to(cr, px, py);
          } else {
            cairo_line_to(cr, px, py);
          }
        }
        if (fill) {
          cairo_line_to(cr, x + w - 14, y + h - 24);
          cairo_line_to(cr, x + 14, y + h - 24);
          cairo_close_path(cr);
          setRgb(cr, color, 0.12);
          cairo_fill_preserve(cr);
        }
        setRgb(cr, color, 0.9);
        cairo_set_line_width(cr, 1.3);
        cairo_stroke(cr);
      };
      curve(0.0f, kAccent, true);
      curve(-static_cast<float>(shared.get(P_NOISE_REDUCTION)), kGold, false);
    } else {
      text(cr, "learn a profile to see the noise curve", x + w * 0.5,
           y + h * 0.5, 12, 1, kDim.r, kDim.g, kDim.b);
    }
    drawHistory(state.noiseHistory, 0.0, 30.0, kGreen, false);
    std::snprintf(label, sizeof(label),
                  "%.1f dB   profile %s   %d bins", state.noiseHistory[latest],
                  shared.profileValid.load(std::memory_order_relaxed) ? "ready"
                                                                     : "not learned",
                  validBins);
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
  } else if (page == 4) {
    text(cr, "REVERB TAIL REDUCTION", x + 14, y + 18, 10, 0,
         kDim.r, kDim.g, kDim.b);
    drawHistory(state.verbHistory, 0.0, 20.0, kAccent, true);
    std::snprintf(label, sizeof(label), "%.1f dB   tail %.2f s   pre %.0f ms",
                  state.verbHistory[latest],
                  shared.get(P_VERB_TAIL), shared.get(P_VERB_PREDELAY));
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
  } else {
    text(cr, "INPUT / OUTPUT", x + 14, y + 18, 10, 0,
         kDim.r, kDim.g, kDim.b);
    auto levelBar = [&](const char* name, float db, double py) {
      const double t = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
      setRgb(cr, Rgb{0.06, 0.09, 0.10});
      cairo_rectangle(cr, x + 90, py, w - 140, 12);
      cairo_fill(cr);
      setRgb(cr, kAccent, 0.8);
      cairo_rectangle(cr, x + 90, py, (w - 140) * t, 12);
      cairo_fill(cr);
      text(cr, name, x + 14, py + 11, 10, 0, kDim.r, kDim.g, kDim.b);
    };
    levelBar("IN", state.inLevelL, y + 50);
    levelBar("OUT", state.outLevelL, y + 82);
    std::snprintf(label, sizeof(label), "peak in %.1f dB   peak out %.1f dB",
                  state.inLevelL, state.outLevelL);
    text(cr, label, x + 14, y + h - 6, 10, 0, kDim.r, kDim.g, kDim.b);
  }
}

void Panel::drawAnalysis(cairo_t* cr, const Shared& shared,
                         const PanelState& state) const {
  const bool ready = shared.analysisReady.load(std::memory_order_relaxed);
  const float progress =
      shared.analysisProgress.load(std::memory_order_relaxed);
  const float humHz = shared.analysisHumHz.load(std::memory_order_relaxed);
  const float humDb = shared.analysisHumDb.load(std::memory_order_relaxed);
  const float noiseDb = shared.analysisNoiseDb.load(std::memory_order_relaxed);
  const float clipPct = shared.analysisClipPct.load(std::memory_order_relaxed);
  const float clicks =
      shared.analysisClickRate.load(std::memory_order_relaxed);
  const float reverb = shared.analysisReverbSec.load(std::memory_order_relaxed);
  char line[220];
  if (progress > 0.0f && !ready) {
    std::snprintf(line, sizeof(line), "ANALYZING  %.0f%%",
                  std::clamp(progress, 0.0f, 1.0f) * 100.0f);
  } else if (!ready) {
    std::snprintf(line, sizeof(line), "ANALYZE 3 s OF INPUT");
  } else {
    std::snprintf(line, sizeof(line), "REVIEW SUGGESTIONS  ·  APPLY CHECKED");
  }
  text(cr, line, 140, 214, 13, 0, ready ? kText.r : kDim.r,
       ready ? kText.g : kDim.g, ready ? kText.b : kDim.b);
  if (progress > 0.0f && !ready) {
    setRgb(cr, Rgb{0.06, 0.09, 0.10});
    cairo_rectangle(cr, 140, 224, 600, 6);
    cairo_fill(cr);
    setRgb(cr, kAccent);
    cairo_rectangle(cr, 140, 224, 600 * std::clamp(progress, 0.0f, 1.0f), 6);
    cairo_fill(cr);
  }
  if (ready) {
    const char* names[5] = {"HUM", "NOISE", "CLIP", "CLICKS", "REVERB"};
    char values[5][80];
    if (humHz > 0.0f) {
      std::snprintf(values[0], sizeof(values[0]), "%.0f Hz  %.0f dB", humHz,
                    humDb);
    } else {
      std::snprintf(values[0], sizeof(values[0]), "none");
    }
    std::snprintf(values[1], sizeof(values[1]), "%.0f dB", noiseDb);
    std::snprintf(values[2], sizeof(values[2]), "%.2f%%", clipPct);
    std::snprintf(values[3], sizeof(values[3]), "%.1f /s", clicks);
    std::snprintf(values[4], sizeof(values[4]), "%.2f s", reverb);
    for (int i = 0; i < 5; ++i) {
      const double y = kAnalysisRowY + i * kAnalysisRowH;
      const bool on = state.analysisApply[i];
      roundRect(cr, 140, y, 18, 18, 3);
      setRgb(cr, on ? kAccent : Rgb{0.06, 0.08, 0.09});
      cairo_fill(cr);
      setRgb(cr, kEdge, 0.8);
      cairo_set_line_width(cr, 1.0);
      roundRect(cr, 140.5, y + 0.5, 17, 17, 3);
      cairo_stroke(cr);
      if (on) {
        setRgb(cr, Rgb{0.02, 0.05, 0.05});
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, 144, y + 9);
        cairo_line_to(cr, 148, y + 14);
        cairo_line_to(cr, 155, y + 4);
        cairo_stroke(cr);
      }
      text(cr, names[i], 172, y + 14, 11, 0, kText.r, kText.g, kText.b);
      text(cr, values[i], 282, y + 14, 11, 0, kDim.r, kDim.g, kDim.b);
    }
    text(cr, "click a checkbox to include or skip that module",
         140, kAnalysisRowY + 5 * kAnalysisRowH + 16, 10, 0, kDim.r, kDim.g,
         kDim.b);
  } else {
    text(cr, "ANALYZE measures the raw input; review each suggestion before "
             "APPLY",
         140, 252, 11, 0, kDim.r, kDim.g, kDim.b);
  }
}

void Panel::drawMonitorStrip(cairo_t* cr, const Shared& shared,
                             const PanelState& state) const {
  (void)state;
  const int monitor = static_cast<int>(shared.get(P_MONITOR) + 0.5);
  text(cr, "MONITOR", kMonX, kMonY + 18, 10, 0, kDim.r, kDim.g, kDim.b);
  const char* names[3] = {"OUTPUT", "INPUT", "DELTA"};
  for (int i = 0; i < 3; ++i) {
    const double x = kMonX + 66 + i * kMonSegW;
    roundRect(cr, x, kMonY, kMonSegW - 4, kMonH, 4);
    setRgb(cr, monitor == i ? kAccent : Rgb{0.07, 0.095, 0.102});
    cairo_fill(cr);
    setRgb(cr, monitor == i ? Rgb{0.02, 0.05, 0.05} : kDim,
           monitor == i ? 1.0 : 0.95);
    text(cr, names[i], x + (kMonSegW - 4) * 0.5, kMonY + 18, 10, 1,
         monitor == i ? 0.02 : kDim.r, monitor == i ? 0.05 : kDim.g,
         monitor == i ? 0.05 : kDim.b);
  }
  const bool ab = shared.compare.load(std::memory_order_relaxed);
  roundRect(cr, kMonABX, kMonY, kMonABW, kMonH, 4);
  setRgb(cr, ab ? kGold : Rgb{0.07, 0.095, 0.102});
  cairo_fill(cr);
  text(cr, "A/B", kMonABX + kMonABW * 0.5, kMonY + 18, 10, 1,
       ab ? 0.08 : kDim.r, ab ? 0.07 : kDim.g, ab ? 0.02 : kDim.b);
}

bool Panel::monitorHit(double x, double y, int* index) const {
  if (y < kMonY || y > kMonY + kMonH) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    const double sx = kMonX + 66 + i * kMonSegW;
    if (x >= sx && x <= sx + kMonSegW - 4) {
      if (index) {
        *index = i;
      }
      return true;
    }
  }
  if (x >= kMonABX && x <= kMonABX + kMonABW) {
    if (index) {
      *index = 3;
    }
    return true;
  }
  return false;
}

bool Panel::clipHistogramHit(const Shared& shared, double x, double y,
                             int* which) const {
  if (x < 56 || x > kPanelWidth - 56 || y < 420 || y > 610) {
    return false;
  }
  auto thresholdY = [](double db) {
    return 420.0 + 190.0 - 18.0 -
           std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * (190.0 - 48.0);
  };
  const double upper = thresholdY(shared.get(P_CLIP_UPPER));
  const double lower = thresholdY(shared.get(P_CLIP_LOWER));
  if (std::fabs(y - upper) <= 8.0) {
    if (which) {
      *which = 1;
    }
    return true;
  }
  if (std::fabs(y - lower) <= 8.0) {
    if (which) {
      *which = 2;
    }
    return true;
  }
  return false;
}

bool Panel::humTableHit(const PanelState& state, double x, double y,
                        int* harmonic) const {
  (void)state;
  if (x < 356 || x > 926 || y < 454 || y > 586) {
    return false;
  }
  const int col = static_cast<int>((x - 356) / 190.0);
  const int row = static_cast<int>((y - 454) / 16.0);
  if (col < 0 || col > 2 || row < 0 || row > 7) {
    return false;
  }
  if (harmonic) {
    *harmonic = col * 8 + row;
  }
  return true;
}

void Panel::applyAnalysis(Shared& shared, const PanelState& state) {
  if (!shared.analysisReady.load(std::memory_order_relaxed)) {
    return;
  }
  const float humHz = shared.analysisHumHz.load(std::memory_order_relaxed);
  const float humDb = shared.analysisHumDb.load(std::memory_order_relaxed);
  const float noiseDb = shared.analysisNoiseDb.load(std::memory_order_relaxed);
  const float clipPct = shared.analysisClipPct.load(std::memory_order_relaxed);
  const float clicks =
      shared.analysisClickRate.load(std::memory_order_relaxed);
  const float reverb = shared.analysisReverbSec.load(std::memory_order_relaxed);

  if (state.analysisApply[0] && humHz > 0.0f && humDb > -60.0f) {
    shared.set(P_HUM_ON, 1.0);
    shared.set(P_HUM_AUTO, 0.0);
    shared.set(P_HUM_FREQ, std::clamp(static_cast<double>(humHz), 40.0, 70.0));
    shared.set(P_HUM_HARMONICS, 8.0);
    shared.set(P_HUM_DEPTH, std::clamp(static_cast<double>(-humDb + 6.0f), 12.0, 60.0));
  } else if (state.analysisApply[0]) {
    shared.set(P_HUM_ON, 0.0);
  }
  if (state.analysisApply[1] && noiseDb < -45.0f) {
    shared.set(P_NOISE_ON, 1.0);
    shared.set(P_NOISE_REDUCTION,
               std::clamp(static_cast<double>(-noiseDb - 30.0f), 6.0, 24.0));
  } else if (state.analysisApply[1]) {
    shared.set(P_NOISE_ON, 0.0);
  }
  if (state.analysisApply[2]) {
    shared.set(P_CLIP_ON, clipPct > 0.01f ? 1.0 : 0.0);
    shared.set(P_CLIP_AUTO, 1.0);
  }
  if (state.analysisApply[3] && clicks > 0.3f) {
    shared.set(P_CLICK_ON, 1.0);
    shared.set(P_CLICK_SENS, 5.0);
  } else if (state.analysisApply[3]) {
    shared.set(P_CLICK_ON, 0.0);
  }
  if (state.analysisApply[4] && reverb > 0.4f) {
    shared.set(P_VERB_ON, 1.0);
    shared.set(P_VERB_TAIL,
               std::clamp(static_cast<double>(reverb), 0.3, 2.5));
  } else if (state.analysisApply[4]) {
    shared.set(P_VERB_ON, 0.0);
  }
}

void Panel::drawSpectralPage(cairo_t* cr, const Shared& shared,
                             const PanelState& state) const {
  const bool bridgeAvailable =
      shared.spectralBridge.load(std::memory_order_relaxed) != nullptr;
  // Title
  text(cr, state.diffMode ? "SPECTRAL DIFF" : "SPECTRAL REPAIR", kSpecX, 172,
       18, 0, kText.r, kText.g, kText.b);
  if (state.captureArmed) {
    const char* msg = "● ARMED — press play in the host, then FREEZE";
    if (state.captureSeconds > 0.0f) {
      msg = "● CAPTURING — press FREEZE when the event has passed";
    } else if (!state.transportValid) {
      msg = "● ARMED — host transport position is not available";
    }
    text(cr, msg, kSpecX + kSpecW, 172, 10, 2,
         state.transportValid ? kGreen.r : kAmber.r,
         state.transportValid ? kGreen.g : kAmber.g,
         state.transportValid ? kGreen.b : kAmber.b);
  }

  // Plot frame
  roundRect(cr, kSpecX, kSpecY, kSpecW, kSpecH, 6);
  setRgb(cr, Rgb{0.02, 0.03, 0.04});
  cairo_fill(cr);
  if (state.specValid && state.specW > 0 && state.specH > 0) {
    const std::vector<uint32_t>& shown =
        (state.diffMode && diffValid_) ? diffPixels_ : state.specPixels;
    cairo_surface_t* image = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(
            const_cast<uint32_t*>(shown.data())),
        CAIRO_FORMAT_ARGB32, state.specW, state.specH, state.specW * 4);
    const double zoom = std::max(1.0, state.specZoom);
    const double srcW = static_cast<double>(state.specW) / zoom;
    const double srcX =
        state.specScroll * (static_cast<double>(state.specW) - srcW);
    cairo_save(cr);
    cairo_rectangle(cr, kSpecX, kSpecY, kSpecW, kSpecH);
    cairo_clip(cr);
    cairo_translate(cr, kSpecX, kSpecY);
    cairo_scale(cr, kSpecW / std::max(1.0, srcW), kSpecH / state.specH);
    cairo_translate(cr, -srcX, 0.0);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(image);
  } else if (!bridgeAvailable) {
    text(cr, "Spectral capture needs host instance-access",
         kSpecX + kSpecW * 0.5, kSpecY + kSpecH * 0.5, 14, 1, kAmber.r,
         kAmber.g, kAmber.b);
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

  // Selection.  The mask is drawn as merged cell runs so lasso/brush shapes
  // stay cheap even at full resolution.
  if (state.hasSelection && state.specTool == 0) {
    const double x0 = std::clamp(std::min(state.selX0, state.selX1), kSpecX,
                                 kSpecX + kSpecW);
    const double x1 = std::clamp(std::max(state.selX0, state.selX1), kSpecX,
                                 kSpecX + kSpecW);
    const double y0 = std::clamp(std::min(state.selY0, state.selY1), kSpecY,
                                 kSpecY + kSpecH);
    const double y1 = std::clamp(std::max(state.selY0, state.selY1), kSpecY,
                                 kSpecY + kSpecH);
    setRgb(cr, kAccent, 0.18);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_fill(cr);
    setRgb(cr, kAccent, 0.85);
    cairo_set_line_width(cr, 1.3);
    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
    cairo_stroke(cr);
  } else if (state.hasSelection && state.specMask.size() ==
                               static_cast<size_t>(state.specW) * state.specH) {
    setRgb(cr, kAccent, 0.22);
    for (int row = 0; row < state.specH; ++row) {
      int c = 0;
      while (c < state.specW) {
        while (c < state.specW &&
               !state.specMask[static_cast<size_t>(row * state.specW + c)]) {
          ++c;
        }
        if (c >= state.specW) {
          break;
        }
        const int start = c;
        while (c < state.specW &&
               state.specMask[static_cast<size_t>(row * state.specW + c)]) {
          ++c;
        }
        const double total =
            static_cast<double>(captureL_.size()) /
            (state.sampleRate > 0.0f ? state.sampleRate : 48000.0f);
        const double t0 = state.specStartTime +
                          static_cast<double>(start) / state.specW * total;
        const double t1 = state.specStartTime +
                          static_cast<double>(c) / state.specW * total;
        const double x0 = std::clamp(timeToX(state, t0), kSpecX,
                                     kSpecX + kSpecW);
        const double x1 = std::clamp(timeToX(state, t1), kSpecX,
                                     kSpecX + kSpecW);
        const double y0 = kSpecY + static_cast<double>(row) / state.specH * kSpecH;
        const double y1 = kSpecY + static_cast<double>(row + 1) / state.specH * kSpecH;
        cairo_rectangle(cr, x0, y0 - 0.5, std::max(0.5, x1 - x0), y1 - y0 + 0.5);
        cairo_fill(cr);
      }
    }
  } else if (state.selecting) {
    if (state.specTool == 1 && lassoPoints_.size() > 1) {
      setRgb(cr, kAccent, 0.9);
      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, lassoPoints_.front().first,
                    lassoPoints_.front().second);
      for (const auto& p : lassoPoints_) {
        cairo_line_to(cr, p.first, p.second);
      }
      cairo_stroke(cr);
    } else if (state.specTool == 0) {
      const double x0 = std::clamp(std::min(state.selX0, state.selX1), kSpecX,
                                   kSpecX + kSpecW);
      const double x1 = std::clamp(std::max(state.selX0, state.selX1), kSpecX,
                                   kSpecX + kSpecW);
      const double y0 = std::clamp(std::min(state.selY0, state.selY1), kSpecY,
                                   kSpecY + kSpecH);
      const double y1 = std::clamp(std::max(state.selY0, state.selY1), kSpecY,
                                   kSpecY + kSpecH);
      setRgb(cr, kAccent, 0.18);
      cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
      cairo_fill(cr);
      setRgb(cr, kAccent, 0.9);
      cairo_set_line_width(cr, 1.4);
      cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
      cairo_stroke(cr);
    }
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
  if (state.timelineStale) {
    text(cr, "timeline moved — re-capture edits", kSpecX + kSpecW,
         kSpecY + kSpecH + 24, 11, 2, kAmber.r, kAmber.g, kAmber.b);
  } else {
    text(cr, "drag a box on the spectrogram", kSpecX + kSpecW,
         kSpecY + kSpecH + 24, 11, 2, kDim.r, kDim.g, kDim.b);
  }
  if (state.historyOpen) {
    drawSpectralHistory(cr, shared, state);
  }
}

void Panel::drawSpectralHistory(cairo_t* cr, const Shared& shared,
                                const PanelState& state) const {
  (void)shared;
  (void)state;
  const double x = 790.0;
  const double y = 386.0;
  const double w = 354.0;
  const double h = 252.0;
  roundRect(cr, x, y, w, h, 6);
  setRgb(cr, Rgb{0.027, 0.038, 0.044}, 0.985);
  cairo_fill(cr);
  setRgb(cr, kAccent, 0.75);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 6);
  cairo_stroke(cr);
  text(cr, "SPECTRAL HISTORY", x + 14, y + 22, 11, 0, kText.r, kText.g,
       kText.b);
  text(cr, "click an entry to restore", x + w - 14, y + 22, 9, 2, kDim.r,
       kDim.g, kDim.b);
  const int rows = std::min(9, static_cast<int>(undoNames_.size()));
  for (int r = 0; r < rows; ++r) {
    const int idx = static_cast<int>(undoNames_.size()) - 1 - r;
    const double ry = y + 34 + r * 23.0;
    if (r == 0) {
      roundRect(cr, x + 6, ry - 1, w - 12, 21, 3);
      setRgb(cr, kAccent, 0.15);
      cairo_fill(cr);
    }
    text(cr, undoNames_[static_cast<size_t>(idx)].c_str(), x + 16, ry + 13,
         10, 0, kText.r, kText.g, kText.b);
  }
  if (undoNames_.empty()) {
    text(cr, "no edits yet", x + 18, y + 56, 10, 0, kDim.r, kDim.g, kDim.b);
  }
}

void Panel::restoreHistory(Shared& shared, PanelState& state, int index) {
  if (index < 0 || index >= static_cast<int>(undoStack_.size())) {
    return;
  }
  auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed);
  if (!bridge) {
    return;
  }
  std::vector<SpectralEdit> current;
  bridge->copyEdits(&current);
  redoStack_.push_back(std::move(current));
  redoNames_.push_back("Current");
  bridge->replaceEdits(undoStack_[static_cast<size_t>(index)]);
  undoStack_.resize(static_cast<size_t>(index));
  if (undoNames_.size() > static_cast<size_t>(index)) {
    undoNames_.resize(static_cast<size_t>(index));
  }
  state.editCount = bridge->editCount();
  state.hasSelection = false;
  state.historyOpen = false;
  diffValid_ = false;
  if (state.previewMode) {
    setPreview(shared, state);
  }
}

void Panel::selectHarmonics(Shared& shared, PanelState& state) {
  if (state.specW <= 0 || state.specH <= 0 || captureL_.empty()) {
    return;
  }
  const float sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double total = static_cast<double>(captureL_.size()) / sr;
  const double t0 = plotTime(state, std::min(state.selX0, state.selX1));
  const double t1 = plotTime(state, std::max(state.selX0, state.selX1));
  const int c0 = std::clamp(static_cast<int>(
                                (t0 - state.specStartTime) /
                                std::max(1.0e-9, total) * state.specW),
                            0, state.specW - 1);
  const int c1 = std::clamp(static_cast<int>(
                                (t1 - state.specStartTime) /
                                std::max(1.0e-9, total) * state.specW),
                            c0, state.specW - 1);
  if (c1 <= c0) {
    return;
  }
  float fundamental = shared.humDetectedHz.load(std::memory_order_relaxed);
  if (fundamental <= 0.0f) {
    fundamental = static_cast<float>(shared.get(P_HUM_FREQ));
  }
  fundamental = std::clamp(fundamental, 20.0f, 2000.0f);
  const int harmonics = std::clamp(
      static_cast<int>(shared.get(P_HUM_HARMONICS) + 0.5), 1, 32);
  if (state.specMask.size() !=
      static_cast<size_t>(state.specW) * state.specH) {
    state.specMask.assign(static_cast<size_t>(state.specW) * state.specH, 0);
  } else {
    std::fill(state.specMask.begin(), state.specMask.end(), 0);
  }
  for (int row = 0; row < state.specH; ++row) {
    const double t = static_cast<double>(row) / (state.specH - 1);
    const double hz = 20000.0 * std::pow(0.001, t);
    bool on = false;
    for (int k = 1; k <= harmonics; ++k) {
      const double f = fundamental * k;
      const double width = std::max(6.0, f * 0.012);
      if (std::fabs(hz - f) <= width) {
        on = true;
        break;
      }
    }
    if (!on) {
      continue;
    }
    for (int c = c0; c <= c1; ++c) {
      state.specMask[static_cast<size_t>(row * state.specW + c)] = 1;
    }
  }
  state.hasSelection = true;
  updateSelectionBoundsFromMask(state);
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
  roundRect(cr, 20, 62, kPanelWidth - 40, kPanelHeight - 82, 12);
  setRgb(cr, kPanel);
  cairo_fill(cr);
  cairo_set_line_width(cr, 1.0);
  setRgb(cr, kEdge, 0.8);
  roundRect(cr, 20.5, 62.5, kPanelWidth - 41, kPanelHeight - 83, 12);
  cairo_stroke(cr);

  drawHeader(cr, shared, state);
  drawMonitorStrip(cr, shared, state);
  if (state.tab == 5) {
    drawSpectralPage(cr, shared, state);
  } else {
    drawStageVisual(cr, shared, state, state.tab);
    if (state.tab == 6) {
      drawAnalysis(cr, shared, state);
    }
  }

  for (const Widget& w : widgets_) {
    drawWidget(cr, w, shared, state);
  }
  if (hoverParam_ >= 0 && hoverParam_ < static_cast<int>(P_COUNT)) {
    const ParamSpec& spec = kParamSpecs[hoverParam_];
    char tip[160];
    std::snprintf(tip, sizeof(tip), "%s  ·  %s", spec.name,
                  formatValue(spec, shared.get(static_cast<uint32_t>(hoverParam_))).c_str());
    const double tw = 220.0;
    const double tx = std::clamp(hoverX_ + 14.0, 4.0, kPanelWidth - tw - 4.0);
    const double ty = std::clamp(hoverY_ - 30.0, 64.0, kPanelHeight - 64.0);
    roundRect(cr, tx, ty, tw, 24, 4);
    setRgb(cr, Rgb{0.02, 0.035, 0.04}, 0.96);
    cairo_fill(cr);
    setRgb(cr, kAccent, 0.75);
    cairo_set_line_width(cr, 1.0);
    roundRect(cr, tx + 0.5, ty + 0.5, tw - 1, 23, 4);
    cairo_stroke(cr);
    text(cr, tip, tx + 8, ty + 16, 10, 0, kText.r, kText.g, kText.b);
  }

  // Footer / status
  const double f = kPanelHeight - 46;
  setRgb(cr, kEdge, 0.55);
  cairo_rectangle(cr, 24, f, kPanelWidth - 48, 1);
  cairo_fill(cr);
  const uint32_t latencySamples =
      shared.latencySamples.load(std::memory_order_relaxed);
  const double sr = state.sampleRate > 0.0f ? state.sampleRate : 48000.0f;
  const double latencyMs = latencySamples > 0
                               ? static_cast<double>(latencySamples) / sr * 1000.0
                               : 0.0;
  const int stftFrame = 512 << std::clamp(
      static_cast<int>(shared.get(P_NOISE_FRAME) + 0.5), 0, 3);
  char status[160];
  std::snprintf(status, sizeof(status),
                "STFT %d / 75%%  ·  latency %.1f ms  ·  capture %.1f s  ·  "
                "edits %d",
                stftFrame, latencyMs, state.captureSeconds, state.editCount);
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
  if (state.presetOpen) {
    drawPresetList(cr, shared, state);
  }
  cairo_restore(cr);
}


void Panel::drawPresetBar(cairo_t* cr, const Shared& shared,
                          const PanelState& state) const {
  (void)shared;
  roundRect(cr, kPresetX, kPresetY, kPresetW, kPresetH, 5);
  setRgb(cr, Rgb{0.04, 0.055, 0.062});
  cairo_fill(cr);
  setRgb(cr, kAccent, 0.55);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, kPresetX + 0.5, kPresetY + 0.5, kPresetW - 1, kPresetH - 1, 5);
  cairo_stroke(cr);
  text(cr, "‹", kPresetX + 18, kPresetY + 25, 20, 1, kDim.r, kDim.g, kDim.b);
  text(cr, "›", kPresetX + kPresetW - 18, kPresetY + 25, 20, 1, kDim.r, kDim.g,
       kDim.b);
  const auto& presets = factoryPresets();
  std::string label = "Init";
  if (state.presetIndex >= 0 &&
      state.presetIndex < static_cast<int>(presets.size())) {
    label = presets[static_cast<size_t>(state.presetIndex)].name;
  }
  if (state.presetDirty) {
    label += " *";
  }
  text(cr, label.c_str(), kPresetX + kPresetW * 0.5, kPresetY + 24, 14, 1,
       kText.r, kText.g, kText.b);
}

void Panel::drawPresetList(cairo_t* cr, const Shared& shared,
                           const PanelState& state) const {
  (void)shared;
  const auto& presets = factoryPresets();
  const auto& order = presetOrder();
  const double h = kListVisible * kListRow + 10.0;
  roundRect(cr, kPresetX, kListY, kPresetW, h, 5);
  setRgb(cr, Rgb{0.035, 0.045, 0.052}, 0.98);
  cairo_fill(cr);
  setRgb(cr, kAccent, 0.7);
  cairo_set_line_width(cr, 1.0);
  roundRect(cr, kPresetX + 0.5, kListY + 0.5, kPresetW - 1, h - 1, 5);
  cairo_stroke(cr);

  cairo_save(cr);
  roundRect(cr, kPresetX + 1, kListY + 1, kPresetW - 2, h - 2, 4);
  cairo_clip(cr);
  double y = kListY + 6 - state.presetScroll * kListRow;
  const char* lastCategory = nullptr;
  for (int i : order) {
    const auto& p = presets[static_cast<size_t>(i)];
    if (!lastCategory || std::strcmp(lastCategory, p.category) != 0) {
      lastCategory = p.category;
      if (y + kListRow > kListY && y < kListY + h) {
        text(cr, p.category, kPresetX + 14, y + 14, 10, 0, kAccent.r,
             kAccent.g, kAccent.b);
      }
      y += kListRow;
    }
    if (y + kListRow > kListY && y < kListY + h) {
      if (i == state.presetIndex) {
        roundRect(cr, kPresetX + 6, y + 1, kPresetW - 12, kListRow - 2, 3);
        setRgb(cr, kAccent, 0.22);
        cairo_fill(cr);
      }
      text(cr, p.name, kPresetX + 26, y + 15, 11.5, 0, kText.r, kText.g,
           kText.b);
    }
    y += kListRow;
  }
  cairo_restore(cr);

  const int totalRows =
      static_cast<int>(order.size()) + presetCategoryCount();
  if (totalRows > static_cast<int>(kListVisible)) {
    const double frac = kListVisible / totalRows;
    const double barH = std::max(24.0, h * frac);
    const double maxScroll =
        std::max(1.0, static_cast<double>(totalRows - kListVisible));
    const double barY =
        kListY + 4 + (h - 8 - barH) * state.presetScroll / maxScroll;
    setRgb(cr, kAccent, 0.35);
    cairo_rectangle(cr, kPresetX + kPresetW - 7, barY, 3, barH);
    cairo_fill(cr);
  }
}

void Panel::drawHeader(cairo_t* cr, const Shared& shared,
                       const PanelState& state) const {
  cairo_pattern_t* bar = cairo_pattern_create_linear(0, 0, 0, 62);
  cairo_pattern_add_color_stop_rgb(bar, 0.0, kPanel.r * 1.35,
                                   kPanel.g * 1.35, kPanel.b * 1.35);
  cairo_pattern_add_color_stop_rgb(bar, 1.0, kPanel.r * 0.72,
                                   kPanel.g * 0.72, kPanel.b * 0.72);
  cairo_rectangle(cr, 0, 0, kPanelWidth, 62);
  cairo_set_source(cr, bar);
  cairo_fill(cr);
  cairo_pattern_destroy(bar);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 0.5);
  cairo_line_to(cr, kPanelWidth, 0.5);
  cairo_stroke(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
  cairo_move_to(cr, 0, 61.5);
  cairo_line_to(cr, kPanelWidth, 61.5);
  cairo_stroke(cr);
  text(cr, "REPAIR SUITE", kPanelWidth * 0.5, 38, 19, 1, 0.80, 0.82, 0.84);
  drawPresetBar(cr, shared, state);
}

void Panel::drawWidget(cairo_t* cr, const Widget& w, const Shared& shared,
                       const PanelState& state) const {
  switch (w.kind) {
    case Kind::Power: {
      const bool on = shared.get(P_POWER) > 0.5;
      cairo_arc(cr, w.x, w.y, w.r, 0, 2 * kPiD);
      setRgb(cr, Rgb{0.12, 0.12, 0.12});
      cairo_fill(cr);
      cairo_arc(cr, w.x, w.y, w.r - 1.5, 0, 2 * kPiD);
      setRgb(cr, on ? kGreen : Rgb{0.42, 0.42, 0.42});
      cairo_set_line_width(cr, 2.2);
      cairo_new_path(cr);
      cairo_arc(cr, w.x, w.y, 7, -kPiD / 2 + 0.6,
                -kPiD / 2 + 2 * kPiD - 0.6);
      cairo_stroke(cr);
      cairo_move_to(cr, w.x, w.y - 9);
      cairo_line_to(cr, w.x, w.y - 1);
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
      const bool active =
          (w.action == 1 &&
           shared.captureActive.load(std::memory_order_relaxed)) ||
          (w.action >= 80 && w.action <= 86 &&
           std::fabs(shared.get(P_AUDITION) - (w.action - 79)) < 0.5);
      roundRect(cr, w.x, w.y, w.w, w.h, 5);
      setRgb(cr, active ? kAccent : kPanelLight);
      cairo_fill(cr);
      setRgb(cr, kEdge, 0.8);
      cairo_set_line_width(cr, 1.0);
      roundRect(cr, w.x + 0.5, w.y + 0.5, w.w - 1, w.h - 1, 5);
      cairo_stroke(cr);
      const char* label = w.label;
      if (w.action == 1 && active) {
        label = "ARMED";
      }
      text(cr, label, w.x + w.w * 0.5, w.y + w.h * 0.5 + 4, 11, 1,
           active ? 0.02 : kText.r, active ? 0.05 : kText.g,
           active ? 0.05 : kText.b);
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
  const ParamSpec& spec = kParamSpecs[editParam_];
  roundRect(cr, editBoxX_, editBoxY_, PanelState::kEditW, PanelState::kEditH,
            5);
  setRgb(cr, Rgb{0.02, 0.03, 0.035}, 0.99);
  cairo_fill(cr);
  setRgb(cr, kAccent, 0.9);
  cairo_set_line_width(cr, 1.2);
  roundRect(cr, editBoxX_ + 0.5, editBoxY_ + 0.5, PanelState::kEditW - 1,
            PanelState::kEditH - 1, 5);
  cairo_stroke(cr);
  text(cr, spec.name, editBoxX_ + 8, editBoxY_ + 13, 8.5, 0, kDim.r, kDim.g,
       kDim.b);
  const std::string shown =
      (state.editText.empty() ? "type a value" : state.editText) + "|";
  text(cr, shown.c_str(), editBoxX_ + 8, editBoxY_ + 31, 12, 0, kGold.r,
       kGold.g, kGold.b);
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


bool Panel::pointerDown(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  if (handlePresetClick(shared, state, x, y)) {
    return true;
  }
  int mon = -1;
  if (monitorHit(x, y, &mon)) {
    if (mon == 3) {
      const bool ab = shared.compare.load(std::memory_order_relaxed);
      shared.compare.store(!ab, std::memory_order_relaxed);
    } else {
      shared.set(P_MONITOR, static_cast<double>(mon));
      shared.set(P_DELTA, mon == 2 ? 1.0 : 0.0);
      state.presetDirty = true;
    }
    return true;
  }
  if (state.tab == 5 && state.historyOpen) {
    const double hx = 790.0;
    const double hy = 386.0;
    if (x >= hx && x <= hx + 354.0 && y >= hy && y <= hy + 252.0) {
      const int row = static_cast<int>((y - (hy + 34.0)) / 23.0);
      if (row >= 0 && row < static_cast<int>(undoNames_.size()) && row < 9) {
        const int idx = static_cast<int>(undoNames_.size()) - 1 - row;
        restoreHistory(shared, state, idx);
      }
      return true;
    }
    state.historyOpen = false;
  }
  if (state.tab == 0) {
    int which = 0;
    if (clipHistogramHit(shared, x, y, &which)) {
      clipDrag_ = which;
      return true;
    }
  }
  if (state.tab == 1 && x >= 580 && x <= 1080 && y >= 462 && y <= 574) {
    shared.set(P_AUDITION, 2.0);
    shared.set(P_MONITOR, 0.0);
    shared.set(P_DELTA, 0.0);
    state.presetDirty = true;
    return true;
  }
  if (state.tab == 6 && x >= 132 && x <= 172 &&
      y >= kAnalysisRowY && y < kAnalysisRowY + 5 * kAnalysisRowH) {
    const int row = static_cast<int>((y - kAnalysisRowY) / kAnalysisRowH);
    if (row >= 0 && row < 5) {
      state.analysisApply[row] = !state.analysisApply[row];
      return true;
    }
  }
  if (state.tab == 2) {
    int harmonic = -1;
    if (humTableHit(state, x, y, &harmonic) && harmonic >= 0) {
      uint64_t mask = shared.humMask.load(std::memory_order_relaxed);
      mask ^= (1ull << harmonic);
      shared.humMask.store(mask, std::memory_order_relaxed);
      return true;
    }
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
      case Kind::Knob: {
        const double now = nowMs();
        const bool doubleClick =
            lastClickParam_ == static_cast<int>(w.param) &&
            now - lastClickTime_ <= PanelState::kDoubleClickMs;
        lastClickParam_ = static_cast<int>(w.param);
        lastClickTime_ = now;
        if (doubleClick) {
          shared.set(w.param, presetResetValue(state, w.param));
          state.presetDirty = true;
          dragParam_ = -1;
          return true;
        }
        dragParam_ = static_cast<int>(w.param);
        dragStartY_ = y;
        dragStartValue_ = shared.get(w.param);
        (void)fine;
        return false;
      }
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
            state.previewMode = false;
            if (auto* bridge =
                    shared.spectralBridge.load(std::memory_order_relaxed)) {
              bridge->clearPreview();
            }
          }
        } else if (w.action == 2) {
          const bool learn =
              shared.noiseLearnActive.load(std::memory_order_relaxed);
          shared.noiseLearnActive.store(!learn, std::memory_order_relaxed);
        } else if (w.action == 3) {
          shared.captureFreeze.store(true, std::memory_order_relaxed);
        } else if (w.action == 4) {
          pushUndo(shared, "Clear edits");
          shared.clearEdits.store(true, std::memory_order_relaxed);
          if (auto* bridge = shared.spectralBridge.load(std::memory_order_relaxed)) {
            bridge->clearEdits();
            state.editCount = bridge->editCount();
          }
          state.hasSelection = false;
          diffValid_ = false;
          if (state.previewMode) {
            setPreview(shared, state);
          }
        } else if (w.action == 5) {
          applySpectralEdit(shared, state);
        } else if (w.action >= 10 && w.action <= 18) {
          if (w.action <= 12) {
            state.specTool = w.action - 10;
            state.specMask.assign(static_cast<size_t>(state.specW) * state.specH,
                                  0);
            state.hasSelection = false;
            lassoPoints_.clear();
          } else if (w.action == 13) {
            undo(shared, state);
          } else if (w.action == 14) {
            redo(shared, state);
          } else if (w.action == 15 || w.action == 16) {
            const double oldZoom = state.specZoom;
            state.specZoom = std::clamp(
                state.specZoom * (w.action == 16 ? 1.35 : 0.74), 1.0, 12.0);
            if (state.specZoom <= 1.001) {
              state.specScroll = 0.0;
            }
            (void)oldZoom;
          } else if (w.action == 17) {
            state.diffMode = !state.diffMode;
            if (state.diffMode) {
              rebuildDiff(shared, state);
            }
          } else if (w.action == 18) {
            state.previewMode = !state.previewMode;
            setPreview(shared, state);
          }
        } else if (w.action == 19) {
          matchEdits(shared, state);
        } else if (w.action == 20) {
          shared.analyzeRequest.store(true, std::memory_order_relaxed);
        } else if (w.action == 21) {
          applyAnalysis(shared, state);
        } else if (w.action == 22) {
          state.specTool = 3;
          state.specMask.assign(static_cast<size_t>(state.specW) * state.specH,
                                0);
          state.hasSelection = false;
          lassoPoints_.clear();
        } else if (w.action == 23) {
          state.historyOpen = !state.historyOpen;
        } else if (w.action == 24) {
          static const double kGammas[] = {0.8, 1.0, 1.25, 1.6, 2.0};
          int at = 2;
          for (int i = 0; i < 5; ++i) {
            if (std::fabs(state.specGamma - kGammas[i]) < 1.0e-6) {
              at = i;
            }
          }
          state.specGamma = kGammas[(at + 1) % 5];
          buildSpectrogram(state);
        } else if (w.action == 25) {
          static const float kRanges[] = {36.0f, 48.0f, 60.0f, 72.0f, 84.0f};
          int at = 2;
          for (int i = 0; i < 5; ++i) {
            if (std::fabs(state.specRangeDb - kRanges[i]) < 1.0e-6) {
              at = i;
            }
          }
          state.specRangeDb = kRanges[(at + 1) % 5];
          buildSpectrogram(state);
        } else if (w.action == 60) {
          resetModule(shared, state, state.tab);
        } else if (w.action == 70) {
          applyModulePreset(shared, state, state.tab);
        } else if (w.action >= 80 && w.action <= 86) {
          const double page = static_cast<double>(w.action - 79);
          const double cur = shared.get(P_AUDITION);
          shared.set(P_AUDITION, std::fabs(cur - page) < 0.5 ? 0.0 : page);
          if (shared.get(P_AUDITION) > 0.5) {
            shared.set(P_MONITOR, 0.0);
            shared.set(P_DELTA, 0.0);
          }
          state.presetDirty = true;
        }
        return true;
    }
  }
  if (state.tab == 5 && plotHit(x, y)) {
    selectionStartX_ = x;
    selectionStartY_ = y;
    state.specMask.assign(static_cast<size_t>(state.specW) * state.specH, 0);
    state.hasSelection = false;
    if (state.specTool == 2) {
      paintBrush(state, x, y);
      selectionDrag_ = true;
      return true;
    }
    if (state.specTool == 1) {
      lassoPoints_.clear();
      lassoPoints_.emplace_back(x, y);
      selectionDrag_ = true;
      state.selecting = true;
      state.hasSelection = false;
      return true;
    }
    selectionDrag_ = true;
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
  for (const Widget& w : widgets_) {
    if (w.kind == Kind::Knob && hit(w, x, y)) {
      beginTextEntry(shared, state, x, y);
      return true;
    }
  }
  if (state.tab == 5 && plotHit(x, y) && state.specZoom > 1.001) {
    plotPan_ = true;
    panStartX_ = x;
    panStartScroll_ = state.specScroll;
    return true;
  }
  return false;
}

void Panel::beginTextEntry(Shared& shared, PanelState& state, double x,
                           double y) {
  (void)shared;
  state.presetOpen = 0;
  state.historyOpen = false;
  for (const Widget& w : widgets_) {
    if (w.kind != Kind::Knob || !hit(w, x, y)) {
      continue;
    }
    editing_ = true;
    editParam_ = w.param;
    state.editParam = static_cast<int>(w.param);
    state.editText.clear();
    editBoxX_ = std::clamp(x + 16.0, 4.0, kPanelWidth - PanelState::kEditW - 4.0);
    editBoxY_ = std::clamp(y - PanelState::kEditH - 14.0, 4.0,
                           kPanelHeight - PanelState::kEditH - 4.0);
    return;
  }
}

bool Panel::textKey(Shared& shared, PanelState& state, int key,
                    const char* text) {
  if (!editing_) {
    return false;
  }
  if (key == 13) {
    return commitTextEntry(shared, state);
  }
  if (key == 27) {
    editing_ = false;
    state.editText.clear();
    return true;
  }
  if (key == 8) {
    if (!state.editText.empty()) {
      state.editText.pop_back();
    }
    return true;
  }
  if (text) {
    for (const char* c = text; *c; ++c) {
      if (*c >= 32 && *c < 127 && state.editText.size() < 24) {
        state.editText.push_back(*c);
      }
    }
  }
  return true;
}

bool Panel::commitTextEntry(Shared& shared, PanelState& state) {
  if (!editing_) {
    return false;
  }
  editing_ = false;
  if (state.editText.empty()) {
    return false;
  }
  double value = 0.0;
  if (!parseParamValue(kParamSpecs[editParam_], state.editText, &value)) {
    return false;
  }
  shared.set(editParam_, value);
  state.presetDirty = true;
  state.editText.clear();
  return true;
}

bool Panel::keyEvent(Shared& shared, PanelState& state, int key,
                     const char* text) {
  if (textKey(shared, state, key, text)) {
    return true;
  }
  if (!text || !text[0]) {
    return false;
  }
  switch (text[0]) {
    case 'd':
    case 'D': {
      const bool on = shared.get(P_MONITOR) > 1.5;
      shared.set(P_MONITOR, on ? 0.0 : 2.0);
      shared.set(P_DELTA, on ? 0.0 : 1.0);
      return true;
    }
    case 'o':
    case 'O':
      shared.set(P_MONITOR, 0.0);
      shared.set(P_DELTA, 0.0);
      return true;
    case 'i':
    case 'I':
      shared.set(P_MONITOR, 1.0);
      return true;
    case 'z':
    case 'Z':
      shared.undoParameter();
      state.presetDirty = true;
      return true;
    case 'y':
    case 'Y':
      shared.redoParameter();
      state.presetDirty = true;
      return true;
    case 'b':
    case 'B': {
      const bool ab = shared.compare.load(std::memory_order_relaxed);
      shared.compare.store(!ab, std::memory_order_relaxed);
      return true;
    }
    case '[':
      state.tab = std::max(0, state.tab - 1);
      return true;
    case ']':
      state.tab = std::min(6, state.tab + 1);
      return true;
    default:
      return false;
  }
}

bool Panel::pointerMove(Shared& shared, PanelState& state, double x, double y,
                        bool fine) {
  hoverX_ = x;
  hoverY_ = y;
  if (clipDrag_) {
    const double t = std::clamp((610.0 - y) / (190.0 - 48.0), 0.0, 1.0);
    const double db = -60.0 + t * 60.0;
    shared.set(clipDrag_ == 1 ? P_CLIP_UPPER : P_CLIP_LOWER, db);
    state.presetDirty = true;
    return true;
  }
  if (plotPan_) {
    const double total =
        static_cast<double>(captureL_.size()) /
        (state.sampleRate > 0.0f ? state.sampleRate : 48000.0f);
    if (total > 0.0 && state.specZoom > 1.001) {
      const double delta = (x - panStartX_) / kSpecW / state.specZoom;
      state.specScroll =
          std::clamp(panStartScroll_ - delta, 0.0, 1.0);
    }
    return true;
  }
  if (selectionDrag_) {
    const double cx = std::clamp(x, kSpecX, kSpecX + kSpecW);
    const double cy = std::clamp(y, kSpecY, kSpecY + kSpecH);
    if (state.specTool == 2) {
      paintBrush(state, cx, cy);
      return true;
    }
    if (state.specTool == 1) {
      if (!lassoPoints_.empty() &&
          std::hypot(cx - lassoPoints_.back().first,
                     cy - lassoPoints_.back().second) > 2.0) {
        lassoPoints_.emplace_back(cx, cy);
      }
      state.selX1 = cx;
      state.selY1 = cy;
      state.selecting = true;
      return true;
    }
    state.selX1 = cx;
    state.selY1 = cy;
    state.selecting = true;
    return true;
  }
  if (dragParam_ < 0) {
    hoverParam_ = -1;
    for (const Widget& w : widgets_) {
      if (w.kind == Kind::Knob && hit(w, x, y)) {
        hoverParam_ = static_cast<int>(w.param);
        break;
      }
    }
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
  plotPan_ = false;
  clipDrag_ = 0;
  if (selectionDrag_) {
    selectionDrag_ = false;
    state.selecting = false;
    if (state.specTool == 0) {
      if (std::fabs(state.selX1 - state.selX0) > 4.0 &&
          std::fabs(state.selY1 - state.selY0) > 4.0) {
        fillSelectionMask(state);
      }
    } else if (state.specTool == 1) {
      selectLasso(state);
    } else if (state.specTool == 3) {
      selectHarmonics(shared, state);
    }
    bool any = false;
    for (uint8_t v : state.specMask) {
      any = any || v != 0;
      if (any) {
        break;
      }
    }
    state.hasSelection = any;
    if (state.previewMode) {
      setPreview(shared, state);
    }
  }
}

bool Panel::wheel(Shared& shared, PanelState& state, double x, double y,
                  double dy) {
  if (state.presetOpen && x >= kPresetX && x <= kPresetX + kPresetW &&
      y >= kListY && y <= kListY + kListVisible * kListRow + 10) {
    const int totalRows =
        static_cast<int>(presetOrder().size()) + presetCategoryCount();
    const int maxScroll = std::max(0, totalRows - static_cast<int>(kListVisible));
    state.presetScroll = std::clamp(
        state.presetScroll - (dy > 0 ? 1 : -1), 0, maxScroll);
    return true;
  }
  if (state.tab == 5 && plotHit(x, y)) {
    const double total =
        static_cast<double>(captureL_.size()) /
        (state.sampleRate > 0.0f ? state.sampleRate : 48000.0f);
    if (total <= 0.0) {
      return true;
    }
    const double anchor = plotTime(state, x);
    const double oldZoom = state.specZoom;
    state.specZoom = std::clamp(
        state.specZoom * (dy > 0 ? 1.25 : 0.8), 1.0, 12.0);
    if (state.specZoom <= 1.001) {
      state.specScroll = 0.0;
    } else if (state.specZoom != oldZoom) {
      const double view = total / state.specZoom;
      const double start =
          anchor - (x - kSpecX) / kSpecW * view;
      state.specScroll = std::clamp(
          (start - state.specStartTime) / std::max(1.0e-9, total - view),
          0.0, 1.0);
    }
    return true;
  }
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

void Panel::resetModule(Shared& shared, PanelState& state, int page) {
  static const char* kModules[7] = {"De-Clip", "De-Click", "De-Hum",
                                    "De-Noise", "De-Reverb", "Spectral",
                                    "Global"};
  for (uint32_t p = 0; p < P_COUNT; ++p) {
    const char* module = kParamSpecs[p].module;
    bool match = false;
    if (page >= 0 && page < 6) {
      match = std::strcmp(module, kModules[page]) == 0;
    } else {
      match = std::strcmp(module, "Global") == 0 ||
              std::strcmp(module, "Output") == 0;
    }
    if (match && p != P_POWER && p != P_MONITOR && p != P_AUDITION) {
      shared.set(p, kParamSpecs[p].def);
    }
  }
  if (page >= 0 && page < 7) {
    state.modulePreset[page] = 0;
  }
  state.presetDirty = true;
}

void Panel::applyModulePreset(Shared& shared, PanelState& state, int page) {
  if (page < 0 || page > 6) {
    return;
  }
  const auto& list = modulePresets()[static_cast<size_t>(page)];
  if (list.empty()) {
    return;
  }
  state.modulePreset[page] =
      (state.modulePreset[page] + 1) % static_cast<int>(list.size());
  const auto& preset = list[static_cast<size_t>(state.modulePreset[page])];
  for (const ModulePresetValue& v : preset.values) {
    shared.set(v.param, v.value);
  }
  state.presetDirty = true;
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
  state.presetOpen = 0;
}

bool Panel::handlePresetClick(Shared& shared, PanelState& state, double x,
                              double y) {
  const int count = static_cast<int>(factoryPresets().size());
  if (y >= kPresetY && y <= kPresetY + kPresetH && x >= kPresetX &&
      x <= kPresetX + kPresetW) {
    if (x <= kPresetX + 30) {
      const int next = state.presetIndex <= 0 ? 0 : state.presetIndex - 1;
      applyPreset(shared, state, next);
    } else if (x >= kPresetX + kPresetW - 30) {
      const int next = state.presetIndex < 0 ? 0 : state.presetIndex + 1;
      applyPreset(shared, state, std::min(count - 1, next));
    } else {
      state.presetOpen = state.presetOpen ? 0 : 1;
    }
    return true;
  }
  if (!state.presetOpen) {
    return false;
  }
  const auto& order = presetOrder();
  const double h = kListVisible * kListRow + 10.0;
  if (x < kPresetX || x > kPresetX + kPresetW || y < kListY ||
      y > kListY + h) {
    state.presetOpen = 0;
    return true;
  }
  double rowY = kListY + 6 - state.presetScroll * kListRow;
  const char* lastCategory = nullptr;
  for (int i : order) {
    const auto& p = factoryPresets()[static_cast<size_t>(i)];
    if (!lastCategory || std::strcmp(lastCategory, p.category) != 0) {
      lastCategory = p.category;
      rowY += kListRow;
    }
    if (y >= rowY && y < rowY + kListRow) {
      applyPreset(shared, state, i);
      return true;
    }
    rowY += kListRow;
  }
  return true;
}

} // namespace repair
