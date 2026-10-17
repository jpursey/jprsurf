// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/call_counter.h"

#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/testing/test_control_surface.h"

namespace jpr {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class CallCounterTest : public ::testing::Test {
 protected:
  CallCounterTest() {
    FakeProject& project = reaper_.GetProject();
    project.AddSend(project.AddTrack("A"), project.AddTrack("B"));
    reaper_.GetPluginInfo().Register("csurf",
                                     RecordingSurface::GetRegistration());
    surface_ = reaper_.AddSurface();
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
    counter_.TakeCounts();
  }

  // Runs the surface once, making `calls` in the run.
  void RunWith(absl::AnyInvocable<void()> calls) {
    RecordingSurface::Get()->SetOnRun(std::move(calls));
    surface_->Run();
  }

  FakeReaper reaper_;
  std::unique_ptr<TestControlSurface> surface_;
  CallCounter counter_{&reaper_};
  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

//------------------------------------------------------------------------------
// Counting
//------------------------------------------------------------------------------

TEST_F(CallCounterTest, CountsReadsAsCheapAndScansAsModerate) {
  RunWith([] {
    CountTracks(nullptr);
    CountSelectedMediaItems(nullptr);
  });
  const CallCounts counts = counter_.TakeCounts();
  EXPECT_EQ(counts, (CallCounts{.cheap = 1, .moderate = 1}));
  EXPECT_THAT(counts.calls,
              UnorderedElementsAre(Pair("CountTracks", 1),
                                   Pair("CountSelectedMediaItems", 1)));
}

TEST_F(CallCounterTest, TextListsEachFunctionsCountByName) {
  RunWith([] {
    CountSelectedMediaItems(nullptr);
    CountTracks(nullptr);
  });
  EXPECT_EQ(counter_.TakeCounts().ToString(),
            "{cheap: 1, moderate: 1, refreshes: 0, undo_points: 0, "
            "commands: []}\n"
            "  CountSelectedMediaItems: 1\n"
            "  CountTracks: 1");
}

TEST_F(CallCounterTest, SetterOutsideADeferredRefreshIsARefresh) {
  RunWith([this] { SetTrackUIMute(a_, 1, 0); });
  EXPECT_EQ(counter_.TakeCounts(), (CallCounts{.refreshes = 1}));
}

TEST_F(CallCounterTest, DeferrableSettersAreModerateAndTheEndARefresh) {
  RunWith([this] {
    PreventUIRefresh(1);
    SetTrackUIMute(a_, 1, 0);
    SetTrackUISolo(b_, 1, 0);
    PreventUIRefresh(-1);
  });
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.cheap = 1, .moderate = 2, .refreshes = 1}));
}

TEST_F(CallCounterTest, OnlyTheEndOfTheOutermostDeferredRefreshIsARefresh) {
  RunWith([this] {
    PreventUIRefresh(1);
    PreventUIRefresh(1);
    CSurf_OnVolumeChangeEx(a_, 0.5, false, false);
    PreventUIRefresh(-1);
    PreventUIRefresh(-1);
  });
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.cheap = 3, .moderate = 1, .refreshes = 1}));
}

TEST_F(CallCounterTest, EmptyDeferredRefreshIsNoRefresh) {
  RunWith([] {
    PreventUIRefresh(1);
    PreventUIRefresh(-1);
  });
  EXPECT_EQ(counter_.TakeCounts(), (CallCounts{.cheap = 2}));
}

TEST_F(CallCounterTest, RefreshThatIsntDeferrableIsOnlyItsOwn) {
  RunWith([this] {
    PreventUIRefresh(1);
    ShowConsoleMsg("Refreshed");
    PreventUIRefresh(-1);
  });
  EXPECT_EQ(counter_.TakeCounts(), (CallCounts{.cheap = 2, .refreshes = 1}));
}

TEST_F(CallCounterTest, CountsUndoPoints) {
  RunWith([] { Undo_OnStateChangeEx("Change", UNDO_STATE_TRACKCFG, -1); });
  EXPECT_EQ(counter_.TakeCounts(), (CallCounts{.undo_points = 1}));
}

TEST_F(CallCounterTest, CountsTheActionsRunInOrder) {
  RunWith([] {
    Main_OnCommand(kRedoAction, 0);
    Main_OnCommand(kUndoAction, 0);
  });
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.commands = {kRedoAction, kUndoAction}}));
}

