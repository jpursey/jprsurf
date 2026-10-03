// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input_handle.h"

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/fake_control_io.h"

namespace jpr {
namespace {

// A control listens to its value input only while a registration reads it, so
// the input's listener shows whether a handle is still registered.
class ControlInputHandleTest : public ::testing::Test {
 protected:
  ControlInputHandleTest() {
    auto input = std::make_unique<FakeValueInput>();
    input_ = input.get();
    control_ = std::make_unique<Control>(
        runner_,
        Control::Options{.name = "Fader", .value_input = std::move(input)});
  }

  // Registers a value input on the control.
  ControlInputHandle Register() {
    return control_->RegisterInput({.input_type = ControlInput::Type::kValue},
                                   &input_changed_);
  }

  Runner runner_{"Controls"};
  std::unique_ptr<Control> control_;
  FakeValueInput* input_ = nullptr;
  bool input_changed_ = false;
};

TEST_F(ControlInputHandleTest, DefaultHandleIsntRegistered) {
  ControlInputHandle handle;
  EXPECT_FALSE(handle.IsRegistered());
}

TEST_F(ControlInputHandleTest, DestroyingAHandleUnregistersIt) {
  {
    ControlInputHandle handle = Register();
    EXPECT_TRUE(handle.IsRegistered());
    EXPECT_NE(handle.GetId(), 0);
    EXPECT_TRUE(input_->HasListener());
  }
  EXPECT_FALSE(input_->HasListener());
}

TEST_F(ControlInputHandleTest, MovingAHandleKeepsItsRegistration) {
  ControlInputHandle handle = Register();
  const InputId id = handle.GetId();

  ControlInputHandle moved(std::move(handle));
  EXPECT_FALSE(handle.IsRegistered());
  EXPECT_TRUE(moved.IsRegistered());
  EXPECT_EQ(moved.GetId(), id);
  EXPECT_TRUE(input_->HasListener());

  ControlInputHandle assigned;
  assigned = std::move(moved);
  EXPECT_FALSE(moved.IsRegistered());
  EXPECT_EQ(assigned.GetId(), id);
  EXPECT_TRUE(input_->HasListener());

  assigned = ControlInputHandle();
  EXPECT_FALSE(input_->HasListener());
}

TEST_F(ControlInputHandleTest, AssigningAHandleUnregistersItsLast) {
  ControlInputHandle first = Register();
  ControlInputHandle second = Register();
  const InputId second_id = second.GetId();

  // Only the second is left, so the input is still read, until it goes too.
  first = std::move(second);
  EXPECT_EQ(first.GetId(), second_id);
  EXPECT_TRUE(input_->HasListener());
  first = ControlInputHandle();
  EXPECT_FALSE(input_->HasListener());
}

}  // namespace
}  // namespace jpr
