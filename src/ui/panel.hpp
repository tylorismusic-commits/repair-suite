// Repair Suite — panel
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cairo.h>

#include <string>
#include <vector>

#include "params.hpp"
#include "dsp/spectral_bridge.hpp"
#include "user_presets.hpp"

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
  bool savePreset = false;
  bool renamePreset = false;
  bool deletePresetConfirm = false;
  std::string presetText;
  bool presetTextFresh = false;
  std::string presetError;
  bool presetOverwriteConfirm = false;
  std::string presetNotice;
  double presetNoticeUntil = 0.0;
  std::vector<double> presetSnapshot;
  int presetScanTicks = 60;
  float inLevelL = -120.0f;
  float inLevelR = -120.0f;
  float outLevelL = -120.0f;
  float outLevelR = -120.0f;
  float grDb = 0.0f;
  float noiseGrDb = 0.0f;
  float verbGrDb = 0.0f;
  float captureSeconds = 0.0f;
  bool captureArmed = false;
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
  std::vector<uint8_t> specMask;
  int specTool = 0;  // 0 rect, 1 lasso, 2 brush
  double specZoom = 1.0;
  double specScroll = 0.0;
  bool diffMode = false;
  bool previewMode = false;
  bool timelineStale = false;
  double specGamma = 1.25;
  float specRangeDb = 60.0f;
  bool historyOpen = false;
  std::vector<std::string> historyNames;
  int clipHist[64] = {};
  float clipRestoredPeakDb = 0.0f;
  bool analysisApply[6] = {true, true, true, true, true, true};
  int modulePreset[7] = {0, 0, 0, 0, 0, 0, 0};
  static constexpr int kHistory = 240;
  static constexpr int kDoubleClickMs = 400;
  static constexpr double kEditW = 170.0;
  static constexpr double kEditH = 40.0;
  float inHistory[kHistory] = {};
  int inHistoryHead = 0;
  float outHistory[kHistory] = {};
  int outHistoryHead = 0;
  float clipHistory[kHistory] = {};
  float clickHistory[kHistory] = {};
  float noiseHistory[kHistory] = {};
  float verbHistory[kHistory] = {};
  int stageHead = 0;
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
  int presetCount() const;
  const char* presetName(int index) const;
  const char* presetCategory(int index) const;
  std::vector<int> presetOrder() const;
  int presetRow(int index) const;
  int presetTotalRows() const;
  double presetResetValue(const PanelState& state, uint32_t param) const;
  void reloadUserPresets();
  void rescanUserPresets(PanelState& state);
  void setPresetNotice(PanelState& state, const std::string& text) const;
  bool commitPresetName(Shared& shared, PanelState& state);
  void duplicatePreset(Shared& shared, PanelState& state);
  void exportPreset(const Shared& shared, PanelState& state) const;
  bool importPresetFile(Shared& shared, PanelState& state,
                        const std::string& path);
  bool handlePresetClick(Shared& shared, PanelState& state, double x, double y);
  void drawPresetBar(cairo_t* cr, const Shared& shared,
                     const PanelState& state) const;
  void drawPresetList(cairo_t* cr, const Shared& shared,
                      const PanelState& state) const;

  bool importDroppedFile(Shared& shared, PanelState& state,
                         const std::string& path);

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
  void drawAnalysis(cairo_t* cr, const Shared& shared,
                    const PanelState& state) const;
  void drawMonitorStrip(cairo_t* cr, const Shared& shared,
                        const PanelState& state) const;
  bool monitorHit(double x, double y, int* index) const;
  bool clipHistogramHit(const Shared& shared, double x, double y,
                        int* which) const;
  bool humTableHit(const PanelState& state, double x, double y, int* harmonic) const;
  void drawSpectralHistory(cairo_t* cr, const Shared& shared,
                           const PanelState& state) const;
  void restoreHistory(Shared& shared, PanelState& state, int index);
  void applyModulePreset(Shared& shared, PanelState& state, int page);
  void resetModule(Shared& shared, PanelState& state, int page);
  void beginTextEntry(Shared& shared, PanelState& state, double x, double y);
  bool textKey(Shared& shared, PanelState& state, int key, const char* text);
  bool commitTextEntry(Shared& shared, PanelState& state);
  void drawStageVisual(cairo_t* cr, const Shared& shared,
                       const PanelState& state, int page) const;
  void applyAnalysis(Shared& shared, const PanelState& state);
  void loadCapture(Shared& shared, PanelState& state);
  void buildSpectrogram(PanelState& state);
  void buildMagnitude(const std::vector<float>& left,
                      const std::vector<float>& right,
                      std::vector<float>* mag, float sampleRate) const;
  bool applySpectralEdit(Shared& shared, PanelState& state);
  bool plotHit(double x, double y) const;
  void selectionTimes(const PanelState& state, double* t0, double* t1,
                      double* f0, double* f1) const;
  void pushUndo(Shared& shared, const char* name = "Edit");
  void undo(Shared& shared, PanelState& state);
  void redo(Shared& shared, PanelState& state);
  void setPreview(Shared& shared, PanelState& state);
  void matchEdits(Shared& shared, PanelState& state);
  void rebuildDiff(Shared& shared, PanelState& state);
  double plotTime(const PanelState& state, double x) const;
  double plotHz(double y) const;
  double timeToX(const PanelState& state, double t) const;
  double hzToY(double hz) const;
  void fillSelectionMask(PanelState& state);
  void paintBrush(PanelState& state, double x, double y);
  void selectLasso(PanelState& state);
  void selectHarmonics(Shared& shared, PanelState& state);
  void updateSelectionBoundsFromMask(PanelState& state);
  void text(cairo_t* cr, const char* s, double x, double y, double size,
            int align, double r, double g, double b) const;

  bool hit(const Widget& w, double x, double y) const;
  void addKnob(uint32_t param, double x, double y, double r);
  void addToggle(uint32_t param, double x, double y, double w, double h,
                 const char* label);
  void addChoice(uint32_t param, double x, double y, double w, double h,
                 const char* label);
  void addButton(double x, double y, double w, double h, const char* label,
                 int action);

  std::vector<Widget> widgets_;
  std::vector<UserPreset> userPresets_;
  int dragParam_ = -1;
  double dragStartY_ = 0.0;
  double dragStartValue_ = 0.0;
  bool editing_ = false;
  uint32_t editParam_ = 0;
  double editBoxX_ = 0.0;
  double editBoxY_ = 0.0;
  int lastClickParam_ = -1;
  double lastClickTime_ = 0.0;
  int clipDrag_ = 0;  // 1 upper threshold, 2 lower threshold
  int hoverParam_ = -1;
  double hoverX_ = 0.0;
  double hoverY_ = 0.0;
  std::vector<float> captureL_;
  std::vector<float> captureR_;
  double captureStart_ = 0.0;
  bool captureLoaded_ = false;
  bool selectionDrag_ = false;
  double selectionStartX_ = 0.0;
  double selectionStartY_ = 0.0;
  bool plotPan_ = false;
  double panStartX_ = 0.0;
  double panStartScroll_ = 0.0;
  std::vector<std::pair<double, double>> lassoPoints_;
  std::vector<std::vector<SpectralEdit>> undoStack_;
  std::vector<std::vector<SpectralEdit>> redoStack_;
  std::vector<std::string> undoNames_;
  std::vector<std::string> redoNames_;
  std::vector<float> editedL_;
  std::vector<float> editedR_;
  std::vector<uint32_t> diffPixels_;
  std::vector<float> lastEditL_;
  std::vector<float> lastEditR_;
  std::vector<float> specMag_;
  bool diffValid_ = false;
  int diffEditCount_ = -1;
  double lastTransport_ = 0.0;
  bool transportSeen_ = false;

  cairo_font_face_t* font_ = nullptr;
  void* ft_ = nullptr;
  void* ftFace_ = nullptr;
};

} // namespace repair
