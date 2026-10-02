// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/timeline.h"

#include <optional>

#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Optional;

// The ruler's time unit actions, and its secondary ones, by mode.
constexpr int kRulerModes[] = {
    41916, 40367, 43205,  // Beats.
    43204, 40365, 40368,  // Time.
    40370, 41973,         // Frames.
    40369,                // Samples.
};
constexpr int kSecondaryModes[] = {
    42360,                // None.
    43705, 42361, 42362,  // Time.
    42364, 42365,         // Frames.
    42363,                // Samples.
};

class TimelineTest : public ::testing::Test {
 protected:
  TimelineTest() {
    AddModes(kRulerModes);
    AddModes(kSecondaryModes);
  }

  // Adds each of `modes` as a toggle action, which turns itself on and the
  // others off when it runs, as the ruler's do. The first is on.
  void AddModes(absl::Span<const int> modes) {
    for (int mode : modes) {
      reaper_.AddCommand(
          {.id = mode,
           .text = "View: Time unit",
           .toggle_state = (mode == modes[0] ? 1 : 0),
           .on_run = [this, modes, mode] { Select(modes, mode); }});
    }
  }

  // Turns `mode` on, and the rest of `modes` off, as the user picking it in
  // REAPER does.
  void Select(absl::Span<const int> modes, int mode) {
    for (int other : modes) {
      reaper_.SetToggleState(other, other == mode ? 1 : 0);
    }
  }

  FakeReaper reaper_;
};

//------------------------------------------------------------------------------
// Ruler modes
//------------------------------------------------------------------------------

TEST_F(TimelineTest, ReadsTheRulerMode) {
  EXPECT_EQ(GetRulerMode(), TimelineMode::kBeats);
  Select(kRulerModes, 40368);  // Seconds.
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);
  Select(kRulerModes, 41973);  // Absolute frames.
  EXPECT_EQ(GetRulerMode(), TimelineMode::kFrames);
  Select(kRulerModes, 40369);
  EXPECT_EQ(GetRulerMode(), TimelineMode::kSamples);
  EXPECT_TRUE(IsCurrentRulerMode(TimelineMode::kSamples));
}

TEST_F(TimelineTest, SetsTheRulerModeOnlyIfItChanges) {
  SetRulerMode(TimelineMode::kBeats);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());

  // The first action of each mode is its default.
  SetRulerMode(TimelineMode::kTime);
  SetRulerMode(TimelineMode::kFrames);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(43204, 40370));
  EXPECT_EQ(GetRulerMode(), TimelineMode::kFrames);
}

TEST_F(TimelineTest, SetsTheLastRulerModeSeenOfEachKind) {
  Select(kRulerModes, 40368);  // Seconds.
  EXPECT_EQ(GetRulerMode(), TimelineMode::kTime);

  SetRulerMode(TimelineMode::kBeats);
  SetRulerMode(TimelineMode::kTime);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(41916, 40368));
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
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(42364, 42360));

  // Clearing it when there is none runs nothing.
  ClearRulerSecondaryMode();
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(42364, 42360));
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
