// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <string>
#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/default_config/default_config_test.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::TestParamInfo;
using ::testing::Values;
using ::testing::WithParamInterface;

using Light = FakeXTouch::Light;
using Ring = FakeXTouch::Ring;
using ScribbleColor = FakeXTouch::ScribbleColor;
using StripButton = FakeXTouch::StripButton;

// The ring mode the surface shows a folder's pan with: boost/cut, with the far
// left and right lights lit.
constexpr int kFolderRing = 5;

// Long enough for a meter to fall from the top, a level a run, unless it is
// sent again.
constexpr absl::Duration kMeterFallTime = absl::Milliseconds(500);

// Ten tracks: eight on the extender, and two on the X-Touch, whose other six
// strips are empty. T3 is a folder, with a track in it.
class TrackStripTest : public DefaultConfigTest {
 protected:
  static constexpr int kTrackCount = 10;
  static constexpr int kFolder = 2;      // T3, on the extender's third strip.
  static constexpr int kEmptyStrip = 2;  // The X-Touch's first empty strip.

  TrackStripTest() {
    tracks_ = AddTracks(kTrackCount);
    AddTracks(1, tracks_[kFolder]);
    AddSurface();
  }

  FakeTrack* GetMaster() { return reaper_.GetProject().GetMasterTrack(); }

  std::vector<FakeTrack*> tracks_;
};

//------------------------------------------------------------------------------
// Faders
//
// The extender's third fader has no touch on JPRSurf's hardware, so it moves
// none.
//------------------------------------------------------------------------------

TEST_F(TrackStripTest, FaderShowsTheVolume) {
  EXPECT_NEAR(xtouch_ext_->GetFader(0), kFader0dB, 1);
  EXPECT_NEAR(xtouch_.GetFader(1), kFader0dB, 1);

  tracks_[0]->volume = 0.0;
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetFader(0), 0);
}

TEST_F(TrackStripTest, MovingAFaderSetsTheVolume) {
  MoveFader(xtouch_, 1, 0);
  EXPECT_EQ(tracks_[9]->volume, 0.0);
  EXPECT_EQ(xtouch_.GetFader(1), 0);

  MoveFader(*xtouch_ext_, 0, kFader0dB);
  EXPECT_NEAR(tracks_[0]->volume, 1.0, 1e-3);
  EXPECT_NEAR(xtouch_ext_->GetFader(0), kFader0dB, 1);
}

TEST_F(TrackStripTest, EmptyStripFaderStaysDown) {
  EXPECT_EQ(xtouch_.GetFader(kEmptyStrip), 0);

  MoveFader(xtouch_, kEmptyStrip, kFader0dB);
  EXPECT_EQ(xtouch_.GetFader(kEmptyStrip), 0);
}

TEST_F(TrackStripTest, MasterFaderShowsTheMasterVolume) {
  EXPECT_NEAR(xtouch_.GetFader(FakeXTouch::kMasterFader), kFader0dB, 1);

  GetMaster()->volume = 0.0;
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), 0);
}

TEST_F(TrackStripTest, MovingTheMasterFaderSetsTheMasterVolume) {
  MoveFader(xtouch_, FakeXTouch::kMasterFader, 0);
  EXPECT_EQ(GetMaster()->volume, 0.0);
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), 0);
}

//------------------------------------------------------------------------------
// Pots
//------------------------------------------------------------------------------

TEST_F(TrackStripTest, RingShowsThePan) {
  tracks_[0]->pan = -1.0;
  tracks_[9]->pan = 1.0;
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kLeftRing}));
  EXPECT_EQ(xtouch_ext_->GetRing(1), (Ring{kPanRing, kCenterRing}));
  EXPECT_EQ(xtouch_.GetRing(1), (Ring{kPanRing, kRightRing}));
}

TEST_F(TrackStripTest, FolderRingHasItsEndsLit) {
  EXPECT_EQ(xtouch_ext_->GetRing(kFolder), (Ring{kFolderRing, kCenterRing}));
}

TEST_F(TrackStripTest, EmptyStripRingIsOff) {
  EXPECT_EQ(xtouch_.GetRing(kEmptyStrip), Ring{});
}

