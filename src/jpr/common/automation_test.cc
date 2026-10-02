// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/automation.h"

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

TEST(AutomationTest, ReadsAndSetsTheOverride) {
  FakeReaper reaper;
  EXPECT_EQ(GetAutoOverride(), AutoOverride::kNone);

  SetAutoOverride(AutoOverride::kWrite);
  EXPECT_EQ(reaper.GetProject().GetAutomationOverride(), 3);

  // REAPER's bypass is 6, not the 5 the SDK documents.
  reaper.GetProject().SetAutomationOverride(6);
  EXPECT_EQ(GetAutoOverride(), AutoOverride::kBypass);

  SetAutoOverride(AutoOverride::kNone);
  EXPECT_EQ(reaper.GetProject().GetAutomationOverride(), -1);
}

}  // namespace
}  // namespace jpr
