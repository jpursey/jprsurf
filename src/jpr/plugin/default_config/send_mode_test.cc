// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/undo.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/default_config/default_config_test.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;

using Button = FakeXTouch::Button;
using Light = FakeXTouch::Light;
using Ring = FakeXTouch::Ring;
using ScribbleColor = FakeXTouch::ScribbleColor;
using StripButton = FakeXTouch::StripButton;

// Six tracks, on the extender's first strips:
// - T1 sends to T4 and T5.
// - T2 and T3 send to T4.
// - T4 is a bus, which receives from T1, T2, and T3, and sends on to T5.
// - T5 only receives, from T1 and T4.
// - T6 has no routes.
//
// In Send/Receive mode, strips 0-14 show the routes, and the X-Touch's last
// strip (strip 15) is the Info strip, which shows the track whose routes they
// are.
class SendModeTest : public DefaultConfigTest {
 protected:
  static constexpr int kTrackCount = 6;
  static constexpr int kSource = 0;        // T1.
  static constexpr int kBus = 3;           // T4.
  static constexpr int kOnlyReceives = 4;  // T5.
  static constexpr int kNoRoutes = 5;      // T6.
  static constexpr int kInfoStrip = 15;

  SendModeTest() {
    tracks_ = AddTracks(kTrackCount);
    FakeProject& project = reaper_.GetProject();
    source_to_bus_ = project.AddSend(tracks_[kSource], tracks_[kBus]);
    project.AddSend(tracks_[kSource], tracks_[kOnlyReceives]);
    project.AddSend(tracks_[1], tracks_[kBus]);
    project.AddSend(tracks_[2], tracks_[kBus]);
    project.AddSend(tracks_[kBus], tracks_[kOnlyReceives]);
    AddSurface();
  }

  // Holds Send, and taps select on `strip`.
  void PickWithSendHeld(int strip) {
    xtouch_.Press(Button::kAssignSend);
    surface_->Run();
    TapSelect(strip);
    xtouch_.Release(Button::kAssignSend);
    RunUntilShown();
  }

  // Lets the route changes' undo point be added.
  void WaitForUndo() {
    surface_->RunFor(ContinuousUndo::kDelay + FakeReaper::GetRunTime());
  }

  // Adds `count` tracks, each with a send from T1, as the user does in REAPER.
  void AddSourceSends(int count) {
    for (FakeTrack* track : AddTracks(count)) {
      reaper_.GetProject().AddSend(tracks_[kSource], track);
    }
    surface_->SetTrackListChange();
  }

  std::vector<FakeTrack*> tracks_;
  FakeRoute* source_to_bus_ = nullptr;
};

//------------------------------------------------------------------------------
// Lights
//
// Track blinks in Track mode, and Send is lit while the selected track has
// routes to show. Send blinks in Send/Receive mode, and Track is lit.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, SendIsLitWhileTheSelectedTrackHasRoutes) {
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignTrack), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kOff);

  TapSelect(kNoRoutes);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kOff);

  TapSelect(kOnlyReceives);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kOn);
}

TEST_F(SendModeTest, SendBlinksInSendMode) {
  EnterSendMode(kSource);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignTrack), Light::kOn);
}

TEST_F(SendModeTest, TrackReturnsToTrackMode) {
  EnterSendMode(kSource);
  Tap(xtouch_, Button::kAssignTrack);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignTrack), Light::kBlinking);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetName(kInfoStrip), "");
}

//------------------------------------------------------------------------------
// Entering
//------------------------------------------------------------------------------

TEST_F(SendModeTest, TappingSendShowsTheSelectedTracksSends) {
  EnterSendMode(kSource);
  EXPECT_EQ(GetName(0), "T4");
  EXPECT_EQ(GetName(1), "T5");
  EXPECT_EQ(GetName(2), "");
  EXPECT_EQ(GetName(kInfoStrip), "T1");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
}

TEST_F(SendModeTest, TappingSendWithoutRoutesStaysInTrackMode) {
  EnterSendMode(kNoRoutes);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignTrack), Light::kBlinking);
  EXPECT_EQ(GetName(0), "T1");
}

TEST_F(SendModeTest, HoldingSendLightsSelectForTracksWithRoutes) {
  TapSelect(kSource);
  xtouch_.Press(Button::kAssignSend);
  RunUntilShown();
  for (int strip = 0; strip < kTrackCount; ++strip) {
    EXPECT_EQ(GetLight(StripButton::kSelect, strip),
              strip == kNoRoutes ? Light::kOff : Light::kOn)
        << strip;
  }

  xtouch_.Release(Button::kAssignSend);
  RunUntilShown();
  EXPECT_EQ(GetLight(StripButton::kSelect, kSource), Light::kOn);
  EXPECT_EQ(GetLight(StripButton::kSelect, kBus), Light::kOff);
}

