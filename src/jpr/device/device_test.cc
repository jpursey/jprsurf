// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device.h"

#include <iterator>

#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
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

}  // namespace
}  // namespace jpr
