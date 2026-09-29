// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "absl/base/log_severity.h"
#include "absl/log/log_sink.h"

namespace jpr {

// Writes Abseil log messages to a file for as long as it exists.
//
// Each line starts with the severity, the local time, the thread ID, and the
// source file and line. The file is cleared when the LogFile is created.
class LogFile final {
 public:
  // Starts writing log messages at or above `min_level` to the file at `path`.
  //
  // The minimum level applies to all of Abseil logging, not just this file.
  // The first LogFile created also initializes Abseil logging, so nothing else
  // may call absl::InitializeLog().
  LogFile(const std::filesystem::path& path,
          absl::LogSeverityAtLeast min_level);
  LogFile(const LogFile&) = delete;
  LogFile& operator=(const LogFile&) = delete;
  ~LogFile();

 private:
  std::unique_ptr<absl::LogSink> sink_;
};

// Returns the path of `file_name` in the folder JPRSurf writes its log to, for
// the log and any other file JPRSurf writes beside it. The folder is the user's
// AppData folder. Returns an empty path if the folder isn't known.
std::filesystem::path GetLogPath(std::string_view file_name);

}  // namespace jpr
