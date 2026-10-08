// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// ContractTest under the fake REAPER.

#include <memory>

#include "absl/functional/function_ref.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/project_file.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"

namespace jpr {
namespace {

// The fake REAPER a contract test runs in.
class FakeEnvironment final {
 public:
  FakeEnvironment() {
    AddReaperActions(&reaper_);
    reaper_.GetPluginInfo().Register("csurf",
                                     RecordingSurface::GetRegistration());
    surface_ = reaper_.AddSurface();
  }

  FakeReaper& GetReaper() { return reaper_; }

 private:
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;
};

// The current test's fake REAPER.
std::unique_ptr<FakeEnvironment> g_environment;

}  // namespace

ContractTest::ContractTest() {
  g_environment = std::make_unique<FakeEnvironment>();
}

ContractTest::~ContractTest() { g_environment.reset(); }

void ContractTest::OpenProject(absl::FunctionRef<void(FakeProject&)> build) {
  FakeProject& project = g_environment->GetReaper().NewProject();
  build(project);
  WriteProjectFile(project);
  TakeCalls();
}

void ContractTest::EndEntryPoint() {
  g_environment->GetReaper().EndEntryPoint();
}

}  // namespace jpr
