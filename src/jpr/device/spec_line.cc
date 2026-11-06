// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/spec_line.h"

#include "absl/strings/str_cat.h"

namespace jpr {

std::string GetSpecError(SpecPart part, std::string_view message) {
  std::string error;
  if (part.line.number > 0) {
    absl::StrAppend(&error, "line ", part.line.number, ": ");
  }
  if (!part.text.empty()) {
    absl::StrAppend(&error, part.text, ": ");
  }
  absl::StrAppend(&error, message);
  return error;
}

}  // namespace jpr
