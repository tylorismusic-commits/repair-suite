// Repair Suite — spectral bridge
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor talks to the audio thread through this small interface.  CLAP
// can hand the editor a direct pointer; the LV2 UI can obtain the same pointer
// through instance-access when the host provides it.
#pragma once

#include <vector>

namespace repair {

struct SpectralEdit {
  double startTime = 0.0;
  double endTime = 0.0;
  std::vector<float> left;
  std::vector<float> right;
};

class SpectralBridge {
public:
  virtual ~SpectralBridge() = default;

  // Freeze the rolling capture.  The audio thread copies it into a stable
  // snapshot; frozenReady() becomes true when copyFrozen() is safe.
  virtual void requestFreeze() = 0;
  virtual void requestThaw() = 0;
  virtual bool frozenReady() const = 0;
  virtual bool copyFrozen(std::vector<float>* left,
                          std::vector<float>* right,
                          double* startTime) = 0;

  // Add a replacement edit keyed to absolute host time.
  virtual bool addEdit(double startTime, double endTime, const float* left,
                       const float* right, int frames) = 0;
  virtual void clearEdits() = 0;
  virtual int editCount() const = 0;
  virtual void copyEdits(std::vector<SpectralEdit>* out) const = 0;
  virtual void replaceEdits(const std::vector<SpectralEdit>& edits) = 0;

  // Selection audition.  The engine loops this buffer while preview is on.
  virtual void setPreview(const float* left, const float* right,
                          int frames) = 0;
  virtual void clearPreview() = 0;
  virtual bool previewActive() const = 0;
  virtual float capturedSeconds() const = 0;
};

} // namespace repair
