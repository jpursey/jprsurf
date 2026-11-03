// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/markdown.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace jpr {

std::string GetGeneratedPageHeader(std::string_view source) {
  return absl::StrCat("<!-- Generated from ", source,
                      " by the update_user_docs target. Don't edit. -->\n");
}

std::string GetCellText(std::string_view text) {
  return absl::StrReplaceAll(text, {{"|", "\\|"}, {"\r\n", " "}, {"\n", " "}});
}

}  // namespace jpr
