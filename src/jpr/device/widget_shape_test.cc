// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_shape.h"

#include <iterator>
#include <optional>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_path.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kDelta = ControlInput::Type::kDelta;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;
constexpr ControlOutput::Type kText = ControlOutput::Type::kText;
constexpr ControlOutput::Type kColor = ControlOutput::Type::kColor;

// Returns the path, failing the test if the text isn't one.
WidgetPath Path(std::string_view text) {
  std::optional<WidgetPath> path = WidgetPath::Parse(text);
  EXPECT_TRUE(path.has_value()) << text;
  return path.value_or(WidgetPath());
}

class WidgetShapeTest : public ::testing::Test {
 protected:
  const WidgetShape button_ =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape fader_ =
      WidgetShape::ForControl({kValue, kPress}, {kCValue});
  const WidgetShape display_ = WidgetShape::ForControl({}, {kText, kColor});
  const WidgetShape strip_ = WidgetShape::ForStruct(
      {{"fader", fader_}, {"select", button_}, {"display", display_}});
  const WidgetShape desk_ = WidgetShape::ForStruct(
      {{"play", button_}, {"strip", WidgetShape::ForArray(strip_, 4)}});
};

TEST_F(WidgetShapeTest, ControlHasItsInputsOutputsAndModeNames) {
  EXPECT_EQ(button_.GetKind(), WidgetKind::kControl);
  EXPECT_EQ(button_.GetInputs(), Control::Inputs(kPress));
  EXPECT_EQ(button_.GetOutputs(), Control::Outputs(kDValue));
  EXPECT_THAT(button_.GetModeNames(), ElementsAre("solid", "blink"));

  EXPECT_EQ(fader_.GetInputs(), Control::Inputs({kValue, kPress}));
  EXPECT_EQ(fader_.GetOutputs(), Control::Outputs(kCValue));
  EXPECT_THAT(fader_.GetModeNames(), IsEmpty());

  EXPECT_THAT(button_.GetFields(), IsEmpty());
  EXPECT_EQ(button_.GetField("solid"), nullptr);
  EXPECT_EQ(button_.GetSize(), 0);
  EXPECT_EQ(button_.GetElement(), nullptr);
}

TEST_F(WidgetShapeTest, StructHasItsFieldsInOrder) {
  EXPECT_EQ(strip_.GetKind(), WidgetKind::kStruct);
  ASSERT_EQ(std::ssize(strip_.GetFields()), 3);
  EXPECT_EQ(strip_.GetFields()[0].name, "fader");
  EXPECT_EQ(strip_.GetFields()[0].shape, fader_);
  EXPECT_EQ(strip_.GetFields()[1].name, "select");
  EXPECT_EQ(strip_.GetFields()[1].shape, button_);
  EXPECT_EQ(strip_.GetFields()[2].name, "display");
  EXPECT_EQ(strip_.GetFields()[2].shape, display_);

  const WidgetShape* select = strip_.GetField("select");
  ASSERT_NE(select, nullptr);
  EXPECT_EQ(*select, button_);
  EXPECT_EQ(strip_.GetField("mute"), nullptr);

  EXPECT_TRUE(strip_.GetInputs().IsEmpty());
  EXPECT_TRUE(strip_.GetOutputs().IsEmpty());
  EXPECT_THAT(strip_.GetModeNames(), IsEmpty());
  EXPECT_EQ(strip_.GetSize(), 0);
  EXPECT_EQ(strip_.GetElement(), nullptr);
}

TEST_F(WidgetShapeTest, ArrayHasItsSizeAndElement) {
  const WidgetShape array = WidgetShape::ForArray(strip_, 4);
  EXPECT_EQ(array.GetKind(), WidgetKind::kArray);
  EXPECT_EQ(array.GetSize(), 4);
  ASSERT_NE(array.GetElement(), nullptr);
  EXPECT_EQ(*array.GetElement(), strip_);

  EXPECT_TRUE(array.GetInputs().IsEmpty());
  EXPECT_TRUE(array.GetOutputs().IsEmpty());
  EXPECT_THAT(array.GetModeNames(), IsEmpty());
  EXPECT_THAT(array.GetFields(), IsEmpty());
  EXPECT_EQ(array.GetField("fader"), nullptr);
}

TEST_F(WidgetShapeTest, FindReachesShapesThroughStructsAndArrays) {
  EXPECT_EQ(desk_.Find(WidgetPath()), &desk_);

  const WidgetShape* found = desk_.Find(Path("play"));
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, button_);

  found = desk_.Find(Path("strip"));
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, WidgetShape::ForArray(strip_, 4));

  found = desk_.Find(Path("strip[0]"));
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, strip_);

  found = desk_.Find(Path("strip[3].fader"));
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, fader_);

  found = strip_.Find(Path("display"));
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(*found, display_);
}

