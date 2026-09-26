// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <windows.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "jpr/common/log_file.h"

namespace jpr {
namespace {

std::unique_ptr<LogFile> g_log_file;

void Initialize() {
  char* appdata = nullptr;
  size_t len = 0;
  if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata != nullptr) {
    std::string log_path = std::string(appdata) + "\\jprsurf.log";
    free(appdata);
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