TEST_F(TrackStripTest, TurningAPotSetsThePan) {
  xtouch_ext_->TurnPot(0, 63);
  RunUntilShown();
  EXPECT_DOUBLE_EQ(tracks_[0]->pan, 1.0);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kRightRing}));

  xtouch_ext_->TurnPot(0, -21);
  RunUntilShown();
  EXPECT_NEAR(tracks_[0]->pan, 2.0 / 3.0, 1e-6);
}

TEST_F(TrackStripTest, TurningAPotStopsAtEachEnd) {
  xtouch_.TurnPot(0, -63);
  RunUntilShown();
  xtouch_.TurnPot(0, -63);
  RunUntilShown();
  EXPECT_DOUBLE_EQ(tracks_[8]->pan, -1.0);
  EXPECT_EQ(xtouch_.GetRing(0), (Ring{kPanRing, kLeftRing}));
}

TEST_F(TrackStripTest, PotButtonCentersThePan) {
  tracks_[0]->pan = 0.5;
  RunUntilShown();
  Tap(*xtouch_ext_, StripButton::kPotButton, 0);
  EXPECT_EQ(tracks_[0]->pan, 0.0);
  EXPECT_EQ(xtouch_ext_->GetRing(0), (Ring{kPanRing, kCenterRing}));
}

//------------------------------------------------------------------------------
// Meters
//------------------------------------------------------------------------------

TEST_F(TrackStripTest, MeterShowsTheLouderChannel) {
  tracks_[0]->peak = {0.1, 0.5};  // -20dB and -6dB.
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetMeter(0), 0xB);
}

TEST_F(TrackStripTest, MeterStaysUpWhileThePeakDoes) {
  tracks_[0]->peak = {0.5, 0.5};
  surface_->RunFor(kMeterFallTime);
  EXPECT_EQ(xtouch_ext_->GetMeter(0), 0xB);

  tracks_[0]->peak = {};
  surface_->RunFor(kMeterFallTime);
  EXPECT_EQ(xtouch_ext_->GetMeter(0), 0);
}

//------------------------------------------------------------------------------
// Scribble strips
//------------------------------------------------------------------------------

TEST_F(TrackStripTest, ScribbleShowsTheNameAndVolume) {
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 0), "T1     ");
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 1), "0.00dB ");

  tracks_[0]->name = "Kick";
  tracks_[0]->volume = 0.5;
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 0), "Kick   ");
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 1), "-6.02dB");
}

// REAPER writes some volumes in 8 characters, which the strip fits in 7 by
// dropping a decimal digit.
TEST_F(TrackStripTest, ScribbleFitsLongVolumes) {
  tracks_[0]->volume = 0.000000631;  // "-124.0dB".
  tracks_[1]->volume = 0.3163;       // "-10.00dB".
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 1), "-124dB ");
  EXPECT_EQ(xtouch_ext_->GetScribble(1, 1), "-10.0dB");
}

// The bottom of a fader's travel, below its -60dB mark, reaches volumes under
// -100dB.
TEST_F(TrackStripTest, ScribbleFitsAVolumeAtTheBottomOfTheFader) {
  MoveFader(*xtouch_ext_, 0, 1);
  RunUntilShown();
  EXPECT_LT(tracks_[0]->volume, 0.00001);                // -100dB.
  EXPECT_EQ(xtouch_ext_->GetScribble(0, 1), "-114dB ");  // "-114.5dB".
}

TEST_F(TrackStripTest, ScribbleShowsTheTrackColor) {
  tracks_[0]->color = 0x010000FF;  // As 0x01BBGGRR.
  tracks_[1]->color = 0x0100FF00;
  tracks_[9]->color = 0x01FF0000;
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(0), ScribbleColor::kRed);
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(1), ScribbleColor::kGreen);
  EXPECT_EQ(xtouch_.GetScribbleColor(1), ScribbleColor::kBlue);
}

TEST_F(TrackStripTest, ScribbleIsWhiteForATrackWithNoColor) {
  EXPECT_EQ(xtouch_ext_->GetScribbleColor(3), ScribbleColor::kWhite);
}

TEST_F(TrackStripTest, EmptyStripScribbleIsBlank) {
  EXPECT_EQ(xtouch_.GetScribble(kEmptyStrip, 0), "       ");
  EXPECT_EQ(xtouch_.GetScribble(kEmptyStrip, 1), "       ");
  EXPECT_EQ(xtouch_.GetScribbleColor(kEmptyStrip), ScribbleColor::kBlack);
}