TEST_F(SendModeTest, HoldingSendAndPressingSelectShowsTheTracksRoutes) {
  PickWithSendHeld(1);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kBlinking);
  EXPECT_EQ(GetName(kInfoStrip), "T2");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
  EXPECT_EQ(GetName(0), "T4");
}

TEST_F(SendModeTest, HoldingSendAndPressingATrackWithoutRoutesDoesNothing) {
  PickWithSendHeld(kNoRoutes);
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignTrack), Light::kBlinking);
  EXPECT_EQ(GetName(0), "T1");
}

TEST_F(SendModeTest, HoldingSendPicksRatherThanSelectingARange) {
  Hold(*xtouch_ext_, StripButton::kSelect, kSource);
  PickWithSendHeld(1);
  xtouch_ext_->Release(StripButton::kSelect, kSource);
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "T2");
  EXPECT_TRUE(tracks_[kSource]->selected);
  EXPECT_FALSE(tracks_[1]->selected);
}

//------------------------------------------------------------------------------
// Route strips
//------------------------------------------------------------------------------

TEST_F(SendModeTest, RouteStripShowsTheOtherTracksColor) {
  tracks_[kBus]->color = 0x010000FF;  // As 0x01BBGGRR. T5 has none.
  EnterSendMode(kSource);
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(0), ScribbleColor::kRed);
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(1), ScribbleColor::kWhite);
}

TEST_F(SendModeTest, RouteStripShowsTheRoute) {
  EnterSendMode(kSource);
  EXPECT_EQ(GetBottomLine(0), "0.00dB");
  EXPECT_NEAR(xtouch_ext_->GetFader(0), kFader0dB, 1);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kCenterRing}));
  EXPECT_EQ(GetLight(StripButton::kMute, 0), Light::kOff);

  source_to_bus_->volume = 0.5;
  source_to_bus_->pan = -1.0;
  source_to_bus_->mute = true;
  RunUntilShown();
  EXPECT_EQ(GetBottomLine(0), "-6.02dB");
  EXPECT_LT(xtouch_ext_->GetFader(0), kFader0dB);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kLeftRing}));
  EXPECT_EQ(GetLight(StripButton::kMute, 0), Light::kOn);
}

TEST_F(SendModeTest, EmptyRouteStripIsBlank) {
  EnterSendMode(1);  // T2, with one send.
  EXPECT_EQ(GetName(1), "");
  EXPECT_EQ(GetBottomLine(1), "");
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(1), ScribbleColor::kBlack);
  EXPECT_EQ(xtouch_ext_->GetFader(1), 0);
  EXPECT_EQ(xtouch_ext_->GetRing(1), Ring{});

  Tap(*xtouch_ext_, StripButton::kMute, 1);
  EXPECT_EQ(GetLight(StripButton::kMute, 1), Light::kOff);
}

TEST_F(SendModeTest, MovingARouteFaderSetsTheVolume) {
  EnterSendMode(kSource);
  MoveFader(*xtouch_ext_, 0, 0);
  EXPECT_EQ(source_to_bus_->volume, 0.0);
  EXPECT_EQ(xtouch_ext_->GetFader(0), 0);
}

TEST_F(SendModeTest, TurningARoutePotSetsThePan) {
  EnterSendMode(kSource);
  xtouch_ext_->TurnPot(0, 63);
  RunUntilShown();
  EXPECT_DOUBLE_EQ(source_to_bus_->pan, 1.0);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kRightRing}));
}

TEST_F(SendModeTest, RoutePotButtonCentersThePan) {
  source_to_bus_->pan = 0.5;
  EnterSendMode(kSource);
  Tap(*xtouch_ext_, StripButton::kPotButton, 0);
  EXPECT_EQ(source_to_bus_->pan, 0.0);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kCenterRing}));
}

TEST_F(SendModeTest, RouteMuteTogglesTheMute) {
  EnterSendMode(kSource);
  Tap(*xtouch_ext_, StripButton::kMute, 0);
  EXPECT_TRUE(source_to_bus_->mute);
  EXPECT_EQ(GetLight(StripButton::kMute, 0), Light::kOn);

  Tap(*xtouch_ext_, StripButton::kMute, 0);
  EXPECT_FALSE(source_to_bus_->mute);
  EXPECT_EQ(GetLight(StripButton::kMute, 0), Light::kOff);
}

