// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace jpr {

//==============================================================================
// StringLiteral
//==============================================================================

// Holds a string literal, so it can be a template argument (see kPrefixedName
// below).
template <int N>
struct StringLiteral {
  // An empty string of N - 1 null characters, to be filled in by a constexpr
  // function.
  constexpr StringLiteral() = default;

  // Implicit, so a literal can be passed as a template argument.
  constexpr StringLiteral(const char (&literal)[N]) {  // NOLINT
    std::copy_n(literal, N, chars);
  }

  // The literal, without its null terminator.
  constexpr std::string_view View() const {
    return std::string_view(chars, N - 1);
  }

  // Public, as a template argument must be a structural type. This includes the
  // null terminator.
  char chars[N] = {};
};

namespace internal {

// Holds the characters for kPrefixedName (see below) in static storage.
template <const std::string_view& Prefix, StringLiteral Name>
struct PrefixedNameStorage {
  static constexpr std::array<char, Prefix.size() + Name.View().size()> kChars =
      [] {
        std::array<char, Prefix.size() + Name.View().size()> chars = {};
        std::ranges::copy(Name.View(),
                          std::ranges::copy(Prefix, chars.begin()).out);
        return chars;
      }();
};

}  // namespace internal

//==============================================================================
// kPrefixedName
//==============================================================================

// A compile time name made of a prefix followed by a string literal. For
// example, kPrefixedName<kPrefix, "mute"> is "name:mute" if kPrefix is "name:".
// The prefix must be a constexpr std::string_view with static storage duration
// (such as an inline constexpr variable), as the view is passed by reference.
//
// The view refers to static storage, and is not null terminated.
template <const std::string_view& Prefix, StringLiteral Name>
inline constexpr std::string_view kPrefixedName{
    internal::PrefixedNameStorage<Prefix, Name>::kChars.data(),
    internal::PrefixedNameStorage<Prefix, Name>::kChars.size()};

}  // namespace jpr
