// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_property.h"

#include <string>
#include <utility>
#include <variant>

#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"

namespace jpr {
namespace {

using Type = ViewProperty::Type;

// A property of any type that reads the value it is constructed with.
class TestProperty final : public ViewProperty {
 public:
  TestProperty(Type type, Value value, int max_value = 0)
      : ViewProperty("user:test", type),
        value_(std::move(value)),
        max_value_(max_value) {}

  int GetMaxValue() const override { return max_value_; }

 protected:
  bool ReadBool() const override { return std::get<bool>(value_); }
  double ReadDouble() const override { return std::get<double>(value_); }
  std::string ReadString() const override {
    return std::get<std::string>(value_);
  }
  Color ReadColor() const override { return std::get<Color>(value_); }
  TimelinePosition ReadTimelinePosition() const override {
    return std::get<TimelinePosition>(value_);
  }
  int ReadInt() const override { return std::get<int>(value_); }

 private:
  Value value_;
  int max_value_;
};

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

}  // namespace
}  // namespace jpr