TEST_F(WidgetShapeTest, FindReturnsNullWhereThereIsNoWidget) {
  EXPECT_EQ(desk_.Find(Path("stop")), nullptr);
  EXPECT_EQ(desk_.Find(Path("strip[4]")), nullptr);
  EXPECT_EQ(desk_.Find(Path("strip[4].fader")), nullptr);
  EXPECT_EQ(desk_.Find(Path("strip[0].mute")), nullptr);
  EXPECT_EQ(desk_.Find(Path("strip.fader")), nullptr);
  EXPECT_EQ(desk_.Find(Path("[0]")), nullptr);
  EXPECT_EQ(desk_.Find(Path("play.light")), nullptr);
  EXPECT_EQ(desk_.Find(Path("play[0]")), nullptr);
  EXPECT_EQ(desk_.Find(Path("strip[0].fader.touch")), nullptr);
}

TEST_F(WidgetShapeTest, ControlsAreEqualWithTheSameInputsOutputsAndModes) {
  EXPECT_EQ(button_,
            WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"}));
  EXPECT_NE(button_,
            WidgetShape::ForControl({kDelta}, {kDValue}, {"solid", "blink"}));
  EXPECT_NE(button_, WidgetShape::ForControl({kPress}, {kDValue, kColor},
                                             {"solid", "blink"}));
  EXPECT_NE(button_,
            WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "flash"}));
  EXPECT_NE(button_,
            WidgetShape::ForControl({kPress}, {kDValue}, {"blink", "solid"}));
  EXPECT_NE(button_, WidgetShape::ForControl({kPress}, {kDValue}, {"solid"}));
}

TEST_F(WidgetShapeTest, StructsAreEqualWithTheSameFieldsInAnyOrder) {
  EXPECT_EQ(strip_, WidgetShape::ForStruct({{"fader", fader_},
                                            {"select", button_},
                                            {"display", display_}}));
  EXPECT_EQ(strip_, WidgetShape::ForStruct({{"display", display_},
                                            {"fader", fader_},
                                            {"select", button_}}));
  EXPECT_NE(strip_, WidgetShape::ForStruct({{"fader", fader_},
                                            {"select", button_},
                                            {"display", button_}}));
  EXPECT_NE(strip_, WidgetShape::ForStruct({{"fader", fader_},
                                            {"select", button_},
                                            {"screen", display_}}));
  EXPECT_NE(strip_,
            WidgetShape::ForStruct({{"fader", fader_}, {"select", button_}}));
  EXPECT_NE(strip_, WidgetShape::ForStruct({{"fader", fader_},
                                            {"select", button_},
                                            {"display", display_},
                                            {"mute", button_}}));
}

TEST_F(WidgetShapeTest, ArraysAreEqualWithTheSameSizeAndElement) {
  EXPECT_EQ(WidgetShape::ForArray(strip_, 4), WidgetShape::ForArray(strip_, 4));
  EXPECT_NE(WidgetShape::ForArray(strip_, 4), WidgetShape::ForArray(strip_, 8));
  EXPECT_NE(WidgetShape::ForArray(strip_, 4),
            WidgetShape::ForArray(button_, 4));
}

TEST_F(WidgetShapeTest, ShapesOfDifferentKindsArentEqual) {
  EXPECT_NE(WidgetShape::ForStruct({}), WidgetShape::ForArray(button_, 0));
  EXPECT_NE(WidgetShape::ForStruct({}), WidgetShape::ForControl({}, {}));
  EXPECT_NE(WidgetShape::ForArray(button_, 0), WidgetShape::ForControl({}, {}));
}

TEST_F(WidgetShapeTest, CopiesAreEqual) {
  const WidgetShape copy = desk_;
  EXPECT_EQ(copy, desk_);
  EXPECT_EQ(copy.Find(Path("strip[2].select")),
            desk_.Find(Path("strip[2].select")));
}

TEST_F(WidgetShapeTest, StructKeepsBadAndRepeatedNames) {
  const WidgetShape shape = WidgetShape::ForStruct(
      {{"play", button_}, {"Stop", button_}, {"play", fader_}, {"", button_}});
  ASSERT_EQ(std::ssize(shape.GetFields()), 4);
  EXPECT_EQ(shape.GetFields()[1].name, "Stop");
  EXPECT_EQ(shape.GetFields()[2].name, "play");
  EXPECT_EQ(shape.GetFields()[3].name, "");

  const WidgetShape* play = shape.GetField("play");
  ASSERT_NE(play, nullptr);
  EXPECT_EQ(*play, button_);
}

TEST_F(WidgetShapeTest, ControlKeepsBadAndRepeatedModeNames) {
  const WidgetShape shape = WidgetShape::ForControl(
      {kPress}, {kDValue}, {"solid", "Blink", "solid", "off"});
  EXPECT_THAT(shape.GetModeNames(),
              ElementsAre("solid", "Blink", "solid", "off"));
}

TEST_F(WidgetShapeTest, NegativeArraySizeIsZero) {
  EXPECT_EQ(WidgetShape::ForArray(button_, -1).GetSize(), 0);
}

}  // namespace
}  // namespace jpr
