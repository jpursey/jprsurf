// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>

namespace jpr {

// This represents a 24-bit RGB color value.
struct Color {
  bool operator==(const Color&) const = default;

  uint8_t r;
  uint8_t g;
  uint8_t b;
};

double GetLuminance(Color color);

}  // namespace jpr