// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/testing/fake_device.h"

#include <iterator>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output.h"
#include "jpr/device/testing/fake_control_io.h"

namespace jpr {
namespace {

using Binding = Control::Binding;
using InputType = ControlInput::Type;
using Inputs = Control::Inputs;
using OutputType = ControlOutput::Type;
using Outputs = Control::Outputs;

class FakeDeviceTest : public ::testing::Test {
 protected:
  Runner runner_{"Controls"};
  FakeDevice device_{runner_};
};

TEST_F(FakeDeviceTest, ControlsAreMadeOfTheFakes) {
  FakeDevice::FakeControl fakes = device_.AddControl({
      .name = "Knob",
      .value_input = std::make_unique<FakeValueInput>(),
      .delta_input = std::make_unique<FakeDeltaInput>(),
      .press_input = std::make_unique<FakePressInput>(/*has_release=*/false),
      .binding = Binding::kDependent,
      .dvalue_output = std::make_unique<FakeDValueOutput>(std::vector{1, 3}),
  });

  Control* control = fakes.control;
  ASSERT_NE(control, nullptr);
  EXPECT_EQ(device_.GetControl("Knob"), control);
  EXPECT_EQ(control->GetInputs(),
            Inputs({InputType::kValue, InputType::kDelta, InputType::kPress}));
  EXPECT_FALSE(control->HasPressRelease());
  EXPECT_EQ(control->GetBinding(), Binding::kDependent);
  EXPECT_EQ(control->GetOutputs(), OutputType::kDValue);
  EXPECT_NE(fakes.value_input, nullptr);
  EXPECT_NE(fakes.delta_input, nullptr);
  EXPECT_NE(fakes.press_input, nullptr);
  EXPECT_NE(fakes.dvalue_output, nullptr);
  EXPECT_EQ(fakes.cvalue_output, nullptr);
  EXPECT_EQ(fakes.text_output, nullptr);
  EXPECT_EQ(fakes.color_output, nullptr);
}

TEST_F(FakeDeviceTest, ATakenNameAddsNothing) {
  FakeDevice::FakeControl play = device_.AddButton("Play");
  FakeDevice::FakeControl again = device_.AddDisplay("Play");

  EXPECT_EQ(again.control, nullptr);
  EXPECT_EQ(again.text_output, nullptr);
  EXPECT_EQ(again.color_output, nullptr);
  EXPECT_EQ(std::ssize(device_.GetControls()), 1);
  EXPECT_EQ(device_.GetControl("Play"), play.control);
}

TEST_F(FakeDeviceTest, AButtonPressesAndLights) {
  FakeDevice::FakeControl button = device_.AddButton("Play");

  EXPECT_EQ(button.control->GetInputs(), InputType::kPress);
  EXPECT_TRUE(button.control->HasPressRelease());
  EXPECT_EQ(button.control->GetOutputs(), OutputType::kDValue);
  EXPECT_EQ(button.control->GetBinding(), Binding::kIndependent);

  bool changed = false;
  ControlInputHandle input = button.control->RegisterInput(
      {.input_type = InputType::kPress}, &changed);
  button.press_input->Press();
  EXPECT_TRUE(changed);
  EXPECT_TRUE(button.control->IsPressed(input.GetId()));

  button.control->SetDValue(1);
  EXPECT_EQ(button.dvalue_output->GetValue(), 1);
}

TEST_F(FakeDeviceTest, AFaderMovesAndIsTouched) {
  FakeDevice::FakeControl fader = device_.AddFader("Fader");

  EXPECT_EQ(fader.control->GetInputs(),
            Inputs({InputType::kValue, InputType::kPress}));
  EXPECT_TRUE(fader.control->HasPressRelease());
  EXPECT_EQ(fader.control->GetOutputs(), OutputType::kCValue);
  EXPECT_EQ(fader.control->GetBinding(), Binding::kMotorized);
}

TEST_F(FakeDeviceTest, APotTurnsAndHasARing) {
  FakeDevice::FakeControl pot = device_.AddPot("Pot");

  EXPECT_EQ(pot.control->GetInputs(), InputType::kDelta);
  EXPECT_EQ(pot.control->GetOutputs(), OutputType::kCValue);
  EXPECT_EQ(pot.control->GetBinding(), Binding::kIndependent);
}

TEST_F(FakeDeviceTest, ADisplayShowsTextAndColor) {
  FakeDevice::FakeControl display = device_.AddDisplay("Scribble");

  EXPECT_EQ(display.control->GetInputs(), Inputs());
  EXPECT_EQ(display.control->GetOutputs(),
            Outputs({OutputType::kText, OutputType::kColor}));
  display.control->SetText("Drums");
  display.control->SetColor(Color{255, 0, 0});
  EXPECT_EQ(display.text_output->GetText(), "Drums");
  EXPECT_EQ(display.color_output->GetColor(), (Color{255, 0, 0}));
}

}  // namespace
}  // namespace jpr
