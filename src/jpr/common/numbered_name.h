// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace jpr {

namespace internal {

// Holds the characters for kNumberedName (see below) in static storage.
template <const std::string_view& Prefix, int Value>
struct NumberedNameStorage {
  static_assert(Value >= 0, "kNumberedName values must be non-negative");

  static constexpr size_t kDigits = [] {
    size_t digits = 1;
    for (int value = Value; value >= 10; value /= 10) {
      ++digits;
    }
    return digits;
  }();

  static constexpr std::array<char, Prefix.size() + kDigits> kChars = [] {
    std::array<char, Prefix.size() + kDigits> chars = {};
    for (size_t i = 0; i < Prefix.size(); ++i) {
      chars[i] = Prefix[i];
    }
    int value = Value;
    for (size_t i = chars.size(); i > Prefix.size(); --i) {
      chars[i - 1] = static_cast<char>('0' + value % 10);
      value /= 10;
    }
    return chars;
  }();
};

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
inline constexpr std::string_view kNumberedName{
    internal::NumberedNameStorage<Prefix, Value>::kChars.data(),
    internal::NumberedNameStorage<Prefix, Value>::kChars.size()};

}  // namespace jpr
