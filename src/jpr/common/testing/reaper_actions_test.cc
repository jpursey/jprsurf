// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/reaper_actions.h"

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

class ReaperActionsTest : public ::testing::Test {
 protected:
  ReaperActionsTest() { AddReaperActions(&reaper_); }

  FakeReaper reaper_;
};

// The ruler's time units are preferences, so the contract tests only check that
// one of each group is on.
TEST_F(ReaperActionsTest, TheRulerIsMeasuresBeatsWithNoSecondaryUnit) {
  EXPECT_EQ(GetToggleCommandState(kRulerMeasuresBeats), 1);
  EXPECT_EQ(GetToggleCommandState(kRulerSecondaryNone), 1);
}

TEST_F(ReaperActionsTest, SelectingARulerModeRunsNothing) {
  SelectRulerMode(&reaper_, 40369);  // Samples.
  EXPECT_EQ(GetToggleCommandState(40369), 1);
  EXPECT_EQ(GetToggleCommandState(kRulerMeasuresBeats), 0);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());

  EXPECT_NONFATAL_FAILURE(SelectRulerMode(&reaper_, 40029), "40029");
}

}  // namespace
}  // namespace jpr
