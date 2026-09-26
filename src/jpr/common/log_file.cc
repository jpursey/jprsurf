// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/log_file.h"

#include <windows.h>

#include <fstream>
#include <string>

#include "absl/base/call_once.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink_registry.h"
#include "absl/time/time.h"

namespace jpr {
namespace {

absl::once_flag g_initialize_log_once;

absl::TimeZone GetLocalTimeZone() {
  TIME_ZONE_INFORMATION tzi;
  DWORD result = GetTimeZoneInformation(&tzi);
  if (result == TIME_ZONE_ID_INVALID) {
    return absl::UTCTimeZone();
  }

  char tz_name[128];
  WideCharToMultiByte(CP_UTF8, 0, tzi.StandardName, -1, tz_name,
                      sizeof(tz_name), nullptr, nullptr);
  absl::TimeZone tz;
  if (absl::LoadTimeZone(tz_name, &tz)) {
    return tz;
  }

  // Windows biases are minutes to add to local time to get UTC, the opposite
  // of an offset from UTC.
  int bias_minutes =
      tzi.Bias +
      (result == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightBias : tzi.StandardBias);
  return absl::FixedTimeZone(-bias_minutes * 60);
}

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

}  // namespace jpr