TEST_F(CallCounterTest, CountsAcrossRuns) {
  RunWith([this] { SetTrackUIMute(a_, 1, 0); });
  RunWith([this] { SetTrackUIMute(a_, 0, 0); });
  EXPECT_EQ(counter_.TakeCounts(), (CallCounts{.refreshes = 2}));
}

TEST_F(CallCounterTest, TakingTheCountsStartsAgain) {
  RunWith([] { CountTracks(nullptr); });
  counter_.TakeCounts();
  RunWith([] {});
  EXPECT_EQ(counter_.TakeCounts(), CallCounts{});
}

//------------------------------------------------------------------------------
// Rules
//------------------------------------------------------------------------------

TEST_F(CallCounterTest, TwoRefreshesInARunFailAsItEnds) {
  auto calls = [this] {
    SetGlobalAutomationOverride(1);
    SetTrackUIMute(a_, 1, 0);
  };
  EXPECT_NONFATAL_FAILURE(
      RunWith(std::move(calls)),
      "refreshed REAPER's UI 2 times, with SetGlobalAutomationOverride, "
      "SetTrackUIMute");
}

TEST_F(CallCounterTest, TwoUndoPointsInARunFail) {
  auto calls = [] {
    Undo_OnStateChangeEx("One", UNDO_STATE_TRACKCFG, -1);
    Undo_OnStateChangeEx("Two", UNDO_STATE_TRACKCFG, -1);
  };
  EXPECT_NONFATAL_FAILURE(RunWith(std::move(calls)), "added 2 undo points");
}

TEST_F(CallCounterTest, RepeatedSetterOnATrackFails) {
  auto calls = [this] {
    PreventUIRefresh(1);
    SetTrackUIMute(a_, 1, 0);
    SetTrackUIMute(a_, 0, 0);
    PreventUIRefresh(-1);
  };
  EXPECT_NONFATAL_FAILURE(RunWith(std::move(calls)), "repeated SetTrackUIMute");
}

TEST_F(CallCounterTest, RepeatedActionFails) {
  auto calls = [] {
    Main_OnCommand(kUndoAction, 0);
    Main_OnCommand(kUndoAction, 0);
  };
  EXPECT_NONFATAL_FAILURE(RunWith(std::move(calls)), "repeated Main_OnCommand");
}

TEST_F(CallCounterTest,
       RefreshThatIsntDeferrableIsASecondBesideADeferrableOne) {
  auto calls = [this] {
    PreventUIRefresh(1);
    ShowConsoleMsg("Refreshed");
    SetTrackUIMute(b_, 1, 0);
    PreventUIRefresh(-1);
  };
  EXPECT_NONFATAL_FAILURE(RunWith(std::move(calls)),
                          "refreshed REAPER's UI 2 times, with ShowConsoleMsg, "
                          "PreventUIRefresh");
}

// The tests that pass a rule also count the calls, so they show the counter
// saw what the rule passed.

TEST_F(CallCounterTest, SameSetterOnTwoTracksPasses) {
  RunWith([this] {
    PreventUIRefresh(1);
    SetTrackUIMute(a_, 1, 0);
    SetTrackUIMute(b_, 1, 0);
    PreventUIRefresh(-1);
  });
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.cheap = 1, .moderate = 2, .refreshes = 1}));
}

TEST_F(CallCounterTest, RulesAreForEachRun) {
  RunWith([this] {
    SetTrackUIMute(a_, 1, 0);
    Undo_OnStateChangeEx("Mute", UNDO_STATE_TRACKCFG, -1);
    Main_OnCommand(kUndoAction, 0);
  });
  RunWith([this] {
    SetTrackUIMute(a_, 0, 0);
    Undo_OnStateChangeEx("Unmute", UNDO_STATE_TRACKCFG, -1);
    Main_OnCommand(kUndoAction, 0);
  });
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.refreshes = 2,
                        .undo_points = 2,
                        .commands = {kUndoAction, kUndoAction}}));
}

TEST_F(CallCounterTest, CallsBetweenRunsAreAnEntryPointOfTheirOwn) {
  RunWith([this] {
    SetTrackUIMute(a_, 1, 0);
    Undo_OnStateChangeEx("Mute", UNDO_STATE_TRACKCFG, -1);
  });
  SetTrackUIMute(a_, 0, 0);
  Undo_OnStateChangeEx("Unmute", UNDO_STATE_TRACKCFG, -1);
  reaper_.EndEntryPoint();
  EXPECT_EQ(counter_.TakeCounts(),
            (CallCounts{.refreshes = 2, .undo_points = 2}));
}

}  // namespace
}  // namespace jpr
