// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <string_view>

namespace jpr {

// Returns the path of `file_name` in the folder JPRSurf writes its log to, for
// the log and any other file JPRSurf writes beside it. The folder is the user's
// AppData folder. Returns an empty path if the folder isn't known.
std::filesystem::path GetLogPath(std::string_view file_name);

}  // namespace jpr
