// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_config/default_config_test.h"

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
using Led = FakeXTouch::Led;
using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

TEST_F(DefaultConfigTest, TheExtenderIsLeftOfTheXTouch) {
  AddTracks(16);
  AddSurface();
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetName(8), "T9");
}

// A folder on the fourth strip, to press its select button.
class PressTest : public DefaultConfigTest {
 protected:
  static constexpr int kFolderStrip = 3;

  PressTest() {
    folder_ = AddTracks(4)[kFolderStrip];
    AddTracks(2, folder_);
    AddSurface();
  }

  FakeTrack* folder_ = nullptr;
};

TEST_F(PressTest, TapPressesOnce) {
  Tap(*xtouch_ext_, StripButton::kSelect, kFolderStrip);
  EXPECT_TRUE(folder_->selected);
  EXPECT_EQ(GetName(0), "T1");
}

TEST_F(PressTest, DoublePressPressesTwice) {
  DoublePress(*xtouch_ext_, StripButton::kSelect, kFolderStrip);
  EXPECT_EQ(GetName(0), "T4.1");
}

TEST_F(PressTest, LongPressHolds) {
  DoublePress(*xtouch_ext_, StripButton::kSelect, kFolderStrip);
  LongPress(xtouch_, Button::kGlobal);
  EXPECT_EQ(GetName(0), "T1");
}

// Removing the surface, as REAPER does at exit, or when the user removes it,
// leaves the hardware as it starts: off, at zero, blank, and black.
TEST_F(DefaultConfigTest, RemovingTheSurfaceClearsTheHardware) {
  for (FakeTrack* track : AddTracks(16)) {
    track->color = 0x010000FF;  // Red, as 0x01BBGGRR.
    track->mute = true;
    track->solo = true;
    track->rec_arm = true;
    track->selected = true;
    track->peak = {0.5, 0.5};
  }
  reaper_.SetToggleState(1007, 1);  // Transport: Play.
  reaper_.GetProject().SetCursorPosition(3.5);
  AddSurface();
  ASSERT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOn);
  ASSERT_EQ(GetLight(StripButton::kMute, 0), Light::kOn);

  RemoveSurface();
  for (FakeXTouch* xtouch : {&*xtouch_ext_, &xtouch_}) {
    for (int strip = 0; strip < 8; ++strip) {
      for (StripButton button : {StripButton::kRec, StripButton::kSolo,
                                 StripButton::kMute, StripButton::kSelect}) {
        EXPECT_EQ(xtouch->GetLight(button, strip), Light::kOff) << strip;
      }
      EXPECT_EQ(xtouch->GetFader(strip), 0) << strip;
      EXPECT_EQ(xtouch->GetRing(strip), FakeXTouch::Ring{}) << strip;
      EXPECT_EQ(xtouch->GetMeter(strip), 0) << strip;
      EXPECT_EQ(xtouch->GetScribble(strip, 0), "       ") << strip;
      EXPECT_EQ(xtouch->GetScribble(strip, 1), "       ") << strip;
      EXPECT_EQ(xtouch->GetScribbleColor(strip),
                FakeXTouch::ScribbleColor::kBlack)
          << strip;
    }
  }
  for (int button = 0; button <= static_cast<int>(Button::kRight); ++button) {
    EXPECT_EQ(xtouch_.GetLight(static_cast<Button>(button)), Light::kOff)
        << button;
  }
  for (Led led : {Led::kSmpte, Led::kBeats, Led::kSolo}) {
    EXPECT_EQ(xtouch_.GetLight(led), Light::kOff);
  }
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), 0);
  EXPECT_EQ(xtouch_.GetTimecode(), "          ");
}

class XTouchAloneTest : public DefaultConfigTest {
 protected:
  XTouchAloneTest() : DefaultConfigTest(/*extender=*/false) {}
};

TEST_F(XTouchAloneTest, TheXTouchShowsTheFirstTracks) {
  AddTracks(1);
  AddSurface();
  EXPECT_FALSE(xtouch_ext_.has_value());
  EXPECT_EQ(GetName(0), "T1");
}

}  // namespace
}  // namespace jpr
