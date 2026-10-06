// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/log_path.h"

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace jpr {

std::filesystem::path GetLogPath(std::string_view file_name) {
  char* appdata = nullptr;
  size_t length = 0;
  if (_dupenv_s(&appdata, &length, "APPDATA") != 0 || appdata == nullptr) {
    return {};
  }
  std::filesystem::path path = std::filesystem::path(appdata) / file_name;
  free(appdata);
  return path;
}

}  // namespace jpr
