// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/testing/call_counter.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/large_project.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/default_config/default_config_test.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
using StripButton = FakeXTouch::StripButton;

//==============================================================================
// CallCountTest
//
// The REAPER calls the default config makes on the large project (see
// AddLargeProject()), as exact counts by cost (see CallCounts), so a change
// that makes more calls, or costlier ones, fails, and so does one that makes
// fewer, until its bounds are lowered with it.
//
// Each test counts only what it does: the counts of starting up are taken
// first. A gesture's counts include the runs that settle it, as the surface
// test helpers make them, and the steady state tests show what those cost.
//==============================================================================

class CallCountTest : public DefaultConfigTest {
 protected:
  static constexpr int kBus = 0;     // T1, with 31 receives.
  static constexpr int kFolder = 4;  // T5, with 9 tracks.

  CallCountTest() {
    AddLargeProject(&reaper_.GetProject());
    AddSurface();
    startup_ = calls_.TakeCounts();
  }

  // Runs the surface for a second, and returns its counts.
  CallCounts CountASecond() {
    calls_.TakeCounts();
    surface_->RunFor(absl::Seconds(1));
    return calls_.TakeCounts();
  }

  CallCounts startup_;
};

TEST_F(CallCountTest, StartingUp) {
  EXPECT_EQ(startup_, (CallCounts{.cheap = 3108, .moderate = 188}));
}

//------------------------------------------------------------------------------
// Steady state
//
// A second of runs, which includes one poll of the track filters: 256 cheap
// reads, two for each track. A count off by that much is the poll moving in or
// out of the second, rather than a change to what each run does.
//------------------------------------------------------------------------------

TEST_F(CallCountTest, TrackModeAtTheTop) {
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 3436, .moderate = 30}));
}

TEST_F(CallCountTest, TrackModeInAFolder) {
  DoublePressSelect(kFolder);
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 2386, .moderate = 30}));
}

// The timecode shows the play position, which moves on each run, as it does
// in REAPER.
TEST_F(CallCountTest, TrackModeWhilePlaying) {
  FakeProject& project = reaper_.GetProject();
  project.SetPlayState(1);
  RunUntilShown();
  calls_.TakeCounts();
  for (int i = 0; i < FakeReaper::kRunsPerSecond; ++i) {
    project.SetPlayPosition(project.GetPlayPosition() +
                            absl::ToDoubleSeconds(FakeReaper::GetRunTime()));
    surface_->Run();
  }
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 3496, .moderate = 60}));
}

TEST_F(CallCountTest, SendReceiveModeOnABus) {
  EnterSendMode(kBus);
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 4306, .moderate = 30}));
}

//------------------------------------------------------------------------------
// Infrequent events
//------------------------------------------------------------------------------

TEST_F(CallCountTest, TrackListRefresh) {
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 2660, .moderate = 131}));
}

TEST_F(CallCountTest, BankChange) {
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 850, .moderate = 8}));
}

TEST_F(CallCountTest, EnteringSendReceiveMode) {
  TapSelect(kBus);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 1031, .moderate = 23}));
}

TEST_F(CallCountTest, ReturningToTrackMode) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAssignTrack);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 899, .moderate = 24}));
}

//==============================================================================
// Gestures
//
// Each gesture the surface tests make, by area as they are. A gesture on a
// strip is on the X-Touch's first strip (strip 8, T9) unless it needs another.
//==============================================================================

//------------------------------------------------------------------------------
// Track strips
//------------------------------------------------------------------------------

TEST_F(CallCountTest, TappingMute) {
  Tap(xtouch_, StripButton::kMute, 0);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 852, .moderate = 10, .refreshes = 1, .undo_points = 1}));
}

TEST_F(CallCountTest, TappingSolo) {
  Tap(xtouch_, StripButton::kSolo, 0);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 852, .moderate = 10, .refreshes = 1, .undo_points = 1}));
}

TEST_F(CallCountTest, TappingRecordArm) {
  Tap(xtouch_, StripButton::kRec, 0);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 3300, .moderate = 139, .refreshes = 1, .undo_points = 1}));
}

// Holding mute on T7, on the extender, and tapping it on T10, on the X-Touch.
TEST_F(CallCountTest, MutingARange) {
  PressRange(StripButton::kMute, 6, 9);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 1074, .moderate = 18, .refreshes = 2, .undo_points = 2}));
}

TEST_F(CallCountTest, MovingAFader) {
  MoveFader(xtouch_, 0, 0);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 321, .moderate = 4, .refreshes = 1}));
}

TEST_F(CallCountTest, TurningAPot) {
  xtouch_.TurnPot(0, 21);
  RunUntilShown();
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 213, .moderate = 3, .refreshes = 1}));
}

TEST_F(CallCountTest, MovingTheMasterFader) {
  MoveFader(xtouch_, FakeXTouch::kMasterFader, 0);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 321, .moderate = 3, .refreshes = 1}));
}

// Centers a pan, so it has one to change.
TEST_F(CallCountTest, PressingAPot) {
  reaper_.GetProject().FindTrackByName("T9")->pan = 0.5;
  RunUntilShown();
  calls_.TakeCounts();
  Tap(xtouch_, StripButton::kPotButton, 0);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 850, .moderate = 9, .refreshes = 1}));
}

TEST_F(CallCountTest, TappingSelect) {
  TapSelect(8);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 858, .moderate = 9, .refreshes = 1}));
}

