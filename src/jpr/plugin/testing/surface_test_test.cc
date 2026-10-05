// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/testing/surface_test.h"

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;

using Button = FakeXTouch::Button;
using StripButton = FakeXTouch::StripButton;

class SurfaceTestTest : public SurfaceTest {
 protected:
  // Returns the names of the current project's tracks, in order.
  std::vector<std::string> GetTrackNames() {
    FakeProject& project = reaper_.GetProject();
    std::vector<std::string> names;
    for (int i = 0; i < project.GetTrackCount(); ++i) {
      names.push_back(project.GetTrack(i)->name);
    }
    return names;
  }
};

TEST_F(SurfaceTestTest, ErrorsLoggedAreKeptToFailTheTest) {
  LOG(ERROR) << "Expected";
  EXPECT_THAT(log_error_guard_.TakeMessages(),
              ElementsAre(Field(&gb::LogRecorder::Message::text, "Expected")));
}

TEST_F(SurfaceTestTest, AddTracksNamesTracksForWhereTheyAre) {
  std::vector<FakeTrack*> top = AddTracks(3);
  std::vector<FakeTrack*> children = AddTracks(2, top[1]);
  AddTracks(1, children[0]);
  AddTracks(1, top[1]);
  AddTracks(1);

  EXPECT_THAT(GetTrackNames(), ElementsAre("T1", "T2", "T2.1", "T2.1.1", "T2.2",
                                           "T2.3", "T3", "T4"));
  FakeProject& project = reaper_.GetProject();
  EXPECT_EQ(project.GetParentTrack(children[1]), top[1]);
  EXPECT_EQ(project.GetParentTrack(top[2]), nullptr);
}

TEST_F(SurfaceTestTest, TheExtenderIsLeftOfTheXTouch) {
  AddTracks(16);
  AddSurface();
  EXPECT_EQ(GetName(0), "T1");
  EXPECT_EQ(GetName(8), "T9");
}

// A folder on the fourth strip, to press its select button.
class PressTest : public SurfaceTestTest {
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

class XTouchAloneTest : public SurfaceTest {
 protected:
  XTouchAloneTest() : SurfaceTest(/*extender=*/false) {}
};

TEST_F(XTouchAloneTest, TheXTouchShowsTheFirstTracks) {
  AddTracks(1);
  AddSurface();
  EXPECT_FALSE(xtouch_ext_.has_value());
  EXPECT_EQ(GetName(0), "T1");
}

}  // namespace
}  // namespace jpr
