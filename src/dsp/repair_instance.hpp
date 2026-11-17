// Repair Suite — LV2 public instance prefix
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "spectral_bridge.hpp"

namespace repair {

// Both the LV2 plugin and its UI include this header.  The plugin keeps this
// as the first member of its instance struct, so a UI that receives the
// instance-access feature can safely read the bridge pointer without knowing
// the rest of the plugin internals.
struct PublicInstance {
  SpectralBridge* bridge = nullptr;
};

} // namespace repair
