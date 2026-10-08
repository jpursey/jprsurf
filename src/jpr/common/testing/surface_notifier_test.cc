// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/surface_notifier.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

// The tests here need the fake: they give REAPER's actions handlers, run the
// surface, or make changes in REAPER's own UI. What REAPER calls from inside
// its functions is checked by contract tests, which run in REAPER too (see
// surface_notifier_contract_test.cc).

namespace jpr {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class SurfaceNotifierTest : public ::testing::Test {
 protected:
  SurfaceNotifierTest() {
    reaper_.GetProject().AddTrack("A");
    reaper_.GetProject().AddTrack("B");
    reaper_.GetPluginInfo().Register("csurf",
                                     RecordingSurface::GetRegistration());
    surface_ = reaper_.AddSurface();
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
  }

  // Returns the calls the surface got since the last time, and forgets them.
  std::vector<std::string> TakeCalls() {
    return RecordingSurface::Get()->TakeCalls();
  }

  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;
  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(SurfaceNotifierTest, CallsDuringARunArePartOfIt) {
  // A run that changes two tracks in one batch, which the fake would fail as
  // separate changes, or as an unbalanced batch, if the calls the notifier
  // makes during it ended the run.
  RecordingSurface::Get()->SetOnRun([this] {
    PreventUIRefresh(1);
    SetTrackUIMute(a_, 1, 0);
    SetTrackUIMute(b_, 1, 0);
    PreventUIRefresh(-1);
  });
  surface_->Run();
  EXPECT_THAT(
      TakeCalls(),
      ElementsAre("SetSurfaceSolo(master, false)",
                  "SetSurfaceSolo(master, false)", "SetSurfaceMute(A, true)",
                  "SetSurfaceSolo(A, false)", "SetSurfaceMute(B, true)",
                  "SetSurfaceSolo(B, false)"));
}

TEST_F(SurfaceNotifierTest, SendsNothingWithNoSurfaceOpen) {
  // With the surface removed, a call on it would use a destroyed surface.
  surface_.reset();
  ASSERT_EQ(RecordingSurface::Get(), nullptr);
  SetTrackUIMute(a_, 1, 0);
  SetTrackUIRecArm(a_, 1, 0);
  Main_OnCommand(kUndoAction, 0);
}

// A mute or solo outside a batch is sent before the next run, which isn't made
// (see SurfaceNotifier), so the next run's batch doesn't send it.
TEST_F(SurfaceNotifierTest, MuteOutsideABatchIsForgottenAtTheNextRun) {
  SetTrackUIMute(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)"));
  reaper_.EndEntryPoint();

  RecordingSurface::Get()->SetOnRun([this] {
    PreventUIRefresh(1);
    SetTrackUISolo(b_, 1, 0);
    PreventUIRefresh(-1);
  });
  surface_->Run();
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, true)",
                                       "SetSurfaceMute(B, false)",
                                       "SetSurfaceSolo(B, true)"));
}

// REAPER's own actions can't be given a handler.
TEST_F(SurfaceNotifierTest, ActionsSendAfterTheirHandler) {
  const int write = kFirstAutoModeAction + static_cast<int>(AutoMode::kWrite);
  std::vector<std::string> calls_before_handler = {"Not run"};
  reaper_.AddCommand(
      {.id = write, .on_run = [&] { calls_before_handler = TakeCalls(); }});
  Main_OnCommand(write, 0);
  EXPECT_THAT(calls_before_handler, IsEmpty());
  std::vector<std::string> calls = TakeCalls();
  ASSERT_FALSE(calls.empty());
  EXPECT_EQ(calls.front(), "SetAutoMode(3)");
}

// REAPER's repeat state is the user's.
TEST_F(SurfaceNotifierTest, UndoSendsTheRepeatState) {
  reaper_.AddCommand({.id = kRepeatAction, .toggle_state = 1});
  Main_OnCommand(kUndoAction, 0);
  EXPECT_THAT(TakeCalls(), Contains("SetRepeatState(true)"));
}

