// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_override.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kDelta = ControlInput::Type::kDelta;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;
constexpr ControlOutput::Type kColor = ControlOutput::Type::kColor;

class ValidateControlOverridesTest : public ::testing::Test {
 protected:
  // Returns the errors validating the overrides of device "xtouch" against the
  // catalog adds, checking that it returns whether there are none.
  std::vector<std::string> Validate(std::vector<ControlOverride> overrides) {
    std::vector<std::string> errors;
    const bool valid =
        ValidateControlOverrides(catalog_, overrides, "xtouch", &errors);
    EXPECT_EQ(valid, errors.empty());
    return errors;
  }

  const WidgetShape button_ =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape fader_ =
      WidgetShape::ForControl({kValue, kPress}, {kCValue});
  const WidgetShape strip_ =
      WidgetShape::ForStruct({{"fader", fader_}, {"select", button_}});
  const WidgetShape catalog_ = WidgetShape::ForStruct(
      {{"play", button_}, {"strip", WidgetShape::ForArray(strip_, 4)}});
};

TEST_F(ValidateControlOverridesTest, OverridesOfWhatControlsHaveAreValid) {
  EXPECT_THAT(Validate({
                  {.control = "strip[2].fader", .inputs = kPress},
                  {.control = "strip[3].fader",
                   .inputs = {kValue, kPress},
                   .outputs = kCValue},
                  {.control = "play", .outputs = kDValue},
              }),
              IsEmpty());
}

TEST_F(ValidateControlOverridesTest, NoOverridesAreValid) {
  EXPECT_THAT(Validate({}), IsEmpty());
}

TEST_F(ValidateControlOverridesTest, PathsMayHaveSpaces) {
  EXPECT_THAT(Validate({{.control = "strip [2] . fader", .inputs = kPress}}),
              IsEmpty());
}

TEST_F(ValidateControlOverridesTest, ControlThatIsntAPathIsAnError) {
  const std::vector<std::string> errors =
      Validate({{.control = "strip.[2]", .inputs = kPress}});
  ASSERT_EQ(errors.size(), 1);
  EXPECT_THAT(errors[0],
              ::testing::StartsWith("xtouch: strip.[2]: isn't a path: "));
}

TEST_F(ValidateControlOverridesTest, ControlThatNamesNothingIsAnError) {
  EXPECT_THAT(Validate({
                  {.control = "stop", .inputs = kPress},
                  {.control = "strip[4].fader", .inputs = kPress},
              }),
              ElementsAre("xtouch: stop: names nothing",
                          "xtouch: strip[4].fader: names nothing"));
}

TEST_F(ValidateControlOverridesTest, ControlThatIsntAControlIsAnError) {
  EXPECT_THAT(Validate({
                  {.control = "strip[2]", .inputs = kPress},
                  {.control = "strip", .inputs = kPress},
              }),
              ElementsAre("xtouch: strip[2]: isn't a control",
                          "xtouch: strip: isn't a control"));
}

TEST_F(ValidateControlOverridesTest, ControlOverriddenTwiceIsAnError) {
  EXPECT_THAT(
      Validate({
          {.control = "strip[2].fader", .inputs = kPress},
          {.control = "strip[2] .fader", .outputs = kCValue},
      }),
      ElementsAre("xtouch: strip[2] .fader: is overridden more than once"));
}

TEST_F(ValidateControlOverridesTest, OverrideThatDropsNothingIsAnError) {
  EXPECT_THAT(Validate({{.control = "play"}}),
              ElementsAre("xtouch: play: drops no inputs or outputs"));
}

TEST_F(ValidateControlOverridesTest, DroppingWhatAControlDoesntHaveIsAnError) {
  EXPECT_THAT(Validate({
                  {.control = "play",
                   .inputs = {kValue, kDelta, kPress},
                   .outputs = {kDValue, kColor}},
              }),
              ElementsAre("xtouch: play: has no value input",
                          "xtouch: play: has no delta input",
                          "xtouch: play: has no color output"));
}

TEST_F(ValidateControlOverridesTest, ErrorsStartWithTheirLine) {
  EXPECT_THAT(Validate({{.control = "play"},
                        {.control = "stop", .inputs = kPress, .line = {7}}}),
              ElementsAre("xtouch: play: drops no inputs or outputs",
                          "line 7: xtouch: stop: names nothing"));
}

TEST_F(ValidateControlOverridesTest, ErrorsAreOptional) {
  EXPECT_FALSE(ValidateControlOverrides(catalog_, {{.control = "stop"}},
                                        "xtouch",
                                        /*errors=*/nullptr));
  EXPECT_TRUE(ValidateControlOverrides(
      catalog_, {{.control = "play", .inputs = kPress}}, "xtouch",
      /*errors=*/nullptr));
}

TEST(GetOverridesByControlTest,
     CombinesEachControlsOverridesWhereFirstWritten) {
  const auto by_control = GetOverridesByControl({
      {.control = "strip[2].fader", .inputs = kPress, .line = {4}},
      {.control = "play", .outputs = kDValue},
      {.control = "strip[2] . fader", .outputs = kCValue, .line = {5}},
      {.control = "strip.[2]", .inputs = kPress},
  });
  ASSERT_EQ(by_control.size(), 2);
  const ControlOverride& fader = by_control.at("strip[2].fader");
  EXPECT_EQ(fader, (ControlOverride{.control = "strip[2].fader",
                                    .inputs = kPress,
                                    .outputs = kCValue}));
  EXPECT_EQ(fader.line.number, 4);
  EXPECT_EQ(by_control.at("play"),
            (ControlOverride{.control = "play", .outputs = kDValue}));
}

}  // namespace
}  // namespace jpr
