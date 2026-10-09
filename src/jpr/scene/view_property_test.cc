// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_property.h"

#include <string>

#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/timeline.h"
#include "jpr/scene/testing/test_property.h"

namespace jpr {
namespace {

using Type = ViewProperty::Type;

TEST(ViewPropertyTest, EqualsBoolReadsAsBool) {
  EXPECT_TRUE(TestProperty(Type::kToggle, true).Equals(true));
  EXPECT_FALSE(TestProperty(Type::kToggle, true).Equals(false));
  EXPECT_TRUE(TestProperty(Type::kNormalized, 0.75).Equals(true));
  EXPECT_TRUE(TestProperty(Type::kNormalized, 0.25).Equals(false));
  EXPECT_TRUE(TestProperty(Type::kEnumerated, 2, 2).Equals(true));
  EXPECT_TRUE(TestProperty(Type::kEnumerated, 0, 2).Equals(false));
}

TEST(ViewPropertyTest, EqualsIntReadsAsInt) {
  TestProperty enumerated(Type::kEnumerated, 1, 2);
  EXPECT_TRUE(enumerated.Equals(1));
  EXPECT_FALSE(enumerated.Equals(0));
  EXPECT_FALSE(enumerated.Equals(2));
  EXPECT_TRUE(TestProperty(Type::kNormalized, 0.5, 4).Equals(2));
  EXPECT_TRUE(TestProperty(Type::kText, std::string("3"), 4).Equals(3));
}

TEST(ViewPropertyTest, EqualsDoubleReadsInPropertyRange) {
  EXPECT_TRUE(TestProperty(Type::kPan, -1.0).Equals(-1.0));
  EXPECT_TRUE(TestProperty(Type::kVolume, 2.0).Equals(2.0));
  EXPECT_TRUE(TestProperty(Type::kNormalized, 0.25).Equals(0.25));
  EXPECT_FALSE(TestProperty(Type::kNormalized, 0.25).Equals(0.5));
  EXPECT_TRUE(TestProperty(Type::kToggle, true).Equals(1.0));
  EXPECT_TRUE(TestProperty(Type::kEnumerated, 1, 2).Equals(0.5));
}

TEST(ViewPropertyTest, EqualsTextReadsAsText) {
  EXPECT_TRUE(TestProperty(Type::kText, std::string("Track"))
                  .Equals(std::string("Track")));
  EXPECT_FALSE(TestProperty(Type::kText, std::string("Track"))
                   .Equals(std::string("Bus")));
  EXPECT_TRUE(TestProperty(Type::kToggle, true).Equals(std::string("On")));
  EXPECT_TRUE(TestProperty(Type::kEnumerated, 1, 2).Equals(std::string("1")));
}

TEST(ViewPropertyTest, EqualsColorReadsAsColor) {
  EXPECT_TRUE(TestProperty(Type::kColor, Color{255, 128, 0})
                  .Equals(Color{255, 128, 0}));
  EXPECT_FALSE(
      TestProperty(Type::kColor, Color{255, 128, 0}).Equals(Color{0, 0, 0}));
  EXPECT_TRUE(TestProperty(Type::kToggle, true).Equals(Color{255, 255, 255}));
}

TEST(ViewPropertyTest, EqualsTimelinePositionOnlyOnTimeline) {
  TestProperty timeline(Type::kTimelinePosition, TimelinePosition(2.0));
  EXPECT_TRUE(timeline.Equals(TimelinePosition(2.0)));
  EXPECT_FALSE(timeline.Equals(TimelinePosition(1.0)));
  EXPECT_FALSE(TestProperty(Type::kToggle, false).Equals(TimelinePosition()));
  EXPECT_FALSE(TestProperty(Type::kNormalized, 0.0).Equals(TimelinePosition()));
}

TEST(ViewPropertyTest, EqualsNeverWithoutValue) {
  EXPECT_FALSE(
      TestProperty(Type::kAction, std::monostate()).Equals(std::monostate()));
}

TEST(ViewPropertyTest, FittedVolumeDropsDecimalsThenTheUnit) {
  FakeReaper reaper;
  EXPECT_EQ(TestProperty(Type::kVolume, 0.000000631).GetFittedText(7),
            "-124dB");  // "-124.0dB".
  EXPECT_EQ(TestProperty(Type::kVolume, 0.3163).GetFittedText(7),
            "-10.0dB");  // "-10.00dB".
  EXPECT_EQ(TestProperty(Type::kVolume, 3.162).GetFittedText(7),
            "+10.0dB");  // "+10.00dB".
  EXPECT_EQ(TestProperty(Type::kVolume, 0.5).GetFittedText(4), "-6dB");
  EXPECT_EQ(TestProperty(Type::kVolume, 0.0000001).GetFittedText(4), "-140");
  EXPECT_EQ(TestProperty(Type::kVolume, 0.0).GetFittedText(4), "-inf");
}

TEST(ViewPropertyTest, FittedVolumeIsUnchangedWhenItFits) {
  FakeReaper reaper;
  EXPECT_EQ(TestProperty(Type::kVolume, 0.5).GetFittedText(7), "-6.02dB");
  EXPECT_EQ(TestProperty(Type::kVolume, 0.0).GetFittedText(7), "-inf dB");

  // A width of 0 is no limit.
  EXPECT_EQ(TestProperty(Type::kVolume, 0.000000631).GetFittedText(0),
            "-124.0dB");
}

// Text that has no shorter form is left for the control to cut.
TEST(ViewPropertyTest, FittedTextOfOtherTypesIsUnchanged) {
  FakeReaper reaper;
  EXPECT_EQ(
      TestProperty(Type::kText, std::string("Vocal Bus")).GetFittedText(7),
      "Vocal Bus");
  EXPECT_EQ(TestProperty(Type::kPan, -0.25).GetFittedText(3), "25%L");
}

}  // namespace
}  // namespace jpr
