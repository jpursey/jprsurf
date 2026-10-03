// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_handle.h"

#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/fake_control_io.h"

namespace jpr {
namespace {

// A control clears its outputs on the next run once it has no writers, so its
// text output shows whether a handle is still registered.
class ControlOutputHandleTest : public ::testing::Test {
 protected:
  ControlOutputHandleTest() {
    auto output = std::make_unique<FakeTextOutput>();
    output_ = output.get();
    control_ = std::make_unique<Control>(
        runner_,
        Control::Options{.name = "Scribble", .text_output = std::move(output)});
  }

  // Runs the control, and returns its text.
  const std::string& RunAndGetText() {
    time_ += 1.0;
    runner_.Run({.precise = time_, .coarse = 0});
    return output_->GetText();
  }

  Runner runner_{"Controls"};
  std::unique_ptr<Control> control_;
  FakeTextOutput* output_ = nullptr;
  double time_ = 1000.0;
};

TEST_F(ControlOutputHandleTest, DefaultHandleIsntRegistered) {
  ControlOutputHandle handle;
  EXPECT_FALSE(handle.IsRegistered());
}

TEST_F(ControlOutputHandleTest, DestroyingAHandleUnregistersIt) {
  {
    ControlOutputHandle handle = control_->RegisterOutputWriter();
    EXPECT_TRUE(handle.IsRegistered());
    control_->SetText("Bass");
    EXPECT_EQ(RunAndGetText(), "Bass");
  }
  EXPECT_EQ(RunAndGetText(), "");
}

TEST_F(ControlOutputHandleTest, MovingAHandleKeepsItsRegistration) {
  ControlOutputHandle handle = control_->RegisterOutputWriter();
  control_->SetText("Bass");

  ControlOutputHandle moved(std::move(handle));
  EXPECT_FALSE(handle.IsRegistered());
  EXPECT_TRUE(moved.IsRegistered());
  EXPECT_EQ(RunAndGetText(), "Bass");

  ControlOutputHandle assigned;
  assigned = std::move(moved);
  EXPECT_FALSE(moved.IsRegistered());
  EXPECT_TRUE(assigned.IsRegistered());
  EXPECT_EQ(RunAndGetText(), "Bass");

  assigned = ControlOutputHandle();
  EXPECT_EQ(RunAndGetText(), "");
}

TEST_F(ControlOutputHandleTest, AssigningAHandleUnregistersItsLast) {
  ControlOutputHandle first = control_->RegisterOutputWriter();
  ControlOutputHandle second = control_->RegisterOutputWriter();
  control_->SetText("Bass");

  // Only the second is left, so the outputs aren't cleared.
  first = std::move(second);
  EXPECT_EQ(RunAndGetText(), "Bass");
  first = ControlOutputHandle();
  EXPECT_EQ(RunAndGetText(), "");
}

}  // namespace
}  // namespace jpr
