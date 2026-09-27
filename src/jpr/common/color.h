// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace jpr {

// This represents a 24-bit RGB color value.
struct Color {
  bool operator==(const Color&) const = default;

  uint8_t r;
  uint8_t g;
  uint8_t b;
};

double GetLuminance(Color color);

// Returns the color as text in the form "#rrggbb", with lowercase hex digits.
std::string FormatColor(Color color);

// Parses a color in the form "#rrggbb" (in either case), or returns
// std::nullopt if the text is not in that form.
std::optional<Color> ParseColor(std::string_view text);

}  // namespace jpr
