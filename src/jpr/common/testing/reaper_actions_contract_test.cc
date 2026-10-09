// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// What REAPER's actions in reaper_action_list.h are, and what those the fake
// gives handlers do (see AddReaperActions()). The tests that need the fake are
// in reaper_actions_test.cc.

#include <string>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/reaper_action_list.h"

namespace jpr {
namespace {

// An action REAPER doesn't have.
constexpr int kMissingAction = 99999999;

// Returns the text kbd_getTextFromCmd() has for `id`.
std::string GetText(int id) {
  const char* text = kbd_getTextFromCmd(id, nullptr);
  return text != nullptr ? text : "(null)";
}

// Returns the mode in `group` that is on, or 0 if none is, failing the test
// unless exactly one is.
int GetOn(absl::Span<const RulerMode> group) {
  int on = 0;
  int on_count = 0;
  for (const RulerMode& mode : group) {
    if (GetToggleCommandState(mode.id) == 1) {
      on = mode.id;
      ++on_count;
    }
  }
  EXPECT_EQ(on_count, 1) << "ruler modes on in the group of " << group[0].id;
  return on;
}

using ReaperActionsContractTest = ContractTest;

TEST_F(ReaperActionsContractTest, EachActionHasItsTextAndToggleState) {
  // A new project, so the project's toggles, such as repeat, are off.
  OpenProject([](FakeProject& project) {});
  for (const ReaperAction& action : kReaperActions) {
    SCOPED_TRACE(action.id);
    EXPECT_EQ(GetText(action.id), action.text);
    EXPECT_EQ(GetToggleCommandState(action.id), action.toggle_state);
  }
}

TEST_F(ReaperActionsContractTest, AnActionREAPERDoesntHaveHasNoTextOrToggle) {
  EXPECT_EQ(GetText(kMissingAction), "");
  EXPECT_EQ(GetToggleCommandState(kMissingAction), -1);
}

TEST_F(ReaperActionsContractTest, AutomationModeActionsSetTheSelectedTracks) {
  OpenProject([](FakeProject& project) {
    project.GetMasterTrack()->selected = true;
    project.AddTrack("Selected")->selected = true;
    project.AddTrack("Not selected");
  });
  MediaTrack* master = GetMasterTrack(nullptr);
  MediaTrack* selected = GetTrack(nullptr, 0);
  MediaTrack* not_selected = GetTrack(nullptr, 1);
  for (int mode = kAutoModeCount - 1; mode >= 0; --mode) {
    SCOPED_TRACE(mode);
    Main_OnCommand(GetAutoModeAction(static_cast<AutoMode>(mode)), 0);
    EXPECT_EQ(GetMediaTrackInfo_Value(master, "I_AUTOMODE"), mode);
    EXPECT_EQ(GetMediaTrackInfo_Value(selected, "I_AUTOMODE"), mode);
    EXPECT_EQ(GetMediaTrackInfo_Value(not_selected, "I_AUTOMODE"),
              static_cast<double>(AutoMode::kTrimRead));
  }
}

TEST_F(ReaperActionsContractTest, CommandsAreLookedUpByNumberUnlessNamed) {
  EXPECT_EQ(NamedCommandLookup("40029"), 40029);
  EXPECT_EQ(NamedCommandLookup("40029x"), 40029);
  EXPECT_EQ(NamedCommandLookup("abc"), 0);
  EXPECT_EQ(NamedCommandLookup("_40029"), 0);
  EXPECT_EQ(NamedCommandLookup("_JPR_MISSING"), 0);
}

//------------------------------------------------------------------------------
// The ruler's time units
//------------------------------------------------------------------------------

// The time units are preferences, which outlast the test in REAPER, so each
// test turns back on those that were, in a group it changed.
class RulerModeContractTest : public ContractTest {
 protected:
  RulerModeContractTest()
      : mode_(GetOn(kRulerModes)),
        secondary_mode_(GetOn(kRulerSecondaryModes)) {}

  ~RulerModeContractTest() override {
    if (GetOn(kRulerModes) != mode_) {
      Main_OnCommand(mode_, 0);
    }
    if (GetOn(kRulerSecondaryModes) != secondary_mode_) {
      Main_OnCommand(secondary_mode_, 0);
    }
  }

  const int mode_;
  const int secondary_mode_;
};

TEST_F(RulerModeContractTest, EachModeHasItsText) {
  for (absl::Span<const RulerMode> group : kRulerGroups) {
    for (const RulerMode& mode : group) {
      EXPECT_EQ(GetText(mode.id), mode.text);
    }
  }
}

TEST_F(RulerModeContractTest, EachGroupIsARadioGroup) {
  Main_OnCommand(kRulerSeconds, 0);
  EXPECT_EQ(GetOn(kRulerModes), kRulerSeconds);
  EXPECT_EQ(GetOn(kRulerSecondaryModes), secondary_mode_);

  Main_OnCommand(kRulerSecondaryFrames, 0);
  EXPECT_EQ(GetOn(kRulerSecondaryModes), kRulerSecondaryFrames);
  EXPECT_EQ(GetOn(kRulerModes), kRulerSeconds);
}

TEST_F(RulerModeContractTest, RunningTheModeThatIsOnLeavesItOn) {
  Main_OnCommand(mode_, 0);
  EXPECT_EQ(GetOn(kRulerModes), mode_);
  Main_OnCommand(secondary_mode_, 0);
  EXPECT_EQ(GetOn(kRulerSecondaryModes), secondary_mode_);
}

}  // namespace
}  // namespace jpr
