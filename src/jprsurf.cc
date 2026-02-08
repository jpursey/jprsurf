// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <windows.h>

#include <fstream>
#include <memory>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/time/time.h"

namespace jpr {
namespace {

absl::TimeZone GetLocalTimeZone() {
  // Get Windows timezone info
  TIME_ZONE_INFORMATION tzi;
  DWORD result = GetTimeZoneInformation(&tzi);

  if (result == TIME_ZONE_ID_INVALID) {
    return absl::UTCTimeZone();  // Fallback to UTC
  }

  // Convert wide string to narrow string
  char tz_name[128];
  WideCharToMultiByte(CP_UTF8, 0, tzi.StandardName, -1, tz_name,
                      sizeof(tz_name), nullptr, nullptr);

  // Try to load the timezone by name, fallback to UTC if it fails
  absl::TimeZone tz;
  if (absl::LoadTimeZone(tz_name, &tz)) {
    return tz;
  }

  // If loading by name fails, try using fixed offset
  int bias_minutes =
      tzi.Bias +
      (result == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightBias : tzi.StandardBias);
  return absl::FixedTimeZone(-bias_minutes *
                             60);  // Negative because Windows bias is opposite
}

class FileSink : public absl::LogSink {
 public:
  explicit FileSink(const std::string& filename)
      : file_(filename, std::ios::out | std::ios::trunc),
        time_zone_(GetLocalTimeZone()) {}

  void Send(const absl::LogEntry& entry) override {
    if (file_.is_open()) {
      // Format timestamp in local time
      std::string timestamp =
          absl::FormatTime("%m%d %H:%M:%E6S", entry.timestamp(), time_zone_);

      // Output: timestamp severity file:line message
      file_ << absl::LogSeverityName(entry.log_severity())[0] << timestamp
            << " " << entry.tid() << " (" << entry.source_basename() << ":"
            << entry.source_line() << ") " << entry.text_message() << "\n"
            << std::flush;
    }
  }

 private:
  std::ofstream file_;
  absl::TimeZone time_zone_;
};

std::unique_ptr<FileSink> g_file_sink;

void Initialize() {
  // Initialize Abseil logging
  absl::InitializeLog();

  // Get user's APPDATA folder
  char* appdata = nullptr;
  size_t len = 0;
  if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata != nullptr) {
    std::string log_path = std::string(appdata) + "\\jprsurf.log";
    free(appdata);

    // Create and register file sink
    g_file_sink = std::make_unique<FileSink>(log_path);
    absl::AddLogSink(g_file_sink.get());
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);

    LOG(INFO) << "reaper_jprsurf extension loaded. Logging to: " << log_path;
  } else {
    LOG(ERROR) << "Failed to get APPDATA directory for logging.";
  }
}

void Shutdown() {
  if (g_file_sink != nullptr) {
    absl::RemoveLogSink(g_file_sink.get());
    g_file_sink = nullptr;
  }
}

}  // namespace
}  // namespace jpr

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
      jpr::Initialize();
      break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
      break;
    case DLL_PROCESS_DETACH:
      jpr::Shutdown();
      break;
  }
  return TRUE;
}
