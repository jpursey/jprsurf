// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// ContractTest in REAPER, and the entry point of reaper_jpr_check, the DLL
// that runs the contract tests inside the test install of REAPER (see
// test_install.h). REAPER creates the RecordingSurface at startup, and its
// first run runs every test, writing what gtest printed to output.txt.

#include <cstdio>
#include <filesystem>
#include <memory>

#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/testing/test_install.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

void RunTests() {
  RecordingSurface::Get()->SetOnRun(nullptr);
  LOG(INFO) << "Running the tests";

  // gtest prints to stdout, which REAPER has nowhere to show.
  std::FILE* output = nullptr;
  const std::filesystem::path output_path = GetRunDir() / "output.txt";
  if (_wfreopen_s(&output, output_path.c_str(), L"w", stdout) != 0) {
    output = nullptr;
  }

  int argc = 1;
  char program[] = "reaper_jpr_check";
  char* argv[] = {program, nullptr};
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  std::fflush(stdout);
  LOG(INFO) << "Ran the tests: " << (result == 0 ? "passed" : "failed");
  FinishRun(result == 0);
}

IReaperControlSurface* CreateSurface(const char* type_string,
                                     const char* config_string,
                                     int* err_stats) {
  LOG(INFO) << "Created the surface, with config \""
            << (config_string != nullptr ? config_string : "(null)") << "\"";
  auto* surface = new RecordingSurface;
  if (!GetRunDir().empty()) {
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
  EXPECT_TRUE(OpenProjectFile(*project)) << "Failed to write project.rpp";
  TakeCalls();
}

void ContractTest::EndEntryPoint() {}

}  // namespace jpr

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  return jpr::LoadTestInstall(plugin_info, &jpr::g_registration) ? 1 : 0;
}

}  // extern "C"
