// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device.h"

#include <iterator>

#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_override.h"
#include "jpr/device/testing/fake_device.h"

namespace jpr {
namespace {

class DeviceTest : public ::testing::Test {
 protected:
  Runner runner_{"Controls"};
  FakeDevice device_{runner_};
};

TEST_F(DeviceTest, FindsControlsByName) {
  device_.AddButton("Play");
  device_.AddButton("Stop");

  Control* play = device_.GetControl("Play");
  ASSERT_NE(play, nullptr);
  EXPECT_EQ(play->GetName(), "Play");
  Control* stop = device_.GetControl("Stop");
  ASSERT_NE(stop, nullptr);
  EXPECT_EQ(stop->GetName(), "Stop");
  EXPECT_EQ(device_.GetControl("Record"), nullptr);
}

TEST_F(DeviceTest, ListsControlsInTheOrderAdded) {
  device_.AddButton("Stop");
  device_.AddButton("Play");

  ASSERT_EQ(std::ssize(device_.GetControls()), 2);
  EXPECT_EQ(device_.GetControls()[0]->GetName(), "Stop");
  EXPECT_EQ(device_.GetControls()[1]->GetName(), "Play");
}

TEST_F(DeviceTest, ControlWithATakenNameIsntAdded) {
  Control* play = device_.AddButton("Play").control;
  ASSERT_NE(play, nullptr);
  EXPECT_EQ(device_.AddButton("Play").control, nullptr);

  EXPECT_EQ(std::ssize(device_.GetControls()), 1);
  EXPECT_EQ(device_.GetControl("Play"), play);
}

// Each control is named by its path, as overrides name it.
TEST(DeviceOverrideTest, OverriddenInputsAndOutputsAreDropped) {
  Runner runner("Controls");
  const ControlOverride overrides[] = {
      {.control = "strip[2].fader", .inputs = ControlInput::Type::kPress},
      {.control = "play", .outputs = ControlOutput::Type::kDValue},
  };
  FakeDevice device(runner, overrides);

  FakeDevice::FakeControl fader = device.AddFader("strip[2].fader");
  ASSERT_NE(fader.control, nullptr);
  EXPECT_EQ(fader.control->GetInputs(),
            Control::Inputs(ControlInput::Type::kValue));
  EXPECT_EQ(fader.control->GetOutputs(),
            Control::Outputs(ControlOutput::Type::kCValue));
  EXPECT_NE(fader.value_input, nullptr);
  EXPECT_EQ(fader.press_input, nullptr);

  FakeDevice::FakeControl play = device.AddButton("play");
  ASSERT_NE(play.control, nullptr);
  EXPECT_EQ(play.control->GetInputs(),
            Control::Inputs(ControlInput::Type::kPress));
  EXPECT_TRUE(play.control->GetOutputs().IsEmpty());
  EXPECT_EQ(play.dvalue_output, nullptr);

  // Controls without an override keep everything.
  FakeDevice::FakeControl other = device.AddFader("strip[1].fader");
  ASSERT_NE(other.control, nullptr);
  EXPECT_EQ(other.control->GetInputs(),
            Control::Inputs(
                {ControlInput::Type::kValue, ControlInput::Type::kPress}));
}

TEST(DeviceOverrideTest, OverridesFindControlsHoweverTheyAreWritten) {
  Runner runner("Controls");
  const ControlOverride overrides[] = {
      {.control = "strip [2] . fader", .inputs = ControlInput::Type::kPress},
  };
  FakeDevice device(runner, overrides);

  Control* fader = device.AddFader("strip[2].fader").control;
  ASSERT_NE(fader, nullptr);
  EXPECT_EQ(fader->GetInputs(), Control::Inputs(ControlInput::Type::kValue));
}

TEST(DeviceOverrideTest, OverridesOfOneControlAreCombined) {
  Runner runner("Controls");
  const ControlOverride overrides[] = {
      {.control = "strip[2].fader", .inputs = ControlInput::Type::kPress},
      {.control = "strip[2].fader", .outputs = ControlOutput::Type::kCValue},
  };
  FakeDevice device(runner, overrides);

  Control* fader = device.AddFader("strip[2].fader").control;
  ASSERT_NE(fader, nullptr);
  EXPECT_EQ(fader->GetInputs(), Control::Inputs(ControlInput::Type::kValue));
  EXPECT_TRUE(fader->GetOutputs().IsEmpty());
}

}  // namespace
}  // namespace jpr
