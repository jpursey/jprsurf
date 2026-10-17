// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/test_install.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/base/init_logging.h"
#include "gb/base/log_file.h"
#include "gb/file/local_file_protocol.h"
#include "jpr/common/file_system.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/project_file.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// File: Quit REAPER.
constexpr int kQuitAction = 40004;

// REAPER's Main_openProject(), GetMainHwnd(), and CSurf_FlushUndo(). The
// extension never calls them, so they aren't on the API list (see
// reaper_api.h), and the fake must never fake them. LoadTestInstall() loads
// them by name.
using MainOpenProject = void (*)(const char* name);
using GetMainHwnd = HWND (*)();
using CSurfFlushUndo = void (*)(bool force);
MainOpenProject g_main_open_project = nullptr;
GetMainHwnd g_get_main_hwnd = nullptr;
CSurfFlushUndo g_csurf_flush_undo = nullptr;

// The run directory, from JPR_REAPER_RUN_DIR, or empty if it isn't set.
std::filesystem::path g_dir;

// Writes what is logged to log.txt in g_dir.
std::unique_ptr<gb::LogFile> g_log_file;

// Returns the directory JPR_REAPER_RUN_DIR names, or empty if it isn't set.
std::filesystem::path ReadRunDir() {
  wchar_t* value = nullptr;
  size_t length = 0;
  if (_wdupenv_s(&value, &length, L"JPR_REAPER_RUN_DIR") != 0 ||
      value == nullptr) {
    return {};
  }
  std::filesystem::path dir(value);
  free(value);
  return dir;
}

}  // namespace

bool LoadTestInstall(reaper_plugin_info_t* plugin_info,
                     reaper_csurf_reg_t* registration) {
  if (plugin_info == nullptr) {
    return false;
  }
  g_dir = ReadRunDir();
  if (!g_dir.empty()) {
    gb::InitLogging();
    g_log_file = std::make_unique<gb::LogFile>(g_dir / "log.txt",
                                               absl::LogSeverityAtLeast::kInfo);
    GetFileSystem()->Register(gb::LocalFileProtocol::Create(g_dir), "run");
  }
  if (plugin_info->caller_version != REAPER_PLUGIN_VERSION ||
      plugin_info->GetFunc == nullptr || !LoadReaperApi(plugin_info->GetFunc)) {
    LOG(ERROR) << "Failed to load REAPER's API";
    return false;
  }
  g_main_open_project = reinterpret_cast<MainOpenProject>(
      plugin_info->GetFunc("Main_openProject"));
  g_get_main_hwnd =
      reinterpret_cast<GetMainHwnd>(plugin_info->GetFunc("GetMainHwnd"));
  g_csurf_flush_undo =
      reinterpret_cast<CSurfFlushUndo>(plugin_info->GetFunc("CSurf_FlushUndo"));
  if (g_main_open_project == nullptr || g_get_main_hwnd == nullptr ||
      g_csurf_flush_undo == nullptr) {
    LOG(ERROR) << "Failed to load the functions that aren't on the API list";
    return false;
  }
  plugin_info->Register("csurf", registration);
  LOG(INFO) << "Loaded, and registered the surface type";
  return true;
}

const std::filesystem::path& GetRunDir() { return g_dir; }

bool WriteRunFile(std::string_view name, std::string_view text) {
  return GetFileSystem()->WriteFile(absl::StrCat("run:/", name), text);
}

bool OpenProjectFile(const FakeProject& project) {
  if (!WriteRunFile("project.rpp", WriteProjectFile(project))) {
    LOG(ERROR) << "Failed to write project.rpp in " << g_dir.string();
    return false;
  }
  const std::string name =
      absl::StrCat("noprompt:", (g_dir / "project.rpp").string());

  // REAPER holds open the undo point of a volume or pan change made with
  // CSurf_On*ChangeEx(), and crashes after a run if the project it is in was
  // closed first.
  g_csurf_flush_undo(true);
  g_main_open_project(name.c_str());
  return true;
}

void FinishRun(bool succeeded) {
  if (!WriteRunFile("result.txt", succeeded ? "0\n" : "1\n")) {
    LOG(ERROR) << "Failed to write result.txt in " << g_dir.string();
  }
  OpenProjectFile(*FakeProject::Create());

  // Posted, so REAPER quits once the run has returned.
  LOG(INFO) << "Quitting";
  PostMessage(g_get_main_hwnd(), WM_COMMAND, kQuitAction, 0);
}

}  // namespace jpr
