// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_condition.h"

#include <memory>

#include "gtest/gtest.h"
#include "jpr/scene/value_property.h"

namespace jpr {
namespace {

TEST(ViewConditionTest, MetWhilePropertyEqualsValue) {
  ToggleValueProperty property("user:test", true);
  ViewCondition on(&property, true);
  ViewCondition off(&property, false);
  EXPECT_TRUE(on.IsMet());
  EXPECT_FALSE(off.IsMet());
  property.SetBool(false);
  EXPECT_FALSE(on.IsMet());
  EXPECT_TRUE(off.IsMet());
}

TEST(ViewConditionTest, ChangesOnlyWhileWatched) {
  ToggleValueProperty property("user:test");
  auto condition = std::make_unique<ViewCondition>(&property, true);
  property.SetBool(true);
  EXPECT_FALSE(condition->HasChanged());

  condition->Watch(true);
  EXPECT_TRUE(property.IsWatched());
  property.SetBool(false);
  EXPECT_TRUE(condition->HasChanged());
  condition->Watch(true);
  EXPECT_FALSE(condition->HasChanged());

  condition.reset();
  EXPECT_FALSE(property.IsWatched());
}

}  // namespace
}  // namespace jpr
