// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/color.h"

#include <cmath>

namespace jpr {

double GetLuminance(Color color) {
  // Calculate the relative luminance of the color using the formula from
  // https://en.wikipedia.org/wiki/Relative_luminance. This is a value in the
  // range [0.0, 1.0], where 0.0 is black and 1.0 is white.
  auto linearize = [](uint8_t channel) {
    double c = channel / 255.0;
    return (c <= 0.03928) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
  };
  double r = linearize(color.r);
  double g = linearize(color.g);
  double b = linearize(color.b);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

}  // namespace jpr