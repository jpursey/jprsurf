// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/reaper_actions.h"

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

class ReaperActionsTest : public ::testing::Test {
 protected:
  ReaperActionsTest() { AddReaperActions(&reaper_); }

  FakeReaper reaper_;
};

TEST_F(ReaperActionsTest, AddsActionsAsREAPERReportsThem) {
  EXPECT_STREQ(kbd_getTextFromCmd(40029, nullptr), "Edit: Undo");
  EXPECT_EQ(GetToggleCommandState(40029), -1);
  EXPECT_STREQ(kbd_getTextFromCmd(40364, nullptr), "Options: Toggle metronome");
  EXPECT_EQ(GetToggleCommandState(40364), 0);
}

TEST_F(ReaperActionsTest, AutomationModeActionsSetTheSelectedTracksModes) {
  FakeProject& project = reaper_.GetProject();
  FakeTrack* master = project.GetMasterTrack();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bass = project.AddTrack("Bass");
  master->selected = true;
  bass->selected = true;

  Main_OnCommand(kFirstAutoModeAction + static_cast<int>(AutoMode::kWrite), 0);
  EXPECT_EQ(master->auto_mode, static_cast<int>(AutoMode::kWrite));
  EXPECT_EQ(bass->auto_mode, static_cast<int>(AutoMode::kWrite));
  EXPECT_EQ(drums->auto_mode, static_cast<int>(AutoMode::kTrimRead));

  Main_OnCommand(kLastAutoModeAction, 0);
  EXPECT_EQ(bass->auto_mode, static_cast<int>(AutoMode::kLatch));
}

TEST_F(ReaperActionsTest, RulerModesAreRadioGroups) {
  EXPECT_EQ(GetToggleCommandState(40367), 1);  // Measure.Beats.
  EXPECT_EQ(GetToggleCommandState(42360), 1);  // No secondary unit.

  Main_OnCommand(40368, 0);  // Seconds.
  EXPECT_EQ(GetToggleCommandState(40368), 1);
  EXPECT_EQ(GetToggleCommandState(40367), 0);
  EXPECT_EQ(GetToggleCommandState(42360), 1);

  Main_OnCommand(42364, 0);  // Secondary Hours:Minutes:Seconds:Frame.
  EXPECT_EQ(GetToggleCommandState(42364), 1);
  EXPECT_EQ(GetToggleCommandState(42360), 0);
  EXPECT_EQ(GetToggleCommandState(40368), 1);
}

TEST_F(ReaperActionsTest, SelectingARulerModeRunsNothing) {
  SelectRulerMode(&reaper_, 40369);  // Samples.
  EXPECT_EQ(GetToggleCommandState(40369), 1);
  EXPECT_EQ(GetToggleCommandState(40367), 0);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());

  EXPECT_NONFATAL_FAILURE(SelectRulerMode(&reaper_, 40029), "40029");
}

}  // namespace
}  // namespace jpr
