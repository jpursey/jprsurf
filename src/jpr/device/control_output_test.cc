// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output.h"

#include <iterator>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/timeline.h"
#include "jpr/device/fake_control_io.h"

namespace jpr {
namespace {

using ::testing::Optional;

//==============================================================================
// ControlCValueOutput
//==============================================================================

TEST(ControlCValueOutputTest, ValueAndModeAreClamped) {
  FakeCValueOutput output(/*mode_count=*/2);
  EXPECT_EQ(output.GetType(), ControlOutput::Type::kCValue);
  EXPECT_EQ(output.GetModeCount(), 2);

  output.SetValue(0.5, /*mode=*/1);
  EXPECT_EQ(output.GetValue(), 0.5);
  EXPECT_EQ(output.GetMode(), 1);

  output.SetValue(1.5, /*mode=*/2);
  EXPECT_EQ(output.GetValue(), 1.0);
  EXPECT_EQ(output.GetMode(), 1);

  output.SetValue(-0.5, /*mode=*/-1);
  EXPECT_EQ(output.GetValue(), 0.0);
  EXPECT_EQ(output.GetMode(), 0);
}

// The same value again is still set, as an output may need to send it again.
TEST(ControlCValueOutputTest, SameValueIsSetAgain) {
  FakeCValueOutput output;
  output.SetValue(0.5);
  output.SetValue(0.5);
  EXPECT_EQ(output.GetSetCount(), 2);
}

//==============================================================================
// ControlDValueOutput
//==============================================================================

TEST(ControlDValueOutputTest, EachModeHasItsOwnMaxValue) {
  FakeDValueOutput output({1, 10});
  EXPECT_EQ(output.GetType(), ControlOutput::Type::kDValue);
  EXPECT_EQ(output.GetModeCount(), 2);
  EXPECT_EQ(output.GetMaxValue(), 1);
  EXPECT_EQ(output.GetMaxValue(/*mode=*/1), 10);

  // A mode out of range is the nearest.
  EXPECT_EQ(output.GetMaxValue(/*mode=*/2), 10);
  EXPECT_EQ(output.GetMaxValue(/*mode=*/-1), 1);
}

TEST(ControlDValueOutputTest, ValueIsClampedToItsModesRange) {
  FakeDValueOutput output({1, 10});

  output.SetValue(5, /*mode=*/1);
  EXPECT_EQ(output.GetValue(), 5);
  EXPECT_EQ(output.GetMode(), 1);

  output.SetValue(5, /*mode=*/0);
  EXPECT_EQ(output.GetValue(), 1);
  EXPECT_EQ(output.GetMode(), 0);

  output.SetValue(20, /*mode=*/3);
  EXPECT_EQ(output.GetValue(), 10);
  EXPECT_EQ(output.GetMode(), 1);

  output.SetValue(-1, /*mode=*/1);
  EXPECT_EQ(output.GetValue(), 0);
}

TEST(ControlDValueOutputTest, ClearedValueIsZeroUnlessSet) {
  FakeDValueOutput output({1, 10});
  EXPECT_EQ(output.GetClearedValue(), 0);
  EXPECT_EQ(output.GetClearedMode(), 0);

  output.SetCleared(3, /*mode=*/1);
  EXPECT_EQ(output.GetClearedValue(), 3);
  EXPECT_EQ(output.GetClearedMode(), 1);
}

//==============================================================================
// ControlTextOutput
//==============================================================================

// A text output with the default timeline text, formatted from the position.
class FormattedTextOutput final : public ControlTextOutput {
 public:
  const std::string& GetText() const { return text_; }
  int GetMode() const { return mode_; }

 protected:
  void OnTextChanged(std::string_view text, int mode) override {
    text_ = std::string(text);
    mode_ = mode;
  }

 private:
  std::string text_;
  int mode_ = -1;
};

class ControlTextOutputTest : public ::testing::Test {
 protected:
  // The ruler's "Seconds" time unit action.
  static constexpr int kRulerSeconds = 40368;

  // The ruler shows seconds.
  ControlTextOutputTest() {
    AddReaperActions(&reaper_);
    SelectRulerMode(&reaper_, kRulerSeconds);
  }

  FakeReaper reaper_;
};

TEST_F(ControlTextOutputTest, TextModeIsClamped) {
  FakeTextOutput output(/*mode_count=*/2);
  EXPECT_EQ(output.GetType(), ControlOutput::Type::kText);

  output.SetText("Bass", /*mode=*/1);
  EXPECT_EQ(output.GetText(), "Bass");
  EXPECT_EQ(output.GetMode(), 1);

  output.SetText("Drums", /*mode=*/2);
  EXPECT_EQ(output.GetMode(), 1);
  output.SetText("Keys", /*mode=*/-1);
  EXPECT_EQ(output.GetMode(), 0);
}

// Mode 0 is the ruler's time unit, and 1-4 are beats, time, frames, and
// samples. A mode out of range is the nearest.
TEST_F(ControlTextOutputTest, TimelineModeIsTheRulersOrChosen) {
  struct Case {
    int mode;
    TimelineMode timeline_mode;
  };
  constexpr Case kCases[] = {
      {0, TimelineMode::kTime},    {1, TimelineMode::kBeats},
      {2, TimelineMode::kTime},    {3, TimelineMode::kFrames},
      {4, TimelineMode::kSamples}, {5, TimelineMode::kSamples},
      {-1, TimelineMode::kTime},
  };
  FakeTextOutput output;
  for (const Case& entry : kCases) {
    SCOPED_TRACE(entry.mode);
    output.SetTimelineText(TimelinePosition(3.5), entry.mode);
    EXPECT_THAT(output.GetPosition(), Optional(3.5));
    EXPECT_EQ(output.GetTimelineMode(), entry.timeline_mode);
  }
}

// By default, a position is set as Timeline formats it, as text in mode 0.
TEST_F(ControlTextOutputTest, TimelineTextIsFormattedByDefault) {
  constexpr TimelineMode kModes[] = {TimelineMode::kBeats, TimelineMode::kTime,
                                     TimelineMode::kFrames,
                                     TimelineMode::kSamples};
  const TimelinePosition position(3.5);
  FormattedTextOutput output;
  for (int i = 0; i < std::ssize(kModes); ++i) {
    SCOPED_TRACE(i);
    output.SetTimelineText(position, /*mode=*/i + 1);
    EXPECT_EQ(output.GetText(), position.ToString(kModes[i]));
    EXPECT_EQ(output.GetMode(), 0);
  }
}

//==============================================================================
// ControlColorOutput
//==============================================================================

TEST(ControlColorOutputTest, ColorIsSetAndModeIsClamped) {
  FakeColorOutput output(/*mode_count=*/2);
  EXPECT_EQ(output.GetType(), ControlOutput::Type::kColor);

  output.SetColor({255, 128, 0}, /*mode=*/1);
  EXPECT_EQ(output.GetColor(), (Color{255, 128, 0}));
  EXPECT_EQ(output.GetMode(), 1);

  output.SetColor({0, 0, 255}, /*mode=*/5);
  EXPECT_EQ(output.GetMode(), 1);
  output.SetColor({0, 0, 255}, /*mode=*/-1);
  EXPECT_EQ(output.GetMode(), 0);
}

}  // namespace
}  // namespace jpr
