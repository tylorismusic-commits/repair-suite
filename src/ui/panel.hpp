// Repair Suite — panel
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "params.hpp"

namespace repair {

constexpr int kPanelWidth = 1200;
constexpr int kPanelHeight = 760;

// UI-only state.  Host-visible values live in Shared.
struct PanelState {
  int tab = 0;             // 0 CLIP .. 6 OUT
  int presetIndex = -1;
  bool presetDirty = false;
  int presetOpen = 0;
  int presetScroll = 0;
  float inLevelL = -120.0f;
  float inLevelR = -120.0f;
  float outLevelL = -120.0f;
  float outLevelR = -120.0f;
  float grDb = 0.0f;
  float noiseGrDb = 0.0f;
  float verbGrDb = 0.0f;
  float captureSeconds = 0.0f;
  double transportSeconds = 0.0;
  bool transportValid = false;
  int editCount = 0;
  int editParam = -1;
  std::string editText;

  // Spectral page cache and selection.
  std::vector<uint32_t> specPixels;
  int specW = 0;
  int specH = 0;
  bool specValid = false;
  double specStartTime = 0.0;
  float sampleRate = 48000.0f;
  bool selecting = false;
  bool hasSelection = false;
  double selX0 = 0.0;
  double selY0 = 0.0;
  double selX1 = 0.0;
  double selY1 = 0.0;
  static constexpr int kHistory = 240;
  float inHistory[kHistory] = {};
  int inHistoryHead = 0;
  float outHistory[kHistory] = {};
  int outHistoryHead = 0;
};

class Panel {
public:
  Panel();
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // Loads Jost from the given directory, falling back to a system font.
  void loadFont(const std::string& dir);

  void tick(Shared& shared, PanelState& state);
  void draw(cairo_t* cr, Shared& shared, const PanelState& state);

  bool pointerDown(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  bool contextDown(Shared& shared, PanelState& state, double x, double y);
  bool keyEvent(Shared& shared, PanelState& state, int key, const char* text);
  bool pointerMove(Shared& shared, PanelState& state, double x, double y,
                   bool fine);
  void pointerUp(Shared& shared, PanelState& state);
  bool wheel(Shared& shared, PanelState& state, double x, double y, double dy);
  bool dragging() const { return dragParam_ >= 0; }

  void applyPreset(Shared& shared, PanelState& state, int index) const;
  bool handlePresetClick(Shared& shared, PanelState& state, double x, double y);

private:
  enum class Kind { Knob, Toggle, Choice, PageTab, Power, Button };

  struct Widget {
    Kind kind = Kind::Knob;
    uint32_t param = 0;
    double value = 0.0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double r = 0.0;
    const char* label = "";
    int action = 0;  // 1 capture arm, 2 noise learn, 3 freeze, 4 clear edits
  };

  void layout(const Shared& shared, const PanelState& state);
  void drawHeader(cairo_t* cr, const Shared& shared,
                  const PanelState& state) const;
  void drawWidget(cairo_t* cr, const Widget& w, const Shared& shared,
                  const PanelState& state) const;
  void drawKnob(cairo_t* cr, const Widget& w, double value,
                const ParamSpec& spec) const;
  void drawToggle(cairo_t* cr, const Widget& w, bool on) const;
  void drawChoice(cairo_t* cr, const Widget& w, int selected) const;
  void drawPageTab(cairo_t* cr, const Widget& w, bool active,
                   bool moduleOn) const;
  void drawTextEntry(cairo_t* cr, const Shared& shared,
                     const PanelState& state) const;
  void drawSpectralPage(cairo_t* cr, const Shared& shared,
                        const PanelState& state) const;
  void loadCapture(Shared& shared, PanelState& state);
  void buildSpectrogram(PanelState& state);
  bool applySpectralEdit(Shared& shared, PanelState& state);
  bool plotHit(double x, double y) const;
  void selectionTimes(const PanelState& state, double* t0, double* t1,
                      double* f0, double* f1) const;
  void text(cairo_t* cr, const char* s, double x, double y, double size,
            int align, double r, double g, double b) const;

  bool hit(const Widget& w, double x, double y) const;
  const Widget* find(uint32_t param, Kind kind) const;
  void addKnob(uint32_t param, double x, double y, double r);
  void addToggle(uint32_t param, double x, double y, double w, double h,
                 const char* label);
  void addChoice(uint32_t param, double x, double y, double w, double h,
                 const char* label);
  void addButton(double x, double y, double w, double h, const char* label,
                 int action);

  std::vector<Widget> widgets_;
  int dragParam_ = -1;
  double dragStartY_ = 0.0;
  double dragStartValue_ = 0.0;
  bool editing_ = false;
  uint32_t editParam_ = 0;
  double editBoxX_ = 0.0;
  double editBoxY_ = 0.0;
  double lastClickTime_ = -1.0e9;
  int lastClickParam_ = -1;
  std::vector<float> captureL_;
  std::vector<float> captureR_;
  double captureStart_ = 0.0;
  bool captureLoaded_ = false;
  bool selectionDrag_ = false;
  double selectionStartX_ = 0.0;
  double selectionStartY_ = 0.0;

  cairo_font_face_t* font_ = nullptr;
  void* ft_ = nullptr;
  void* ftFace_ = nullptr;
};

} // namespace repair
