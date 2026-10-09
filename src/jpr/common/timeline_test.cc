// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/timeline.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/reaper_actions.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Optional;

// The ruler starts in Measure.Beats, with no secondary unit.
class TimelineTest : public ::testing::Test {
 protected:
  TimelineTest() { AddReaperActions(&reaper_); }

  // Picks the ruler's time unit action `mode`, as the user does in REAPER.
  void Select(int mode) { SelectRulerMode(&reaper_, mode); }

  FakeReaper reaper_;
};

//------------------------------------------------------------------------------
// Ruler modes
//------------------------------------------------------------------------------

TEST_F(TimelineTest, ReadsTheRulerMode) {
  EXPECT_EQ(GetRulerMode(), TimelineMode::kBeats);
  Select(kRulerSeconds);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);
  Select(kRulerAbsoluteFrames);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kFrames);
  Select(kRulerSamples);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kSamples);
  EXPECT_TRUE(IsCurrentRulerMode(TimelineMode::kSamples));
}

TEST_F(TimelineTest, SetsTheRulerModeOnlyIfItChanges) {
  SetRulerMode(TimelineMode::kBeats);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());

  // The first action of each mode is its default.
  SetRulerMode(TimelineMode::kTime);
  SetRulerMode(TimelineMode::kFrames);
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(kRulerMinutesSecondsMinimal, kRulerFrames));
  EXPECT_EQ(GetRulerMode(), TimelineMode::kFrames);
}

TEST_F(TimelineTest, SetsTheLastRulerModeSeenOfEachKind) {
  Select(kRulerSeconds);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);

  SetRulerMode(TimelineMode::kBeats);
  SetRulerMode(TimelineMode::kTime);
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(kRulerMeasuresBeatsMinimal, kRulerSeconds));
}

TEST_F(TimelineTest, ReadsAndSetsTheSecondaryMode) {
  EXPECT_EQ(GetRulerSecondaryMode(), std::nullopt);
  EXPECT_FALSE(HasCurrentRulerSecondaryMode());

  SetRulerSecondaryMode(TimelineMode::kFrames);
  EXPECT_THAT(GetRulerSecondaryMode(), Optional(TimelineMode::kFrames));
  EXPECT_TRUE(IsCurrentRulerSecondaryMode(TimelineMode::kFrames));

  // Beats can't be a secondary mode, so it clears it.
  SetRulerSecondaryMode(TimelineMode::kBeats);
  EXPECT_EQ(GetRulerSecondaryMode(), std::nullopt);
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(kRulerSecondaryFrames, kRulerSecondaryNone));

  // Clearing it when there is none runs nothing.
  ClearRulerSecondaryMode();
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(kRulerSecondaryFrames, kRulerSecondaryNone));
}

//------------------------------------------------------------------------------
// Positions
//------------------------------------------------------------------------------

TEST_F(TimelineTest, PositionIsThePlayPositionWhilePlayingOrPaused) {
  FakeProject& project = reaper_.GetProject();
  project.SetPlayPosition(2.0);
  project.SetCursorPosition(1.0);
  EXPECT_EQ(TimelinePosition::Get().GetValue(), 1.0);
  project.SetPlayState(1);  // Playing.
  EXPECT_EQ(TimelinePosition::Get().GetValue(), 2.0);
  project.SetPlayState(2);  // Paused.
  EXPECT_EQ(TimelinePosition::Get().GetValue(), 2.0);
  EXPECT_EQ(TimelinePosition::GetEdit().GetValue(), 1.0);
}

// The fake's project is at 120 BPM in 4/4, with 30 frames and 44100 samples a
// second.
TEST_F(TimelineTest, ReadsPositionsInEachMode) {
  const TimelinePosition position(3725.5);
  const BeatsPosition beats = position.ToBeats();
  EXPECT_EQ(beats.measure, 1863);
  EXPECT_EQ(beats.beat, 4);
  EXPECT_EQ(beats.division, 0);

  const TimePosition time = position.ToTime();
  EXPECT_EQ(time.hours, 1);
  EXPECT_EQ(time.minutes, 2);
  EXPECT_EQ(time.seconds, 5);
  EXPECT_EQ(time.milliseconds, 500);

  const FramesPosition frames = position.ToFrames();
  EXPECT_EQ(frames.hours, 1);
  EXPECT_EQ(frames.minutes, 2);
  EXPECT_EQ(frames.seconds, 5);
  EXPECT_EQ(frames.frames, 15);

  EXPECT_EQ(position.ToSamples(), 164294550);
}

TEST_F(TimelineTest, FormatsPositions) {
  const TimelinePosition position(3.5);
  EXPECT_EQ(position.ToString(TimelineMode::kBeats), "2.4.00");
  EXPECT_EQ(position.ToString(TimelineMode::kTime), "0:03.500");
  EXPECT_EQ(position.ToString(TimelineMode::kFrames), "00:00:03:15");
  EXPECT_EQ(position.ToString(TimelineMode::kSamples), "154350");
}

TEST_F(TimelineTest, ReadsNegativePositions) {
  const TimePosition time = TimelinePosition(-65.25).ToTime();
  EXPECT_TRUE(time.negative);
  EXPECT_EQ(time.minutes, 1);
  EXPECT_EQ(time.seconds, 5);
  EXPECT_EQ(time.milliseconds, 250);
}

}  // namespace
}  // namespace jpr
