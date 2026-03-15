// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cmath>
#include <limits>

namespace jpr {

// Converts a REAPER volume value (linear amplitude) to decibels.
// A volume of 0.0 (silence) returns negative infinity.
inline double VolumeToDecibels(double volume) {
  if (volume <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }
  return 20.0 * std::log10(volume);
}

// Converts a decibel value to a REAPER volume value (linear amplitude).
inline double DecibelsToVolume(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace jpr
