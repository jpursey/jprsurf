// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gb/base/flags.h"

namespace jpr {

// The first line of a generated page, a comment that says what it is generated
// from, such as "the XTouch catalog", and not to edit it.
std::string GetGeneratedPageHeader(std::string_view source);

// Returns the text, safe in a table cell, on one line.
std::string GetCellText(std::string_view text);

// Returns the names as code, such as "`solid`, `blink`".
template <typename Names>
std::string GetCodeList(const Names& names) {
  return absl::StrJoin(names, ", ",
                       [](std::string* out, std::string_view name) {
                         absl::StrAppend(out, "`", name, "`");
                       });
}

// Returns the names of each of the values that is set in the flags, in the
// values' order, such as "read, write".
template <typename Type, typename Values, typename GetName>
std::string GetFlagNames(gb::Flags<Type> flags, const Values& values,
                         GetName get_name) {
  std::vector<std::string_view> names;
  for (Type value : values) {
    if (flags.IsSet(value)) {
      names.push_back(get_name(value));
    }
  }
  return absl::StrJoin(names, ", ");
}

}  // namespace jpr
