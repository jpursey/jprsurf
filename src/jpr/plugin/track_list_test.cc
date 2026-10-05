// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <vector>

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/testing/surface_test.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

// 32 top level tracks: two banks past the 16 strips. T2 is a folder two levels
// deep (T2.1, with T2.1.1, and T2.2), and T20 has 20 tracks, more than the
// strips.
class TrackListTest : public SurfaceTest {
 protected:
  static constexpr int kTopCount = 32;
  static constexpr int kFolder = 1;      // T2.
  static constexpr int kBigFolder = 19;  // T20.
  static constexpr int kBigFolderCount = 20;

  explicit TrackListTest(bool extender = true) : SurfaceTest(extender) {
    top_ = AddTracks(kTopCount);
    AddTracks(1, AddTracks(2, top_[kFolder])[0]);
    AddTracks(kBigFolderCount, top_[kBigFolder]);
    AddSurface();
  }

  Light GetSelectLight(int strip) {
    return GetXTouch(strip).GetLight(StripButton::kSelect,
                                     GetXTouchStrip(strip));
  }

  void TapSelect(int strip) {
    Tap(GetXTouch(strip), StripButton::kSelect, GetXTouchStrip(strip));
  }
  void DoublePressSelect(int strip) {
    DoublePress(GetXTouch(strip), StripButton::kSelect, GetXTouchStrip(strip));
  }

  // Goes into T20, which a bank right shows.
  void GoIntoBigFolder() {
    Tap(xtouch_, Button::kBankRight);
    DoublePressSelect(kBigFolder - 8);
  }

  std::vector<FakeTrack*> top_;
};

//------------------------------------------------------------------------------
// Select
//------------------------------------------------------------------------------

TEST_F(TrackListTest, TapSelectsOnlyTheTrack) {
  top_[0]->selected = true;
  top_[12]->selected = true;
  RunUntilShown();

  TapSelect(3);
  for (FakeTrack* track : top_) {
    EXPECT_EQ(track->selected, track == top_[3]) << track->name;
  }
  EXPECT_EQ(GetSelectLight(0), Light::kOff);
  EXPECT_EQ(GetSelectLight(3), Light::kOn);
  EXPECT_EQ(GetSelectLight(12), Light::kOff);
}

TEST_F(TrackListTest, TapUnselectsTheOnlySelectedTrack) {
  TapSelect(3);
  TapSelect(3);
  EXPECT_FALSE(top_[3]->selected);
  EXPECT_EQ(GetSelectLight(3), Light::kOff);
}

TEST_F(TrackListTest, SelectLightShowsTheSelection) {
  top_[9]->selected = true;
  RunUntilShown();
  EXPECT_EQ(GetSelectLight(9), Light::kOn);
  EXPECT_EQ(GetSelectLight(8), Light::kOff);
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
  Hold(*xtouch_ext_, StripButton::kSelect, 0);
  TapSelect(11);
  xtouch_ext_->Release(StripButton::kSelect, 0);
  surface_->Run();

  for (int i = 0; i < kTopCount; ++i) {
    EXPECT_EQ(top_[i]->selected, i <= 11) << top_[i]->name;
  }
  EXPECT_EQ(GetSelectLight(11), Light::kOn);
}

//------------------------------------------------------------------------------
// Folders
//------------------------------------------------------------------------------

TEST_F(TrackListTest, DoublePressGoesIntoAFolder) {
  DoublePressSelect(kFolder);
  EXPECT_EQ(GetName(0), "T2.1");
  EXPECT_EQ(GetName(1), "T2.2");
  EXPECT_EQ(GetName(2), "");
}

TEST_F(TrackListTest, DoublePressGoesIntoAFolderInAFolder) {
  DoublePressSelect(kFolder);
  DoublePressSelect(0);
  EXPECT_EQ(GetName(0), "T2.1.1");
  EXPECT_EQ(GetName(1), "");
}

TEST_F(TrackListTest, DoublePressOnATrackWithNoTracksInItStays) {
  DoublePressSelect(0);
  EXPECT_EQ(GetName(0), "T1");
}

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

TEST_F(TrackListTest, GlobalCentersTheFolderItLeaves) {
  GoIntoBigFolder();
  EXPECT_EQ(GetName(0), "T20.1");

  Tap(xtouch_, Button::kGlobal);
  EXPECT_EQ(GetName(8), "T20");
}

TEST_F(TrackListTest, HoldingGlobalGoesToTheTop) {
  DoublePressSelect(kFolder);
  DoublePressSelect(0);

  LongPress(xtouch_, Button::kGlobal);
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(xtouch_.GetLight(Button::kGlobal), Light::kOff);
}

//------------------------------------------------------------------------------
// Bank and Channel
//------------------------------------------------------------------------------

TEST_F(TrackListTest, BankMovesEightTracksUntilAnEndIsShown) {
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T9");
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T17");
  EXPECT_EQ(GetName(15), "T32");
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T17");

  Tap(xtouch_, Button::kBankLeft);
  EXPECT_EQ(GetName(0), "T9");
  Tap(xtouch_, Button::kBankLeft);
  EXPECT_EQ(GetName(0), "T1");
  Tap(xtouch_, Button::kBankLeft);
  EXPECT_EQ(GetName(0), "T1");
}

TEST_F(TrackListTest, ChannelMovesOneTrackUntilAnEndIsShown) {
  Tap(xtouch_, Button::kChannelRight);
  EXPECT_EQ(GetName(0), "T2");
  Tap(xtouch_, Button::kChannelLeft);
  EXPECT_EQ(GetName(0), "T1");
  Tap(xtouch_, Button::kChannelLeft);
  EXPECT_EQ(GetName(0), "T1");
}

TEST_F(TrackListTest, BankInAFolderStopsAtItsLastTrack) {
  GoIntoBigFolder();
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T20.5");
  EXPECT_EQ(GetName(15), "T20.20");
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(GetName(0), "T20.5");
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
  EXPECT_EQ(GetSelectLight(7), Light::kOn);
}

}  // namespace
}  // namespace jpr
