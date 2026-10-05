// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input.h"

#include "gtest/gtest.h"
#include "jpr/device/testing/fake_control_io.h"

namespace jpr {
namespace {

//==============================================================================
// ControlValueInput
//==============================================================================

TEST(ControlValueInputTest, ValueStartsAtZero) {
  FakeValueInput input;
  EXPECT_EQ(input.GetType(), ControlInput::Type::kValue);
  EXPECT_EQ(input.GetValue(), 0.0);
  EXPECT_FALSE(input.HasListener());
}

TEST(ControlValueInputTest, ListenerIsCalledForEachValue) {
  FakeValueInput input;
  int calls = 0;
  double heard = 0.0;
  input.SetListener([&](ControlValueInput* changed) {
    EXPECT_EQ(changed, &input);
    ++calls;
    heard = changed->GetValue();
  });
  EXPECT_TRUE(input.HasListener());

  input.SetValue(0.25);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(heard, 0.25);
  EXPECT_EQ(input.GetValue(), 0.25);

  // The same value again is still input.
  input.SetValue(0.25);
  EXPECT_EQ(calls, 2);
}

TEST(ControlValueInputTest, ValueChangesWithoutAListener) {
  FakeValueInput input;
  int calls = 0;
  input.SetListener([&](ControlValueInput*) { ++calls; });
  input.SetListener(nullptr);
  EXPECT_FALSE(input.HasListener());

  input.SetValue(0.5);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(input.GetValue(), 0.5);
}

//==============================================================================
// ControlDeltaInput
//==============================================================================

TEST(ControlDeltaInputTest, DeltaStartsAtZero) {
  FakeDeltaInput input;
  EXPECT_EQ(input.GetType(), ControlInput::Type::kDelta);
  EXPECT_EQ(input.PeekDelta(), 0.0);
  EXPECT_FALSE(input.HasListener());
}

TEST(ControlDeltaInputTest, DeltasAddUpUntilRead) {
  FakeDeltaInput input;
  int calls = 0;
  input.SetListener([&](ControlDeltaInput* changed) {
    EXPECT_EQ(changed, &input);
    ++calls;
  });

  input.AddDelta(0.25);
  input.AddDelta(-0.5);
  EXPECT_EQ(calls, 2);
  // Peeking leaves the delta, and reading takes it.
  EXPECT_EQ(input.PeekDelta(), -0.25);
  EXPECT_EQ(input.ReadDelta(), -0.25);
  EXPECT_EQ(input.PeekDelta(), 0.0);

  input.AddDelta(1.0);
  input.ResetDelta();
  EXPECT_EQ(input.ReadDelta(), 0.0);
}

TEST(ControlDeltaInputTest, DeltasAddUpWithoutAListener) {
  FakeDeltaInput input;
  input.AddDelta(0.25);
  input.AddDelta(0.25);
  EXPECT_EQ(input.ReadDelta(), 0.5);
}

//==============================================================================
// ControlPressInput
//==============================================================================

TEST(ControlPressInputTest, PressStartsReleased) {
  FakePressInput input;
  EXPECT_EQ(input.GetType(), ControlInput::Type::kPress);
  EXPECT_TRUE(input.HasRelease());
  EXPECT_FALSE(input.IsPressed());
  EXPECT_EQ(input.GetPressCount(), 0);
  EXPECT_EQ(input.GetReleaseCount(), 0);
  EXPECT_FALSE(input.HasListener());
}

TEST(ControlPressInputTest, PressIsHeldUntilReleased) {
  FakePressInput input;
  int calls = 0;
  input.SetListener([&](ControlPressInput* changed) {
    EXPECT_EQ(changed, &input);
    ++calls;
  });

  input.Press();
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(input.IsPressed());
  EXPECT_EQ(input.GetPressCount(), 1);

  input.Release();
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(input.IsPressed());
  EXPECT_EQ(input.GetReleaseCount(), 1);

  input.Press();
  input.Release();
  EXPECT_EQ(calls, 4);
  EXPECT_EQ(input.GetPressCount(), 2);
  EXPECT_EQ(input.GetReleaseCount(), 2);
}

// A press while pressed, or a release while released, isn't input.
TEST(ControlPressInputTest, RepeatedPressOrReleaseIsIgnored) {
  FakePressInput input;
  int calls = 0;
  input.SetListener([&](ControlPressInput*) { ++calls; });

  input.Release();
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(input.GetReleaseCount(), 0);

  input.Press();
  input.Press();
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(input.GetPressCount(), 1);

  input.Release();
  input.Release();
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(input.GetReleaseCount(), 1);
}

TEST(ControlPressInputTest, ResettingCountsKeepsThePress) {
  FakePressInput input;
  input.Press();
  input.ResetCounts();
  EXPECT_EQ(input.GetPressCount(), 0);
  EXPECT_TRUE(input.IsPressed());

  input.Release();
  EXPECT_EQ(input.GetReleaseCount(), 1);
}

// Without release, each press is input, and the input is never pressed.
TEST(ControlPressInputTest, PressWithoutReleaseIsNeverHeld) {
  FakePressInput input(/*has_release=*/false);
  int calls = 0;
  input.SetListener([&](ControlPressInput*) { ++calls; });
  EXPECT_FALSE(input.HasRelease());

  input.Press();
  input.Press();
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(input.GetPressCount(), 2);
  EXPECT_FALSE(input.IsPressed());

  input.Release();
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(input.GetReleaseCount(), 0);
}

}  // namespace
}  // namespace jpr