//------------------------------------------------------------------------------
// Changes made in REAPER's own UI
//
// REAPER's UI is the user's. Each change is made between runs, as REAPER makes
// it, and each of its calls is an entry point of its own.
//------------------------------------------------------------------------------

TEST_F(SurfaceNotifierTest, ClickingATrackSelectsOnlyIt) {
  FakeTrack* const a = reaper_.GetProject().GetTrack(0);
  FakeTrack* const b = reaper_.GetProject().GetTrack(1);
  a->selected = true;

  notifier_.ClickTrack(b);
  EXPECT_FALSE(a->selected);
  EXPECT_TRUE(b->selected);
  EXPECT_THAT(
      TakeCalls(),
      ElementsAre("SetSurfaceSelected(A, false)", "SetSurfaceSelected(B, true)",
                  "OnTrackSelection(B)", "Extended(SETLASTTOUCHEDTRACK, B)"));
}

TEST_F(SurfaceNotifierTest, CtrlClickingATrackAddsIt) {
  FakeTrack* const a = reaper_.GetProject().GetTrack(0);
  FakeTrack* const b = reaper_.GetProject().GetTrack(1);
  a->selected = true;

  notifier_.CtrlClickTrack(b);
  EXPECT_TRUE(a->selected);
  EXPECT_TRUE(b->selected);
  EXPECT_THAT(TakeCalls(),
              ElementsAre("SetSurfaceSelected(B, true)", "OnTrackSelection(B)",
                          "Extended(SETLASTTOUCHEDTRACK, B)"));
}

TEST_F(SurfaceNotifierTest, ClickingMuteTogglesItAndTouchesTheTrack) {
  FakeTrack* const a = reaper_.GetProject().GetTrack(0);

  notifier_.ClickMute(a);
  EXPECT_TRUE(a->mute);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETLASTTOUCHEDTRACK, A)",
                                       "SetSurfaceMute(A, true)",
                                       "SetSurfaceSolo(A, false)"));

  notifier_.ClickMute(a);
  EXPECT_FALSE(a->mute);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETLASTTOUCHEDTRACK, A)",
                                       "SetSurfaceMute(A, false)",
                                       "SetSurfaceSolo(A, false)"));
}

TEST_F(SurfaceNotifierTest, ChangesNoTraceHasShownFail) {
  FakeTrack* const master = reaper_.GetProject().GetMasterTrack();
  FakeTrack* const a = reaper_.GetProject().GetTrack(0);
  FakeTrack* const b = reaper_.GetProject().GetTrack(1);
  a->selected = true;
  b->selected = true;

  EXPECT_NONFATAL_FAILURE(notifier_.ClickTrack(master),
                          "a click on the master");
  EXPECT_NONFATAL_FAILURE(notifier_.CtrlClickTrack(master),
                          "a Ctrl+click on the master");
  EXPECT_NONFATAL_FAILURE(notifier_.CtrlClickTrack(a),
                          "a Ctrl+click that unselects a track");
  EXPECT_NONFATAL_FAILURE(notifier_.ClickMute(master),
                          "a click on the master's mute");
  EXPECT_NONFATAL_FAILURE(
      notifier_.ClickMute(a),
      "a click on the mute of one of several selected tracks");
  EXPECT_FALSE(master->selected);
  EXPECT_FALSE(master->mute);
  EXPECT_TRUE(a->selected);
  EXPECT_FALSE(a->mute);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierTest, ChangesInsideABatchFail) {
  FakeTrack* const a = reaper_.GetProject().GetTrack(0);
  PreventUIRefresh(1);
  EXPECT_NONFATAL_FAILURE(notifier_.ClickMute(a), "PreventUIRefresh() scope");
  EXPECT_FALSE(a->mute);
  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

}  // namespace
}  // namespace jpr
