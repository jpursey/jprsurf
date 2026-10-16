// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// The DLL REAPER loads: its entry points, and everything the plugin reads from
// its environment, which no test runs.

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "gb/base/init_logging.h"
#include "gb/base/log_file.h"
#include "gb/file/local_file_protocol.h"
#include "jpr/common/file_system.h"
#include "jpr/common/log_path.h"
#include "jpr/plugin/plugin.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

std::unique_ptr<gb::LogFile> g_log_file;

void Initialize() {
  gb::InitLogging();
  const std::filesystem::path log_path = GetLogPath("jprsurf.log");
  if (!log_path.empty()) {
    g_log_file = std::make_unique<gb::LogFile>(log_path,
                                               absl::LogSeverityAtLeast::kInfo);
  }
}

// Returns true if the JPRSURF_TRACE environment variable is set to anything
// but 0.
bool IsTraceOn() {
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "JPRSURF_TRACE") != 0 || value == nullptr) {
    return false;
  }
  const std::string_view text(value);
  const bool trace = !text.empty() && text != "0";
  free(value);
  return trace;
}

// Registers the log's folder as "profile" and "trace" in GetFileSystem(), so
// the profile and the trace are written beside the log. Returns false if it
// couldn't.
bool RegisterLogFolder() {
  const std::filesystem::path folder = GetLogFolder();
  if (folder.empty()) {
    return false;
  }
  return GetFileSystem()->Register(
      gb::LocalFileProtocol::Create(folder),
      std::vector<std::string>{"profile", "trace"});
}

// Returns the plugin's options, from its environment.
Plugin::Options GetOptions() {
  Plugin::Options options;
  const bool has_log_folder = RegisterLogFolder();
  if (IsTraceOn()) {
    if (has_log_folder) {
      options.trace_path = "trace:/jprsurf_trace.txt";
    } else {
      LOG(ERROR)
          << "JPRSURF_TRACE is set, but there is nowhere to write the trace.";
    }
  }
  if (has_log_folder) {
    options.profile_path = "profile:/jprsurf_profile.txt";
  } else {
    LOG(ERROR) << "There is nowhere to write the profile, so it is off.";
  }
  return options;
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
      jpr::g_log_file = nullptr;
      break;
  }
  return TRUE;
}

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  if (plugin_info == nullptr) {
    jpr::Plugin::Unload();
    return 0;
  }
  return jpr::Plugin::Load(instance, *plugin_info, jpr::GetOptions()) ? 1 : 0;
}

}  // extern "C"
