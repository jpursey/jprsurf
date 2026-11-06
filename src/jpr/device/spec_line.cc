// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/spec_line.h"

#include "absl/strings/str_cat.h"

namespace jpr {

std::string GetSpecError(SpecLine line, std::string_view subject,
                         std::string_view message) {
  std::string error;
  if (line.number > 0) {
    absl::StrAppend(&error, "line ", line.number, ": ");
  }
  if (!subject.empty()) {
    absl::StrAppend(&error, subject, ": ");
  }
  absl::StrAppend(&error, message);
  return error;
}

}  // namespace jpr