TEST_F(CallCountTest, SelectingARange) {
  SelectRange(0, 11);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 2393, .moderate = 44, .refreshes = 2}));
}

TEST_F(CallCountTest, GoingIntoAFolder) {
  DoublePressSelect(kFolder);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 784, .moderate = 19}));
}

//------------------------------------------------------------------------------
// The track list
//------------------------------------------------------------------------------

TEST_F(CallCountTest, Channel) {
  Tap(xtouch_, Button::kChannelRight);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 850, .moderate = 8}));
}

TEST_F(CallCountTest, GlobalGoesUpALevel) {
  DoublePressSelect(kFolder);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kGlobal);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 815, .moderate = 24}));
}

TEST_F(CallCountTest, HoldingGlobalGoesToTheTop) {
  DoublePressSelect(kFolder);
  calls_.TakeCounts();
  LongPress(xtouch_, Button::kGlobal);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 1631, .moderate = 35}));
}

// A track in a folder the surface isn't showing, which it goes to.
TEST_F(CallCountTest, ClickingATrackInReaper) {
  notifier_.ClickTrack(reaper_.GetProject().FindTrackByName("T12.5"));
  RunUntilShown();
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 153, .moderate = 11}));
}

//------------------------------------------------------------------------------
// Send/Receive mode
//
// On the bus, whose receives are on strips 0-14 (the extender first).
//------------------------------------------------------------------------------

TEST_F(CallCountTest, PickingWithSendHeld) {
  PickWithSendHeld(kFolder);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 916, .moderate = 21}));
}

// Includes the undo point added once the route stops moving.
TEST_F(CallCountTest, MovingARouteFader) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  MoveFader(*xtouch_ext_, 0, 0);
  WaitForUndo();
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 2824, .moderate = 34, .refreshes = 1, .undo_points = 1}));
}

// Includes the undo point added once the route stops moving.
TEST_F(CallCountTest, TurningARoutePot) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  xtouch_ext_->TurnPot(0, 21);
  RunUntilShown();
  WaitForUndo();
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 2687, .moderate = 33, .refreshes = 1, .undo_points = 1}));
}

// Centers a pan, so it has one to change.
TEST_F(CallCountTest, PressingARoutePot) {
  EnterSendMode(kBus);
  FakeProject& project = reaper_.GetProject();
  project.GetReceives(project.FindTrackByName("T1"))[0]->pan = 0.5;
  RunUntilShown();
  calls_.TakeCounts();
  Tap(*xtouch_ext_, StripButton::kPotButton, 0);
  WaitForUndo();
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 3498, .moderate = 39, .refreshes = 1, .undo_points = 1}));
}

TEST_F(CallCountTest, TappingARouteMute) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  Tap(*xtouch_ext_, StripButton::kMute, 0);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 1084, .moderate = 23, .refreshes = 1}));
}

// Goes across the route, to the track at its other end.
TEST_F(CallCountTest, SelectingARoute) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  TapSelect(0);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 668, .moderate = 18}));
}

TEST_F(CallCountTest, PagingRoutes) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 1082, .moderate = 23}));
}

// From a track with both: T5.1 receives from T5, and sends to a bus.
TEST_F(CallCountTest, SwitchingBetweenSendsAndReceives) {
  DoublePressSelect(kFolder);
  EnterSendMode(0);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 538, .moderate = 9}));
}

//------------------------------------------------------------------------------
// Global buttons
//------------------------------------------------------------------------------

// On the eight tracks a range selects.
TEST_F(CallCountTest, SettingTheSelectedTracksAutomationMode) {
  SelectRange(0, 7);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAutoWrite);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 868,
                        .moderate = 8,
                        .commands = {GetAutoModeAction(AutoMode::kWrite)}}));
}

TEST_F(CallCountTest, TurningTheAutomationOverrideOn) {
  Tap(xtouch_, Button::kAutoGroup);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 908, .moderate = 8, .refreshes = 1}));
}

// Runs Transport: Play/pause (40073).
TEST_F(CallCountTest, Play) {
  Tap(xtouch_, Button::kPlay);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 850, .moderate = 8, .commands = {40073}}));
}

// Runs Move edit cursor to start of current/previous measure (41041).
TEST_F(CallCountTest, Rewind) {
  Tap(xtouch_, Button::kRewind);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 850, .moderate = 8, .commands = {41041}}));
}

// Runs Track: Unsolo all tracks (40340).
TEST_F(CallCountTest, HoldingSoloUnsolosAllTracks) {
  LongPress(xtouch_, Button::kSolo);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 2016, .moderate = 19, .commands = {40340}}));
}

// Runs Solo in front (40745), once Solo is let go before a long press.
TEST_F(CallCountTest, TappingGlobalSolo) {
  Tap(xtouch_, Button::kSolo);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 851, .moderate = 8, .commands = {40745}}));
}

TEST_F(CallCountTest, Redo) {
  TapWith(Button::kShift, Button::kUndo);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{.cheap = 1064, .moderate = 10, .commands = {kRedoAction}}));
}

TEST_F(CallCountTest, SteppingTheTimecodeMode) {
  Tap(xtouch_, Button::kShowTimeBeats);
  EXPECT_EQ(calls_.TakeCounts(),
            (CallCounts{.cheap = 916,
                        .moderate = 9,
                        .commands = {kRulerMinutesSecondsMinimal}}));
}

}  // namespace
}  // namespace jpr
