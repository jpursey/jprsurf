// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <vector>

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/xtouch_config/xtouch_config_test.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

// 32 top level tracks: two banks past the 16 strips. T2 is a folder two levels
// deep (T2.1, with T2.1.1, and T2.2), and T20 has 20 tracks, more than the
// strips. T1 sends to T20.19, for Send/Receive mode to go to.
class TrackListTest : public XTouchConfigTest {
 protected:
  static constexpr int kTopCount = 32;
  static constexpr int kFolder = 1;      // T2.
  static constexpr int kBigFolder = 19;  // T20.
  static constexpr int kBigFolderCount = 20;
  static constexpr int kSendTarget = 18;  // T20.19.

  explicit TrackListTest(bool extender = true) : XTouchConfigTest(extender) {
    top_ = AddTracks(kTopCount);
    AddTracks(1, AddTracks(2, top_[kFolder])[0]);
    big_ = AddTracks(kBigFolderCount, top_[kBigFolder]);
    reaper_.GetProject().AddSend(top_[0], big_[kSendTarget]);
    AddSurface();
  }

  std::vector<FakeTrack*> top_;
  std::vector<FakeTrack*> big_;  // T20's tracks.
};

//------------------------------------------------------------------------------
// Select
//------------------------------------------------------------------------------

TEST_F(TrackListTest, SelectLightShowsTheSelection) {
  top_[9]->selected = true;
  RunUntilShown();
  EXPECT_EQ(GetLight(StripButton::kSelect, 9), Light::kOn);
  EXPECT_EQ(GetLight(StripButton::kSelect, 8), Light::kOff);
}

TEST_F(TrackListTest, HoldingSelectSelectsTheTrack) {
  top_[5]->selected = true;

  Hold(*xtouch_ext_, StripButton::kSelect, 0);
  xtouch_ext_->Release(StripButton::kSelect, 0);
  surface_->Run();
  EXPECT_TRUE(top_[0]->selected);
  EXPECT_FALSE(top_[5]->selected);
}

// Holding select on T1, on the extender, and pressing it on T12, on the
// X-Touch, selects the tracks from one to the other.
TEST_F(TrackListTest, HoldingSelectAndPressingAnotherSelectsTheRange) {
  SelectRange(0, 11);

  for (int i = 0; i < kTopCount; ++i) {
    EXPECT_EQ(top_[i]->selected, i <= 11) << top_[i]->name;
  }
  EXPECT_EQ(GetLight(StripButton::kSelect, 11), Light::kOn);
}

//------------------------------------------------------------------------------
// Folders
//------------------------------------------------------------------------------

TEST_F(TrackListTest, GlobalIsLitInAFolder) {
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOff);
  DoublePressSelect(kFolder);
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOn);
}

TEST_F(TrackListTest, GlobalGoesUpALevel) {
  DoublePressSelect(kFolder);
  DoublePressSelect(0);

  Tap(xtouch_, Button::kGlobal);
  EXPECT_EQ(GetName(0), "T2.1");
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOn);

  Tap(xtouch_, Button::kGlobal);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOff);
}

//------------------------------------------------------------------------------
// Bank and Channel
//------------------------------------------------------------------------------

// A fader held through a bank touches the track it shows next.
TEST_F(TrackListTest, BankMovesAHeldFadersTouch) {
  xtouch_ext_->TouchFader(0);
  RunUntilShown();
  ASSERT_TRUE(IsTouched(top_[0], TrackTouchType::kVolume));

  Tap(xtouch_, Button::kBankRight);
  EXPECT_FALSE(IsTouched(top_[0], TrackTouchType::kVolume));
  EXPECT_TRUE(IsTouched(top_[8], TrackTouchType::kVolume));
}

//------------------------------------------------------------------------------
// The current track
//
// The track list shows the track last touched, in REAPER or by going across a
// route in Send/Receive mode. A track in the folder shown is scrolled to as
// little as it can be, and a track in another folder goes to that folder, with
// the track on the last strip if it can be.
//------------------------------------------------------------------------------

TEST_F(TrackListTest, ScrollsToATrackClickedInReaper) {
  notifier_.ClickTrack(top_[25]);
  RunUntilShown();
  EXPECT_EQ(GetName(0), "T11");
  EXPECT_EQ(GetName(15), "T26");

  notifier_.ClickTrack(top_[2]);
  RunUntilShown();
  EXPECT_EQ(GetName(0), "T3");
}

TEST_F(TrackListTest, GoesToTheFolderOfATrackClickedInReaper) {
  notifier_.ClickTrack(big_[kSendTarget]);
  RunUntilShown();
  EXPECT_EQ(GetName(0), "T20.4");
  EXPECT_EQ(GetName(15), "T20.19");
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOn);
}

TEST_F(TrackListTest, ShowsTheTrackSendModeWentToOnReturning) {
  EnterSendMode(0);
  TapSelect(0);  // Across T1's send, to T20.19.
  Tap(xtouch_, Button::kAssignTrack);
  EXPECT_EQ(GetName(0), "T20.4");
  EXPECT_EQ(GetName(15), "T20.19");
}

//------------------------------------------------------------------------------
// Losing the folder
//
// A deleted folder goes back to the top level.
//------------------------------------------------------------------------------

TEST_F(TrackListTest, DeletingTheFolderGoesToTheTop) {
  DoublePressSelect(kFolder);
  reaper_.GetProject().DeleteTrack(top_[kFolder]);
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOff);
}

//------------------------------------------------------------------------------
// The X-Touch alone
//------------------------------------------------------------------------------

class XTouchAloneTrackListTest : public TrackListTest {
 protected:
  XTouchAloneTrackListTest() : TrackListTest(/*extender=*/false) {}
};

TEST_F(XTouchAloneTrackListTest, BankMovesEightTracks) {
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T9");
  EXPECT_EQ(GetName(7), "T16");
}

TEST_F(XTouchAloneTrackListTest, TapSelectsTheTrack) {
  TapSelect(7);
  EXPECT_TRUE(top_[7]->selected);
  EXPECT_EQ(GetLight(StripButton::kSelect, 7), Light::kOn);
}

}  // namespace
}  // namespace jpr
