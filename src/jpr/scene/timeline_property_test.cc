// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/timeline_property.h"

#include <optional>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/timeline.h"
#include "jpr/scene/scene.h"

namespace jpr {
namespace {

using Source = TimelinePositionProperty::Source;

// The ruler starts in Measure.Beats, with no secondary mode (see
// AddReaperActions()). Each test brings a property up to date as the scene
// does, with UpdateState().
class TimelinePropertyTest : public ::testing::Test {
 protected:
  TimelinePropertyTest() {
    AddReaperActions(&reaper_);
    project_.SetPlayPosition(2.0);
    project_.SetCursorPosition(1.0);
  }

  // Returns the position the source reads now.
  double ReadPosition(Source source) {
    TimelinePositionProperty property(&scene_, "user:position", source);
    property.UpdateState();
    return property.GetTimelinePosition().GetValue();
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  Scene scene_{"Scene"};
  bool changed_ = false;
};

TEST_F(TimelinePropertyTest, PositionsReadEachSource) {
  EXPECT_EQ(ReadPosition(Source::kCurrent), 1.0);
  EXPECT_EQ(ReadPosition(Source::kPlayback), 2.0);
  EXPECT_EQ(ReadPosition(Source::kEdit), 1.0);

  // The current position is the playback position while playing or paused.
  project_.SetPlayState(1);
  EXPECT_EQ(ReadPosition(Source::kCurrent), 2.0);
  EXPECT_EQ(ReadPosition(Source::kEdit), 1.0);
}

TEST_F(TimelinePropertyTest, PositionsNotifyWhenThePositionOrRulerChanges) {
  // Watching it brings it up to date, which is a change.
  TimelinePositionProperty property(&scene_, "user:position", Source::kEdit);
  property.RegisterFlag(&changed_);
  EXPECT_TRUE(changed_);

  changed_ = false;
  property.UpdateState();
  EXPECT_FALSE(changed_);

  project_.SetCursorPosition(3.0);
  property.UpdateState();
  EXPECT_TRUE(changed_);
  EXPECT_EQ(property.GetTimelinePosition().GetValue(), 3.0);

  changed_ = false;
  SelectRulerMode(&reaper_, 40365);  // Minutes:Seconds.
  property.UpdateState();
  EXPECT_TRUE(changed_);
  property.UnregisterFlag(&changed_);
}

TEST_F(TimelinePropertyTest, RulerModeIsEachMode) {
  RulerModeProperty property(&scene_, "user:ruler");
  EXPECT_EQ(property.GetMaxValue(), 3);
  struct Mode {
    int command;
    int value;
    const char* text;
  };
  for (const Mode& mode :
       {Mode{40367, 0, "Beats"}, Mode{40365, 1, "Time"},
        Mode{40370, 2, "Frames"}, Mode{40369, 3, "Samples"}}) {
    SelectRulerMode(&reaper_, mode.command);
    property.UpdateState();
    EXPECT_EQ(property.GetInt(), mode.value) << mode.text;
    EXPECT_EQ(property.GetText(), mode.text);
  }
}

TEST_F(TimelinePropertyTest, WritingTheRulerModeSetsIt) {
  RulerModeProperty property(&scene_, "user:ruler");
  property.SetInt(2);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kFrames);
  EXPECT_EQ(property.GetInt(), 2);
  property.SetInt(0);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kBeats);
}

TEST_F(TimelinePropertyTest, IsRulerModeOnlyTurnsOn) {
  IsRulerModeProperty time(&scene_, "user:time", TimelineMode::kTime);
  time.UpdateState();
  EXPECT_FALSE(time.GetBool());

  time.SetBool(true);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);
  EXPECT_TRUE(time.GetBool());

  // There is always a primary mode, so turning it off does nothing.
  time.SetBool(false);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);
  EXPECT_TRUE(time.GetBool());
}

TEST_F(TimelinePropertyTest, SecondaryRulerModeIsEachModeOrNone) {
  SecondaryRulerModeProperty property(&scene_, "user:secondary");
  property.UpdateState();
  EXPECT_EQ(property.GetInt(), 0);
  EXPECT_EQ(property.GetText(), "None");

  property.SetInt(3);
  EXPECT_EQ(GetRulerSecondaryMode(), TimelineMode::kSamples);
  EXPECT_EQ(property.GetInt(), 3);
  EXPECT_EQ(property.GetText(), "Samples");
  property.SetInt(1);
  EXPECT_EQ(property.GetText(), "Time");
  property.SetInt(2);
  EXPECT_EQ(property.GetText(), "Frames");

  property.SetInt(0);
  EXPECT_EQ(GetRulerSecondaryMode(), std::nullopt);
  EXPECT_EQ(property.GetText(), "None");
}

TEST_F(TimelinePropertyTest, IsSecondaryRulerModeTurnsOnAndOff) {
  IsSecondaryRulerModeProperty frames(&scene_, "user:frames",
                                      TimelineMode::kFrames);
  IsSecondaryRulerModeProperty time(&scene_, "user:time", TimelineMode::kTime);

  frames.SetBool(true);
  EXPECT_EQ(GetRulerSecondaryMode(), TimelineMode::kFrames);
  EXPECT_TRUE(frames.GetBool());

  // Turning off a mode that isn't on leaves the secondary mode as it is.
  time.SetBool(false);
  EXPECT_EQ(GetRulerSecondaryMode(), TimelineMode::kFrames);

  frames.SetBool(false);
  EXPECT_EQ(GetRulerSecondaryMode(), std::nullopt);
  EXPECT_FALSE(frames.GetBool());
}

}  // namespace
}  // namespace jpr
