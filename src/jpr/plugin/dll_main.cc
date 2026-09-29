// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <windows.h>

#include <filesystem>
#include <memory>

#include "jpr/common/log_file.h"

namespace jpr {
namespace {

std::unique_ptr<LogFile> g_log_file;

void Initialize() {
  const std::filesystem::path log_path = GetLogPath("jprsurf.log");
  if (!log_path.empty()) {
    g_log_file =
        std::make_unique<LogFile>(log_path, absl::LogSeverityAtLeast::kInfo);
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
      jpr::g_log_file = nullptr;
      break;
  }
  return TRUE;
}
