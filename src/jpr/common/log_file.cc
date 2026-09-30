// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/log_file.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "absl/base/call_once.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink_registry.h"
#include "absl/time/time.h"
#include "jpr/common/local_time.h"

namespace jpr {
namespace {

absl::once_flag g_initialize_log_once;

class FileSink final : public absl::LogSink {
 public:
  explicit FileSink(const std::filesystem::path& path)
      : file_(path, std::ios::out | std::ios::trunc),
        time_zone_(GetLocalTimeZone()) {}

  void Send(const absl::LogEntry& entry) override {
    if (!file_.is_open()) {
      return;
    }
    std::string timestamp =
        absl::FormatTime("%m%d %H:%M:%E6S", entry.timestamp(), time_zone_);
    file_ << absl::LogSeverityName(entry.log_severity())[0] << timestamp << " "
          << entry.tid() << " (" << entry.source_basename() << ":"
          << entry.source_line() << ") " << entry.text_message() << "\n"
          << std::flush;
  }

 private:
  std::ofstream file_;
  absl::TimeZone time_zone_;
};

}  // namespace

LogFile::LogFile(const std::filesystem::path& path,
                 absl::LogSeverityAtLeast min_level)
    : sink_(std::make_unique<FileSink>(path)) {
  absl::call_once(g_initialize_log_once, absl::InitializeLog);
  absl::AddLogSink(sink_.get());
  absl::SetMinLogLevel(min_level);
}

LogFile::~LogFile() { absl::RemoveLogSink(sink_.get()); }

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
