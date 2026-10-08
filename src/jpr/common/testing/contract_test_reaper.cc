// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// ContractTest in REAPER, and the entry point of reaper_jprsurf_check, the DLL
// that runs the contract tests inside the test install of REAPER (see
// "Contract tests in REAPER" in docs/testing_and_profiling.md).
//
// REAPER creates the RecordingSurface at startup, from the test install's
// reaper.ini. If the JPRSURF_CHECK_DIR environment variable names a
// directory, its first run runs every test, its second quits REAPER, and the
// directory gets:
// - output.txt: what gtest printed.
// - result.txt: 0 if every test passed, and 1 otherwise.
// - log.txt: what the DLL logged, which shows how far a run got.
// - project.rpp: the project file each test opened, written over by the next.
// Without it, the surface does nothing, so the test install can be set up by
// hand.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "absl/base/log_severity.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/base/init_logging.h"
#include "gb/base/log_file.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/project_file.h"
#include "jpr/common/testing/recording_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// File: Quit REAPER.
constexpr int kQuitAction = 40004;

// REAPER's Main_openProject(), GetMainHwnd(), and CSurf_FlushUndo(). JPRSurf
// never calls them, so they aren't on the API list (see reaper_api.h), and the
// fake must never fake them. The entry point loads them by name.
using MainOpenProject = void (*)(const char* name);
using GetMainHwnd = HWND (*)();
using CSurfFlushUndo = void (*)(bool force);
MainOpenProject g_main_open_project = nullptr;
GetMainHwnd g_get_main_hwnd = nullptr;
CSurfFlushUndo g_csurf_flush_undo = nullptr;

// Where the DLL writes its files, from JPRSURF_CHECK_DIR, or empty if it isn't
// set.
std::filesystem::path g_dir;

// Returns the directory JPRSURF_CHECK_DIR names, or empty if it isn't set.
std::filesystem::path GetCheckDir() {
  wchar_t* value = nullptr;
  size_t length = 0;
  if (_wdupenv_s(&value, &length, L"JPRSURF_CHECK_DIR") != 0 ||
      value == nullptr) {
    return {};
  }
  std::filesystem::path dir(value);
  free(value);
  return dir;
}

// Writes `text` to the file `name` in g_dir, replacing it. Returns false if it
// couldn't.
bool WriteCheckFile(std::string_view name, std::string_view text) {
  const std::filesystem::path path = g_dir / name;
  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"w") != 0 || file == nullptr) {
    return false;
  }
  const bool written =
      std::fwrite(text.data(), 1, text.size(), file) == text.size();
  return std::fclose(file) == 0 && written;
}

// Writes what is logged to log.txt in g_dir, which shows how far a run got,
// and why it failed.
std::unique_ptr<gb::LogFile> g_log_file;

// Opens `project` in REAPER, from a project file, without asking to save the
// one it replaces.
void OpenProjectFile(const FakeProject& project) {
  if (!WriteCheckFile("project.rpp", WriteProjectFile(project))) {
    // Logged too, as a failure after the tests isn't part of any.
    LOG(ERROR) << "Failed to write project.rpp in " << g_dir.string();
    ADD_FAILURE() << "Failed to write project.rpp in " << g_dir.string();
    return;
  }
  const std::string name =
      absl::StrCat("noprompt:", (g_dir / "project.rpp").string());

  // REAPER holds open the undo point of a volume or pan change made with
  // CSurf_On*ChangeEx(), and crashes after a run if the project it is in was
  // closed first.
  g_csurf_flush_undo(true);
  g_main_open_project(name.c_str());
}

// Quits REAPER once the run that calls this has returned, as quitting
// destroys the surface, which mustn't happen from inside one of its calls.
void Quit() {
  LOG(INFO) << "Quitting";
  RecordingSurface::Get()->SetOnRun(nullptr);
  PostMessage(g_get_main_hwnd(), WM_COMMAND, kQuitAction, 0);
}

void RunTests() {
  LOG(INFO) << "Running the tests";

  // gtest prints to stdout, which REAPER has nowhere to show.
  std::FILE* output = nullptr;
  const std::filesystem::path output_path = g_dir / "output.txt";
  if (_wfreopen_s(&output, output_path.c_str(), L"w", stdout) != 0) {
    output = nullptr;
  }

  int argc = 1;
  char program[] = "reaper_jprsurf_check";
  char* argv[] = {program, nullptr};
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  std::fflush(stdout);
  WriteCheckFile("result.txt", result == 0 ? "0\n" : "1\n");
  LOG(INFO) << "Ran the tests: " << (result == 0 ? "passed" : "failed");

  // Closes the last test's project, which has changes, so quitting has
  // nothing to save.
  OpenProjectFile(*FakeProject::Create());
  RecordingSurface::Get()->SetOnRun(&Quit);
}

IReaperControlSurface* CreateSurface(const char* type_string,
                                     const char* config_string,
                                     int* err_stats) {
  LOG(INFO) << "Created the surface, with config \""
            << (config_string != nullptr ? config_string : "(null)") << "\"";
  auto* surface = new RecordingSurface;
  if (!g_dir.empty()) {
    surface->SetOnRun(&RunTests);
  }
  return surface;
}

reaper_csurf_reg_t g_registration = {
    RecordingSurface::GetRegistration()->type_string,
    RecordingSurface::GetRegistration()->desc_string, &CreateSurface, nullptr};

}  // namespace

ContractTest::ContractTest() = default;

ContractTest::~ContractTest() = default;

void ContractTest::OpenProject(absl::FunctionRef<void(FakeProject&)> build) {
  std::unique_ptr<FakeProject> project = FakeProject::Create();
  build(*project);
  OpenProjectFile(*project);
  TakeCalls();
}

void ContractTest::EndEntryPoint() {}

}  // namespace jpr

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  if (plugin_info == nullptr) {
    return 0;
  }
  jpr::g_dir = jpr::GetCheckDir();
  if (!jpr::g_dir.empty()) {
    gb::InitLogging();
    jpr::g_log_file = std::make_unique<gb::LogFile>(
        jpr::g_dir / "log.txt", absl::LogSeverityAtLeast::kInfo);
  }
  if (plugin_info->caller_version != REAPER_PLUGIN_VERSION ||
      plugin_info->GetFunc == nullptr ||
      !jpr::LoadReaperApi(plugin_info->GetFunc)) {
    LOG(ERROR) << "Failed to load REAPER's API";
    return 0;
  }
  jpr::g_main_open_project = reinterpret_cast<jpr::MainOpenProject>(
      plugin_info->GetFunc("Main_openProject"));
  jpr::g_get_main_hwnd =
      reinterpret_cast<jpr::GetMainHwnd>(plugin_info->GetFunc("GetMainHwnd"));
  jpr::g_csurf_flush_undo = reinterpret_cast<jpr::CSurfFlushUndo>(
      plugin_info->GetFunc("CSurf_FlushUndo"));
  if (jpr::g_main_open_project == nullptr || jpr::g_get_main_hwnd == nullptr ||
      jpr::g_csurf_flush_undo == nullptr) {
    LOG(ERROR) << "Failed to load the functions that aren't on the API list";
    return 0;
  }
  plugin_info->Register("csurf", &jpr::g_registration);
  LOG(INFO) << "Loaded, and registered the surface type";
  return 1;
}

}  // extern "C"
