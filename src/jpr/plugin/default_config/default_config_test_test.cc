// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_config/default_config_test.h"

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
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
