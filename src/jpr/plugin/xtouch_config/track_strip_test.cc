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
#include "jpr/plugin/xtouch_config/xtouch_config_test.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::TestParamInfo;
using ::testing::Values;
using ::testing::WithParamInterface;

using Light = FakeXTouch::Light;
using Ring = FakeXTouch::Ring;
using StripButton = FakeXTouch::StripButton;

// The ring mode the surface shows a folder's pan with: boost/cut, with the far
// left and right lights lit.
constexpr int kFolderRing = 5;

// Long enough for a meter to fall from the top, a level a run, unless it is
// sent again.
constexpr absl::Duration kMeterFallTime = absl::Milliseconds(500);

// Ten tracks: eight on the extender, and two on the X-Touch, whose other six
// strips are empty. T3 is a folder, with a track in it.
class TrackStripTest : public XTouchConfigTest {
 protected:
  static constexpr int kTrackCount = 10;
  static constexpr int kFolder = 2;      // T3, on the extender's third strip.
  static constexpr int kEmptyStrip = 2;  // The X-Touch's first empty strip.

  TrackStripTest() {
    tracks_ = AddTracks(kTrackCount);
    AddTracks(1, tracks_[kFolder]);
    AddSurface();
  }

  std::vector<FakeTrack*> tracks_;
};

//------------------------------------------------------------------------------
// Pots
//------------------------------------------------------------------------------

TEST_F(TrackStripTest, FolderRingHasItsEndsLit) {
  EXPECT_EQ(xtouch_ext_->GetRing(kFolder), (Ring{kFolderRing, kCenterRing}));
}

TEST_F(TrackStripTest, EmptyStripRingIsOff) {
  EXPECT_EQ(xtouch_.GetRing(kEmptyStrip), Ring{});
}

//------------------------------------------------------------------------------
// Meters
//------------------------------------------------------------------------------

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

// Holding the button on T7, on the extender, and pressing it on T10, on the
// X-Touch, sets the tracks from one to the other to T7's new value.
TEST_P(ToggleButtonTest, HoldingAndPressingAnotherSetsTheRange) {
  PressRange(GetParam().button, 6, 9);

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
