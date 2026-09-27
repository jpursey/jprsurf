// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/prefixed_name.h"

namespace jpr {

namespace internal {

// Returns the decimal digits of Value as a StringLiteral.
template <int Value>
constexpr auto DecimalLiteral() {
  static_assert(Value >= 0, "kNumberedName values must be non-negative");
  constexpr int kDigits = [] {
    int digits = 1;
    for (int value = Value; value >= 10; value /= 10) {
      ++digits;
    }
    return digits;
  }();
  StringLiteral<kDigits + 1> literal;
  int value = Value;
  for (int i = kDigits - 1; i >= 0; --i) {
    literal.chars[i] = static_cast<char>('0' + value % 10);
    value /= 10;
  }
  return literal;
}

}  // namespace internal

//==============================================================================
// kNumberedName
//==============================================================================

// A compile time name for a number: a prefix followed by a non-negative integer
// value in decimal. For example, kNumberedName<kPrefix, 42> is "name:42" if
// kPrefix is "name:". The prefix must be a constexpr std::string_view with
// static storage duration (such as an inline constexpr variable), as the view
// is passed by reference.
//
// The view refers to static storage, and is not null terminated.
template <const std::string_view& Prefix, int Value>
inline constexpr std::string_view kNumberedName =
    kPrefixedName<Prefix, internal::DecimalLiteral<Value>()>;

}  // namespace jpr
