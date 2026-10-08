// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <vector>

#include "absl/functional/function_ref.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/recording_surface.h"

namespace jpr {

//==============================================================================
// ContractTest
//
// The fixture for contract tests: tests that act only through REAPER's API, so
// they run unchanged under the fake REAPER and inside REAPER (see
// docs/worklog/check_fakes_in_reaper.md). A test that passes under the fake but
// fails in REAPER shows a gap in the fake.
//
// Each test opens the project it starts from (OpenProject()), and checks what
// REAPER's API returns, and what REAPER called on the RecordingSurface that is
// open throughout (TakeCalls()).
//
// Which REAPER it runs in depends on what the test links:
// - contract_test_fake.cc: each test has its own FakeReaper, with REAPER's
//   actions (AddReaperActions()), a SurfaceNotifier, and a RecordingSurface
//   added.
//==============================================================================

class ContractTest : public ::testing::Test {
 protected:
  ContractTest();
  ~ContractTest() override;

  // Opens the project `build` makes, in place of the one open, as the current
  // project, and forgets the calls opening it made. Under the fake, `build`
  // makes it in the fake. In REAPER, it makes a project of its own, which is
  // opened from a project file (see WriteProjectFile()). It is written under
  // the fake too, so a project REAPER couldn't open fails the test.
  void OpenProject(absl::FunctionRef<void(FakeProject&)> build);

  // Ends the test's calls so far, as one of REAPER's calls on a surface would
  // end. Under the fake, its checks see each part on its own (see "Checks" in
  // FakeReaper), such as two changes to different tracks that JPRSurf would
  // batch. In REAPER, it does nothing.
  void EndEntryPoint();

  // Returns the calls the RecordingSurface got since the last time, and forgets
  // them.
  std::vector<std::string> TakeCalls() {
    RecordingSurface* surface = RecordingSurface::Get();
    if (surface == nullptr) {
      ADD_FAILURE() << "No RecordingSurface is open";
      return {};
    }
    return surface->TakeCalls();
  }
};

}  // namespace jpr