//------------------------------------------------------------------------------
// Paging
//
// Bank left and right page through the routes by the 15 route strips, and
// Channel left and right by one, until an end is shown.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, BankPagesUntilAnEndIsShown) {
  AddSourceSends(15);  // T7-T21, after T4 and T5: 17 sends.
  EnterSendMode(kSource);
  EXPECT_EQ(GetName(0), "T4");
  EXPECT_EQ(GetName(14), "T19");

  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T7");
  EXPECT_EQ(GetName(14), "T21");
  EXPECT_EQ(GetName(kInfoStrip), "T1");
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T7");

  Tap(xtouch_, Button::kBankLeft);
  EXPECT_EQ(GetName(0), "T4");
  Tap(xtouch_, Button::kBankLeft);
  EXPECT_EQ(GetName(0), "T4");
}

TEST_F(SendModeTest, ChannelMovesOneRouteUntilAnEndIsShown) {
  AddSourceSends(15);
  EnterSendMode(kSource);
  Tap(xtouch_, Button::kChannelRight);
  EXPECT_EQ(GetName(0), "T5");
  Tap(xtouch_, Button::kChannelRight);
  Tap(xtouch_, Button::kChannelRight);
  EXPECT_EQ(GetName(0), "T7");
  EXPECT_EQ(GetName(14), "T21");

  Tap(xtouch_, Button::kChannelLeft);
  EXPECT_EQ(GetName(0), "T5");
  Tap(xtouch_, Button::kChannelLeft);
  Tap(xtouch_, Button::kChannelLeft);
  EXPECT_EQ(GetName(0), "T4");
}

//------------------------------------------------------------------------------
// Info strip
//
// The Info strip shows the track whose routes are shown, and controls it, as a
// track strip does.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, InfoStripShowsTheTrack) {
  FakeTrack* source = tracks_[kSource];
  source->color = 0x010000FF;  // Red, as 0x01BBGGRR.
  source->volume = 0.5;
  source->pan = -1.0;
  source->mute = true;
  source->solo = true;
  source->peak = {0.1, 0.5};  // -20dB and -6dB.
  EnterSendMode(kSource);
  const int info = GetXTouchStrip(kInfoStrip);
  EXPECT_EQ(xtouch_.GetScribbleColor(info), ScribbleColor::kRed);
  EXPECT_GT(xtouch_.GetFader(info), 0);
  EXPECT_LT(xtouch_.GetFader(info), kFader0dB);
  EXPECT_EQ(xtouch_.GetRing(info), (Ring{kPanRing, kLeftRing}));
  EXPECT_EQ(xtouch_.GetMeter(info), 0xB);
  EXPECT_EQ(GetLight(StripButton::kMute, kInfoStrip), Light::kOn);
  EXPECT_EQ(GetLight(StripButton::kSolo, kInfoStrip), Light::kOn);
  EXPECT_EQ(GetLight(StripButton::kRec, kInfoStrip), Light::kOff);
}

TEST_F(SendModeTest, InfoStripControlsTheTrack) {
  FakeTrack* source = tracks_[kSource];
  EnterSendMode(kSource);
  const int info = GetXTouchStrip(kInfoStrip);
  MoveFader(xtouch_, info, 0);
  EXPECT_EQ(source->volume, 0.0);

  xtouch_.TurnPot(info, 63);
  RunUntilShown();
  EXPECT_DOUBLE_EQ(source->pan, 1.0);
  Tap(xtouch_, StripButton::kPotButton, info);
  EXPECT_EQ(source->pan, 0.0);

  Tap(xtouch_, StripButton::kMute, info);
  Tap(xtouch_, StripButton::kSolo, info);
  Tap(xtouch_, StripButton::kRec, info);
  EXPECT_TRUE(source->mute);
  EXPECT_TRUE(source->solo);
  EXPECT_TRUE(source->rec_arm);
  EXPECT_EQ(GetName(kInfoStrip), "T1");
  EXPECT_EQ(GetName(0), "T4");
}

//------------------------------------------------------------------------------
// Undo
//
// REAPER adds no undo point for a route's volume or pan changed from a control
// surface, so the surface adds one, once the changes have stopped.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, RouteMovesAddOneUndoPointOnceTheyStop) {
  EnterSendMode(kSource);
  MoveFader(*xtouch_ext_, 0, 0);
  MoveFader(*xtouch_ext_, 1, 0);
  EXPECT_THAT(reaper_.GetProject().GetUndoPoints(), IsEmpty());

  WaitForUndo();
  EXPECT_THAT(
      reaper_.GetProject().GetUndoPoints(),
      ElementsAre(Field(&FakeUndoPoint::name, "JPR: Adjust send volume")));
}

TEST_F(SendModeTest, EachKindOfRouteMoveHasItsOwnUndoPoint) {
  EnterSendMode(kSource);
  MoveFader(*xtouch_ext_, 0, 0);
  xtouch_ext_->TurnPot(0, 63);
  RunUntilShown();
  WaitForUndo();
  EXPECT_THAT(
      reaper_.GetProject().GetUndoPoints(),
      ElementsAre(Field(&FakeUndoPoint::name, "JPR: Adjust send volume"),
                  Field(&FakeUndoPoint::name, "JPR: Adjust send pan")));
}

