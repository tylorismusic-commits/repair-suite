// Repair Suite — DSP controls
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace repair {

// Physical units, matching the parameter table.  The engine consumes these
// directly; the UI and host parameter text share the same units.
struct Controls {
  bool power = true;
  bool delta = false;
  float inputDb = 0.0f;
  float outputDb = 0.0f;
  bool dcOn = true;
  float dcHz = 20.0f;

  bool clipOn = true;
  int clipMode = 0;        // 0 High, 1 Balanced, 2 Fast
  bool clipAuto = true;
  float clipUpperDb = 0.0f;
  float clipLowerDb = 0.0f;
  int clipIterations = 12;
  float clipHeadroomDb = 0.0f;
  float clipMix = 100.0f;

  bool clickOn = true;
  int clickMode = 0;       // 0 Click, 1 Crackle, 2 Thump, 3 Dropout
  float clickSensitivity = 5.0f;
  float clickWidthMs = 5.0f;
  bool clickLink = true;
  float clickMix = 100.0f;

  bool humOn = true;
  bool humAuto = true;
  float humHz = 50.0f;
  int humHarmonics = 12;
  bool humOdd = false;
  float humDepthDb = 30.0f;
  bool humPrecision = false;
  float humMix = 100.0f;

  bool noiseOn = true;
  bool noiseAdaptive = false;
  float noiseReductionDb = 12.0f;
  float noiseTonalDb = 12.0f;
  float noiseSmoothing = 5.0f;
  float noiseTransient = 5.0f;
  int noiseFrame = 2;      // 0 512, 1 1024, 2 2048, 3 4096
  float noiseMix = 100.0f;

  bool verbOn = false;
  float verbAmountDb = 6.0f;
  float verbTailSec = 0.8f;
  float verbPredelayMs = 30.0f;
  float verbTransient = 6.0f;
  float verbTiltDb = 0.0f;
  float verbMix = 100.0f;

  bool specOn = true;
  int specOp = 2;          // 0 Attenuate, 1 Erase, 2 Interpolate, 3 Patch, 4 Repair, 5 Amplify
  float specAmountDb = 24.0f;
  bool specPreserve = true;
  float specMix = 100.0f;

  // One-shot UI actions, not host parameters.
  bool captureArm = false;
  bool analyze = false;
  bool captureFreeze = false;
  bool clearCapture = false;
  bool clearEdits = false;
  bool noiseLearn = false;
  bool compare = false;
};

struct Meters {
  float inPeakDb = -120.0f;
  float outPeakDb = -120.0f;
  float inPeakLDb = -120.0f;
  float inPeakRDb = -120.0f;
  float outPeakLDb = -120.0f;
  float outPeakRDb = -120.0f;
  float grDb = 0.0f;
  float noiseGrDb = 0.0f;
  float verbGrDb = 0.0f;
  float specActive = 0.0f;
  float noiseFloorDb = -120.0f;
  float captureSeconds = 0.0f;
  float captureComplete = 0.0f;
  double transportSeconds = 0.0;
  bool transportValid = false;
  int editCount = 0;
  bool analysisReady = false;
  float analysisProgress = 0.0f;
  float analysisHumHz = 0.0f;
  float analysisHumDb = -120.0f;
  float analysisNoiseDb = -120.0f;
  float analysisClipPct = 0.0f;
  float analysisClickRate = 0.0f;
  float analysisReverbSec = 0.0f;
  float clipActivity = 0.0f;
  int clickEvents = 0;
  float humDetectedHz = 0.0f;
};

} // namespace repair
