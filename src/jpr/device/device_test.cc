// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device.h"

#include <iterator>
#include <memory>
#include <string_view>

#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/fake_control_io.h"

namespace jpr {
namespace {

// A device that adds whatever controls a test gives it.
class TestDevice final : public Device {
 public:
  explicit TestDevice(RunRegistry& run_registry) : Device(run_registry) {}

  using Device::AddControl;
};

// Returns the options for a control named `name`, with a text output.
Control::Options MakeOptions(std::string_view name) {
  return {.name = name, .text_output = std::make_unique<FakeTextOutput>()};
}

class DeviceTest : public ::testing::Test {
 protected:
  Runner runner_{"Controls"};
  TestDevice device_{runner_};
};

TEST_F(DeviceTest, FindsControlsByName) {
  device_.AddControl(MakeOptions("Play"));
  device_.AddControl(MakeOptions("Stop"));

  Control* play = device_.GetControl("Play");
  ASSERT_NE(play, nullptr);
  EXPECT_EQ(play->GetName(), "Play");
  Control* stop = device_.GetControl("Stop");
  ASSERT_NE(stop, nullptr);
  EXPECT_EQ(stop->GetName(), "Stop");
  EXPECT_EQ(device_.GetControl("Record"), nullptr);
}

TEST_F(DeviceTest, ListsControlsInTheOrderAdded) {
  device_.AddControl(MakeOptions("Stop"));
  device_.AddControl(MakeOptions("Play"));

  ASSERT_EQ(std::ssize(device_.GetControls()), 2);
  EXPECT_EQ(device_.GetControls()[0]->GetName(), "Stop");
  EXPECT_EQ(device_.GetControls()[1]->GetName(), "Play");
}

TEST_F(DeviceTest, ControlWithATakenNameIsntAdded) {
  device_.AddControl(MakeOptions("Play"));
  Control* play = device_.GetControl("Play");
  device_.AddControl(MakeOptions("Play"));

  EXPECT_EQ(std::ssize(device_.GetControls()), 1);
  EXPECT_EQ(device_.GetControl("Play"), play);
}

}  // namespace
}  // namespace jpr