TEST_F(SendModeTest, ReceiveMovesAreNamedForReceives) {
  EnterSendMode(kOnlyReceives);
  MoveFader(*xtouch_ext_, 0, 0);
  WaitForUndo();
  EXPECT_THAT(
      reaper_.GetProject().GetUndoPoints(),
      ElementsAre(Field(&FakeUndoPoint::name, "JPR: Adjust receive volume")));
}

//------------------------------------------------------------------------------
// Sends and receives
//
// A track's sends are shown, unless it only has receives, and tapping Send
// switches between them.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, TappingSendSwitchesBetweenSendsAndReceives) {
  EnterSendMode(kBus);
  EXPECT_EQ(GetName(0), "T5");
  EXPECT_EQ(GetName(1), "");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");

  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetName(1), "T2");
  EXPECT_EQ(GetName(2), "T3");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Recv");

  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(GetName(0), "T5");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
}

TEST_F(SendModeTest, TrackWithOnlyReceivesShowsThem) {
  EnterSendMode(kOnlyReceives);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetName(1), "T4");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Recv");

  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Recv");
}

//------------------------------------------------------------------------------
// Walking the routes
//
// Select on a route strip goes to the track at the route's other end, and
// shows its routes of the other kind, scrolled so the route back is shown.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, SelectGoesToTheOtherEndOfTheRoute) {
  EnterSendMode(kSource);
  Tap(*xtouch_ext_, StripButton::kMute, 0);

  TapSelect(0);
  EXPECT_EQ(GetName(kInfoStrip), "T4");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Recv");
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetLight(StripButton::kMute, 0), Light::kOn);

  TapSelect(2);
  EXPECT_EQ(GetName(kInfoStrip), "T3");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
  EXPECT_EQ(GetName(0), "T4");
}

//------------------------------------------------------------------------------
// Following REAPER
//
// Send/Receive mode shows the track last touched in REAPER, as muting it with
// its button, or clicking it in the track panel, does.
//------------------------------------------------------------------------------

TEST_F(SendModeTest, FollowsATrackMutedInReaper) {
  EnterSendMode(kSource);
  notifier_.ClickMute(tracks_[1]);
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "T2");
  EXPECT_EQ(GetName(0), "T4");
}

TEST_F(SendModeTest, FollowsATrackClickedInReaper) {
  EnterSendMode(kSource);
  notifier_.ClickTrack(tracks_[kOnlyReceives]);
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "T5");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Recv");
  EXPECT_EQ(GetName(0), "T1");

  notifier_.CtrlClickTrack(tracks_[kBus]);
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "T4");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
  EXPECT_EQ(GetName(0), "T5");
}

TEST_F(SendModeTest, DeletedTrackShowsNothingUntilAnotherIsTouched) {
  EnterSendMode(kSource);
  reaper_.GetProject().DeleteTrack(tracks_[kSource]);
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "");
  EXPECT_EQ(xtouch_.GetScribbleColor(GetXTouchStrip(kInfoStrip)),
            ScribbleColor::kBlack);
  EXPECT_EQ(GetName(0), "");
  EXPECT_EQ(xtouch_.GetLight(Button::kAssignSend), Light::kBlinking);

  notifier_.ClickTrack(tracks_[kBus]);
  RunUntilShown();
  EXPECT_EQ(GetName(kInfoStrip), "T4");
  EXPECT_EQ(GetBottomLine(kInfoStrip), "Send");
  EXPECT_EQ(GetName(0), "T5");
}

//------------------------------------------------------------------------------
// The X-Touch alone
//
// Its first 7 strips show the routes, and its last is the Info strip.
//------------------------------------------------------------------------------

// T1 sends to T2-T9, one more than the route strips.
class SendModeAloneTest : public DefaultConfigTest {
 protected:
  SendModeAloneTest() : DefaultConfigTest(/*extender=*/false) {
    std::vector<FakeTrack*> tracks = AddTracks(9);
    for (int i = 1; i < 9; ++i) {
      reaper_.GetProject().AddSend(tracks[0], tracks[i]);
    }
    AddSurface();
  }
};

TEST_F(SendModeAloneTest, SevenStripsShowTheRoutes) {
  EnterSendMode(0);
  EXPECT_EQ(GetName(0), "T2");
  EXPECT_EQ(GetName(6), "T8");
  EXPECT_EQ(GetName(7), "T1");
  EXPECT_EQ(GetBottomLine(7), "Send");

  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T3");
  EXPECT_EQ(GetName(6), "T9");
}

}  // namespace
}  // namespace jpr
