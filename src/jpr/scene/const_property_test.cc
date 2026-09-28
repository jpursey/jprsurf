// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/const_property.h"

#include <limits>
#include <memory>
#include <string>
#include <variant>

#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using Type = ViewProperty::Type;

TEST(ConstPropertyTest, Toggle) {
  std::unique_ptr<ViewProperty> property =
      CreateConstProperty(Type::kToggle, true);
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(property->GetType(), Type::kToggle);
  EXPECT_EQ(property->GetBool(), true);
}

TEST(ConstPropertyTest, NamesAreUniqueConstNames) {
  std::unique_ptr<ViewProperty> first =
      CreateConstProperty(Type::kToggle, true);
  std::unique_ptr<ViewProperty> second =
      CreateConstProperty(Type::kToggle, true);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_TRUE(first->GetName().starts_with(kConstNamespace));
  EXPECT_TRUE(second->GetName().starts_with(kConstNamespace));
  EXPECT_NE(first->GetName(), second->GetName());
}

TEST(ConstPropertyTest, Normalized) {
  for (double value : {0.0, 0.25, 1.0}) {
    std::unique_ptr<ViewProperty> property =
        CreateConstProperty(Type::kNormalized, value);
    ASSERT_NE(property, nullptr) << value;
    EXPECT_EQ(property->GetType(), Type::kNormalized) << value;
    EXPECT_EQ(property->GetNormalized(), value) << value;
  }
}

TEST(ConstPropertyTest, Text) {
  std::unique_ptr<ViewProperty> property =
      CreateConstProperty(Type::kText, std::string("Track"));
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(property->GetType(), Type::kText);
  EXPECT_EQ(property->GetText(), "Track");
}

TEST(ConstPropertyTest, Color) {
  std::unique_ptr<ViewProperty> property =
      CreateConstProperty(Type::kColor, Color{255, 128, 0});
  ASSERT_NE(property, nullptr);
  EXPECT_EQ(property->GetType(), Type::kColor);
  EXPECT_EQ(property->GetColor(), (Color{255, 128, 0}));
}

TEST(ConstPropertyTest, IgnoresWrites) {
  std::unique_ptr<ViewProperty> property =
      CreateConstProperty(Type::kToggle, true);
  ASSERT_NE(property, nullptr);
  bool changed = false;
  property->RegisterFlag(&changed);
  property->SetBool(false);
  EXPECT_EQ(property->GetBool(), true);
  EXPECT_FALSE(changed);
  property->UnregisterFlag(&changed);
}

TEST(ConstPropertyTest, RejectsValueOfWrongType) {
  EXPECT_EQ(CreateConstProperty(Type::kToggle, 1.0), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kNormalized, true), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kText, Color{0, 0, 0}), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kColor, std::string("#000")), nullptr);
}

TEST(ConstPropertyTest, RejectsNormalizedOutOfRange) {
  for (double value : {-0.5, 1.5, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
    EXPECT_EQ(CreateConstProperty(Type::kNormalized, value), nullptr) << value;
  }
}

TEST(ConstPropertyTest, RejectsOtherTypes) {
  EXPECT_EQ(CreateConstProperty(Type::kAction, std::monostate()), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kPan, 0.0), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kVolume, 1.0), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kEnumerated, 0), nullptr);
  EXPECT_EQ(CreateConstProperty(Type::kTimelinePosition, TimelinePosition()),
            nullptr);
}

}  // namespace
}  // namespace jpr