TEST_F(TrackStripTest, ScribbleShowsATrackAddedToAnEmptyStrip) {
  AddTracks(1);
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetScribble(kEmptyStrip, 0), "T11    ");
  EXPECT_EQ(xtouch_.GetScribble(kEmptyStrip, 1), "0.00dB ");
}

TEST_F(TrackStripTest, ScribbleIsBlankForADeletedTrack) {
  reaper_.GetProject().DeleteTrack(tracks_[9]);
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetScribble(1, 0), "       ");
  EXPECT_EQ(xtouch_.GetScribble(1, 1), "       ");
}

//------------------------------------------------------------------------------
// Mute, solo, and record arm
//------------------------------------------------------------------------------

// A strip button that turns a value of the track on and off.
struct ToggleButton {
  StripButton button;
  bool FakeTrack::*value;
  std::string undo_name;
  std::string test_name;
};

class ToggleButtonTest : public TrackStripTest,
                         public WithParamInterface<ToggleButton> {
 protected:
  bool& GetValue(FakeTrack* track) const { return track->*GetParam().value; }
};

INSTANTIATE_TEST_SUITE_P(
    TrackStrip, ToggleButtonTest,
    Values(ToggleButton{StripButton::kMute, &FakeTrack::mute, "JPR:Toggle Mute",
                        "Mute"},
           ToggleButton{StripButton::kSolo, &FakeTrack::solo, "JPR:Toggle Solo",
                        "Solo"},
           ToggleButton{StripButton::kRec, &FakeTrack::rec_arm,
                        "JPR:Toggle Record Arm", "RecArm"}),
    [](const TestParamInfo<ToggleButton>& info) {
      return info.param.test_name;
    });

TEST_P(ToggleButtonTest, PressingTogglesTheTrack) {
  Tap(xtouch_, GetParam().button, 0);
  EXPECT_TRUE(GetValue(tracks_[8]));
  EXPECT_EQ(xtouch_.GetLight(GetParam().button, 0), Light::kOn);
  EXPECT_THAT(reaper_.GetProject().GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, GetParam().undo_name)));

  Tap(xtouch_, GetParam().button, 0);
  EXPECT_FALSE(GetValue(tracks_[8]));
  EXPECT_EQ(xtouch_.GetLight(GetParam().button, 0), Light::kOff);
}

TEST_P(ToggleButtonTest, LightShowsTheTrack) {
  GetValue(tracks_[0]) = true;
  RunUntilShown();
  EXPECT_EQ(xtouch_ext_->GetLight(GetParam().button, 0), Light::kOn);
  EXPECT_EQ(xtouch_ext_->GetLight(GetParam().button, 1), Light::kOff);
  EXPECT_THAT(reaper_.GetProject().GetUndoPoints(), IsEmpty());
}

TEST_P(ToggleButtonTest, EmptyStripPressDoesNothing) {
  Tap(xtouch_, GetParam().button, kEmptyStrip);
  EXPECT_EQ(xtouch_.GetLight(GetParam().button, kEmptyStrip), Light::kOff);
  EXPECT_THAT(reaper_.GetProject().GetUndoPoints(), IsEmpty());
}

// Holding the button on T7, on the extender, and pressing it on T10, on the
// X-Touch, sets the tracks from one to the other to T7's new value.
TEST_P(ToggleButtonTest, HoldingAndPressingAnotherSetsTheRange) {
  xtouch_ext_->Press(GetParam().button, 6);
  surface_->Run();
  Tap(xtouch_, GetParam().button, 1);
  xtouch_ext_->Release(GetParam().button, 6);
  surface_->Run();

  for (int i = 0; i < kTrackCount; ++i) {
    EXPECT_EQ(GetValue(tracks_[i]), i >= 6) << tracks_[i]->name;
  }
  EXPECT_EQ(xtouch_ext_->GetLight(GetParam().button, 7), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(GetParam().button, 0), Light::kOn);
  EXPECT_THAT(reaper_.GetProject().GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, GetParam().undo_name),
                          Field(&FakeUndoPoint::name, GetParam().undo_name)));
}

}  // namespace
}  // namespace jpr
