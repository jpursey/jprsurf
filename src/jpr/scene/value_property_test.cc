// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/value_property.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace jpr {
namespace {

TEST(ToggleValuePropertyTest, HoldsItsValue) {
  ToggleValueProperty property("user:flag", true);
  EXPECT_EQ(property.GetType(), ViewProperty::Type::kToggle);
  EXPECT_TRUE(property.GetBool());
  property.SetBool(false);
  EXPECT_FALSE(property.GetBool());
}

TEST(ToggleValuePropertyTest, NotifiesOnlyOnChange) {
  ToggleValueProperty property("user:flag");
  bool changed = false;
  property.RegisterFlag(&changed);
  property.SetBool(false);
  EXPECT_FALSE(changed);
  property.SetBool(true);
  EXPECT_TRUE(changed);
  property.UnregisterFlag(&changed);
}

TEST(EnumeratedValuePropertyTest, NamesSetTheRange) {
  EnumeratedValueProperty property("user:step", {"measure", "beat", "marker"});
  EXPECT_EQ(property.GetType(), ViewProperty::Type::kEnumerated);
  EXPECT_EQ(property.GetMaxValue(), 2);
  EXPECT_EQ(property.GetInt(), 0);
  EXPECT_EQ(property.GetText(), "measure");
}

TEST(EnumeratedValuePropertyTest, StartValueIsClamped) {
  EXPECT_EQ(EnumeratedValueProperty("user:step", {"a", "b", "c"}, 1).GetInt(),
            1);
  EXPECT_EQ(EnumeratedValueProperty("user:step", {"a", "b", "c"}, 5).GetInt(),
            2);
  EXPECT_EQ(EnumeratedValueProperty("user:step", {"a", "b", "c"}, -1).GetInt(),
            0);
}

TEST(EnumeratedValuePropertyTest, SetsTheValueAskedFor) {
  EnumeratedValueProperty property("user:step", {"measure", "beat", "marker"});
  property.SetInt(2);
  EXPECT_EQ(property.GetInt(), 2);
  EXPECT_EQ(property.GetText(), "marker");
  property.SetInt(1);
  EXPECT_EQ(property.GetInt(), 1);
  EXPECT_EQ(property.GetText(), "beat");
  property.SetInt(7);
  EXPECT_EQ(property.GetInt(), 2);
  property.SetInt(-3);
  EXPECT_EQ(property.GetInt(), 0);
}

TEST(EnumeratedValuePropertyTest, SetsTextByName) {
  EnumeratedValueProperty property("user:step", {"measure", "beat", "marker"});
  property.SetText("marker");
  EXPECT_EQ(property.GetInt(), 2);
  property.SetText("beat");
  EXPECT_EQ(property.GetInt(), 1);
  property.SetText("0");
  EXPECT_EQ(property.GetInt(), 0);
  EXPECT_TRUE(property.Equals(std::string("measure")));
}

TEST(EnumeratedValuePropertyTest, NotifiesOnlyOnChange) {
  EnumeratedValueProperty property("user:step", {"measure", "beat", "marker"});
  bool changed = false;
  property.RegisterFlag(&changed);
  property.SetInt(0);
  EXPECT_FALSE(changed);
  property.SetInt(1);
  EXPECT_TRUE(changed);
  changed = false;
  property.SetInt(1);
  EXPECT_FALSE(changed);
  property.UnregisterFlag(&changed);
}

TEST(EnumeratedValuePropertyTest, NoNamesHasOneEmptyValue) {
  EnumeratedValueProperty property("user:step", {}, 3);
  EXPECT_EQ(property.GetMaxValue(), 0);
  EXPECT_EQ(property.GetInt(), 0);
  EXPECT_EQ(property.GetText(), "");
}

TEST(CallbackActionPropertyTest, RunningItCallsTheCallback) {
  int calls = 0;
  CallbackActionProperty property("user:action", [&calls] { ++calls; });
  EXPECT_EQ(property.GetType(), ViewProperty::Type::kAction);
  property.RunAction();
  property.RunAction();
  EXPECT_EQ(calls, 2);
}

TEST(CallbackTogglePropertyTest, EveryWriteCallsTheCallback) {
  std::vector<bool> writes;
  CallbackToggleProperty property(
      "user:toggle", [&writes](bool value) { writes.push_back(value); });
  EXPECT_EQ(property.GetType(), ViewProperty::Type::kToggle);
  property.SetBool(true);
  EXPECT_FALSE(property.GetBool());
  property.SetBool(true);
  property.SetBool(false);
  EXPECT_EQ(writes, (std::vector<bool>{true, true, false}));
}

}  // namespace
}  // namespace jpr
