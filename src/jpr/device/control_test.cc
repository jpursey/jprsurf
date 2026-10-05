// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output_handle.h"
#include "jpr/device/testing/fake_control_io.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Optional;

using Binding = Control::Binding;
using PressBehavior = InputConfig::PressBehavior;

// One run, for RunFor().
const absl::Duration kRunTime = FakeReaper::GetRunTime();

// Control waits 0.35 seconds for a long press, 0.15 seconds for a double
// press's second press, and after input, 0.125 seconds to send a dependent
// output and 0.375 seconds to send a motorized one. Tests check a run or more
// either side of each.

//==============================================================================
// Test fixture
//
// Controls are made of fake inputs and outputs, and run as a device's are: each
// run, the controls run, then input arrives.
//==============================================================================

class ControlTest : public ::testing::Test {
 protected:
  ControlTest() { ResetModifiers(); }
  ~ControlTest() override { ResetModifiers(); }

  //----------------------------------------------------------------------------
  // Making controls
  //
  // Each Add*() adds a fake to the next control Create() makes, and returns it.
  //----------------------------------------------------------------------------

  FakePressInput* AddPressInput(bool has_release = true) {
    return Add<FakePressInput>(options_.press_input, has_release);
  }
  FakeValueInput* AddValueInput() {
    return Add<FakeValueInput>(options_.value_input);
  }
  FakeDeltaInput* AddDeltaInput() {
    return Add<FakeDeltaInput>(options_.delta_input);
  }
  FakeCValueOutput* AddCValueOutput(int mode_count = 1) {
    return Add<FakeCValueOutput>(options_.cvalue_output, mode_count);
  }
  FakeDValueOutput* AddDValueOutput(absl::Span<const int> max_values = {1}) {
    return Add<FakeDValueOutput>(options_.dvalue_output, max_values);
  }
  FakeTextOutput* AddTextOutput(int mode_count = 1) {
    return Add<FakeTextOutput>(options_.text_output, mode_count);
  }
  FakeColorOutput* AddColorOutput(int mode_count = 1) {
    return Add<FakeColorOutput>(options_.color_output, mode_count);
  }

  // Creates a control of what was added since the last one.
  Control* Create(Binding binding = Binding::kIndependent) {
    options_.name = "Control";
    options_.binding = binding;
    controls_.push_back(
        std::make_unique<Control>(runner_, std::move(options_)));
    options_ = Control::Options();
    return controls_.back().get();
  }

  //----------------------------------------------------------------------------
  // Input
  //
  // Each arrives on the next run, after the controls run, as a device's does.
  //----------------------------------------------------------------------------

  void Press(FakePressInput* input) {
    inputs_.push_back([input] { input->Press(); });
  }
  void Release(FakePressInput* input) {
    inputs_.push_back([input] { input->Release(); });
  }
  void Move(FakeValueInput* input, double value) {
    inputs_.push_back([input, value] { input->SetValue(value); });
  }
  void Turn(FakeDeltaInput* input, double delta) {
    inputs_.push_back([input, delta] { input->AddDelta(delta); });
  }

  // Registers an input of `type` on `control`, with `modifiers` (and
  // `behavior`, for a press), to read it.
  ControlInputHandle Read(Control* control, ControlInput::Type type,
                          Modifiers modifiers = 0,
                          PressBehavior behavior = PressBehavior::kNormal) {
    return control->RegisterInput({.input_type = type,
                                   .required_modifiers = modifiers,
                                   .press_behavior = behavior},
                                  &input_changed_);
  }

  //----------------------------------------------------------------------------
  // Running
  //----------------------------------------------------------------------------

  // Advances the clock by one run, runs the controls, and then delivers the
  // input since the last run.
  void Run() {
    ++run_count_;
    const double time = kStartTime + static_cast<double>(run_count_) /
                                         FakeReaper::kRunsPerSecond;
    runner_.Run(
        {.precise = time, .coarse = static_cast<unsigned int>(time * 1000.0)});
    for (absl::AnyInvocable<void()>& input : inputs_) {
      input();
    }
    inputs_.clear();
  }

  //----------------------------------------------------------------------------
  // Presses
  //----------------------------------------------------------------------------

  // Registers a press input on `control`, and returns its index in what
  // RunFor(), Tap(), and GetPressed() return.
  int AddPress(Control* control, PressBehavior behavior,
               Modifiers modifiers = 0) {
    presses_.push_back({control, Read(control, ControlInput::Type::kPress,
                                      modifiers, behavior)});
    return static_cast<int>(presses_.size()) - 1;
  }

  // Runs for `duration`, rounded up to whole runs, and returns how many
  // presses each input from AddPress() delivered in that time.
  std::vector<int> RunFor(absl::Duration duration) {
    std::vector<int> counts(presses_.size(), 0);
    const int runs = FakeReaper::GetRunCount(duration);
    for (int run = 0; run < runs; ++run) {
      RunAndCount(counts);
    }
    return counts;
  }

  // Presses `input` for one run, and releases it on the next, and returns how
  // many presses each input from AddPress() delivered in that time.
  std::vector<int> Tap(FakePressInput* input) {
    std::vector<int> counts(presses_.size(), 0);
    Press(input);
    RunAndCount(counts);
    Release(input);
    RunAndCount(counts);
    return counts;
  }

  // Returns whether each input from AddPress() is pressed.
  std::vector<bool> GetPressed() const {
    std::vector<bool> pressed;
    for (const PressRead& press : presses_) {
      pressed.push_back(press.control->IsPressed(press.handle.GetId()));
    }
    return pressed;
  }

 private:
  // The clock's time before the first run. Any will do, but REAPER's doesn't
  // start at zero either.
  static constexpr double kStartTime = 1000.0;

  struct PressRead {
    Control* control;
    ControlInputHandle handle;
  };

  // Makes a `Fake` from `args`, puts it in `slot`, and returns it.
  template <typename Fake, typename Base, typename... Args>
  Fake* Add(std::unique_ptr<Base>& slot, Args&&... args) {
    auto fake = std::make_unique<Fake>(std::forward<Args>(args)...);
    Fake* result = fake.get();
    slot = std::move(fake);
    return result;
  }

  // Runs once, and adds the presses each input delivered to `counts`.
  void RunAndCount(std::vector<int>& counts) {
    Run();
    for (int i = 0; i < static_cast<int>(presses_.size()); ++i) {
      counts[i] +=
          presses_[i].control->GetPressCount(presses_[i].handle.GetId());
    }
  }

  Runner runner_{"Controls"};
  int run_count_ = 0;
  Control::Options options_;
  std::vector<std::unique_ptr<Control>> controls_;
  std::vector<PressRead> presses_;
  std::vector<absl::AnyInvocable<void()>> inputs_;
  bool input_changed_ = false;  // For RegisterInput(), and no test reads it.
};

//==============================================================================
// Press timing
//==============================================================================

TEST_F(ControlTest, PressIsDeliveredAtOnceAndHeld) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1));
  EXPECT_THAT(GetPressed(), ElementsAre(true));
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0));
  EXPECT_THAT(GetPressed(), ElementsAre(true));

  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0));
  EXPECT_THAT(GetPressed(), ElementsAre(false));
}

TEST_F(ControlTest, LongPressIsDeliveredOnceHeld) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kLongPress);

  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(300)), ElementsAre(0));
  EXPECT_THAT(GetPressed(), ElementsAre(false));
  EXPECT_THAT(RunFor(absl::Milliseconds(100)), ElementsAre(1));
  EXPECT_THAT(GetPressed(), ElementsAre(true));

  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0));
  EXPECT_THAT(GetPressed(), ElementsAre(false));

  // A short press isn't a long press.
  EXPECT_THAT(Tap(button), ElementsAre(0));
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0));
}

TEST_F(ControlTest, DoublePressIsDeliveredOnTheSecondPress) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kDoublePress);

  EXPECT_THAT(Tap(button), ElementsAre(0));
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1));
  EXPECT_THAT(GetPressed(), ElementsAre(false));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0));

  // A second press too long after the first isn't a double press.
  EXPECT_THAT(Tap(button), ElementsAre(0));
  EXPECT_THAT(RunFor(absl::Milliseconds(300)), ElementsAre(0));
  EXPECT_THAT(Tap(button), ElementsAre(0));
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0));
}

TEST_F(ControlTest, TapIsDeliveredWhenAShortPressIsReleased) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kTap);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0));
  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1));
  EXPECT_THAT(GetPressed(), ElementsAre(false));

  // Held as long as a long press, it isn't a tap.
  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(400)), ElementsAre(0));
  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0));
}

// With a long press beside it, a press waits to see which it is.
TEST_F(ControlTest, PressAndLongPressShareAButton) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kLongPress);

  // A short press is a press, on its release, and is never held.
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));
  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));

  // A long one is a long press, held until it is released.
  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(400)), ElementsAre(0, 1));
  EXPECT_THAT(GetPressed(), ElementsAre(false, true));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));
}

// With a double press beside it, a press waits to see if a second follows.
TEST_F(ControlTest, PressAndDoublePressShareAButton) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kDoublePress);

  // A single press is a press, once no second press can follow.
  EXPECT_THAT(Tap(button), ElementsAre(0, 0));
  EXPECT_THAT(RunFor(absl::Milliseconds(100)), ElementsAre(0, 0));
  EXPECT_THAT(RunFor(absl::Milliseconds(100)), ElementsAre(1, 0));

  // Two are a double press, and not a press.
  EXPECT_THAT(Tap(button), ElementsAre(0, 0));
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 1));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0));

  // Held past the time for a second press, it is a press, delivered while the
  // button is still down, but not held.
  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(300)), ElementsAre(1, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0));
}

TEST_F(ControlTest, PressLongPressAndDoublePressShareAButton) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kLongPress);
  AddPress(control, PressBehavior::kDoublePress);

  EXPECT_THAT(Tap(button), ElementsAre(0, 0, 0));
  EXPECT_THAT(RunFor(absl::Milliseconds(300)), ElementsAre(1, 0, 0));

  EXPECT_THAT(Tap(button), ElementsAre(0, 0, 0));
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 0, 1));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0, 0));

  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(400)), ElementsAre(0, 1, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(false, true, false));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0, 0));
}

// A tap never makes a press beside it wait, so a button can do one thing while
// held, and another when tapped.
TEST_F(ControlTest, PressAndTapShareAButton) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kTap);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1, 0));
  EXPECT_THAT(GetPressed(), ElementsAre(true, false));
  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 1));
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));

  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(400)), ElementsAre(1, 0));
  Release(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 0));
}

TEST_F(ControlTest, TapAndLongPressShareAButton) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kTap);
  AddPress(control, PressBehavior::kLongPress);

  EXPECT_THAT(Tap(button), ElementsAre(1, 0));
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0));

  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(400)), ElementsAre(0, 1));
  Release(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(0, 0));
}

//==============================================================================
// Presses without a release
//==============================================================================

TEST_F(ControlTest, PressWithoutReleaseIsNeverHeld) {
  FakePressInput* button = AddPressInput(/*has_release=*/false);
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1));
  EXPECT_THAT(GetPressed(), ElementsAre(false));
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1));
}

TEST_F(ControlTest, PressWithoutReleaseIsATap) {
  FakePressInput* button = AddPressInput(/*has_release=*/false);
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kTap);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(1, 1));
}

TEST_F(ControlTest, PressWithoutReleaseIsNeverALongPress) {
  FakePressInput* button = AddPressInput(/*has_release=*/false);
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kLongPress);

  // As a long press can't happen, a press doesn't wait for one.
  Press(button);
  EXPECT_THAT(RunFor(absl::Seconds(1)), ElementsAre(1, 0));
}

TEST_F(ControlTest, PressWithoutReleaseCanBeADoublePress) {
  FakePressInput* button = AddPressInput(/*has_release=*/false);
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kDoublePress);

  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 0));
  Press(button);
  EXPECT_THAT(RunFor(kRunTime), ElementsAre(0, 1));

  // A single press is a press, once no second press can follow.
  Press(button);
  EXPECT_THAT(RunFor(absl::Milliseconds(100)), ElementsAre(0, 0));
  EXPECT_THAT(RunFor(absl::Milliseconds(100)), ElementsAre(1, 0));
}

//==============================================================================
// Modifiers
//==============================================================================

// Each registration is for its own modifiers, and only when the others' are
// off.
TEST_F(ControlTest, ModifiersChooseBetweenPresses) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kNormal, kModShift);
  AddPress(control, PressBehavior::kNormal, kModCtrl);

  EXPECT_THAT(Tap(button), ElementsAre(1, 0, 0));
  SetModifiers(kModShift, true);
  EXPECT_THAT(Tap(button), ElementsAre(0, 1, 0));

  // Another modifier doesn't matter, as nothing registered for it.
  SetModifiers(kModAlt, true);
  EXPECT_THAT(Tap(button), ElementsAre(0, 1, 0));

  // With both shift and control, neither is for its own modifiers alone.
  SetModifiers(kModCtrl, true);
  EXPECT_THAT(Tap(button), ElementsAre(0, 0, 0));
}

// A press is released by its button, whatever the modifiers are by then.
TEST_F(ControlTest, ReleaseGoesToThePressThatWasPressed) {
  FakePressInput* button = AddPressInput();
  Control* control = Create();
  AddPress(control, PressBehavior::kNormal);
  AddPress(control, PressBehavior::kNormal, kModShift);

  SetModifiers(kModShift, true);
  Press(button);
  RunFor(kRunTime);
  EXPECT_THAT(GetPressed(), ElementsAre(false, true));
  SetModifiers(kModShift, false);
  Release(button);
  RunFor(kRunTime);
  EXPECT_THAT(GetPressed(), ElementsAre(false, false));
}

TEST_F(ControlTest, ModifiersChooseBetweenValues) {
  FakeValueInput* fader = AddValueInput();
  Control* control = Create();
  ControlInputHandle plain = Read(control, ControlInput::Type::kValue);
  ControlInputHandle shifted =
      Read(control, ControlInput::Type::kValue, kModShift);

  Move(fader, 0.25);
  Run();
  EXPECT_EQ(control->GetValue(plain.GetId()), 0.25);
  EXPECT_EQ(control->GetValue(shifted.GetId()), 0.0);

  SetModifiers(kModShift, true);
  Move(fader, 0.5);
  Run();
  EXPECT_EQ(control->GetValue(plain.GetId()), 0.25);
  EXPECT_EQ(control->GetValue(shifted.GetId()), 0.5);
}

TEST_F(ControlTest, ModifiersChooseBetweenDeltas) {
  FakeDeltaInput* pot = AddDeltaInput();
  Control* control = Create();
  ControlInputHandle plain = Read(control, ControlInput::Type::kDelta);
  ControlInputHandle shifted =
      Read(control, ControlInput::Type::kDelta, kModShift);

  Turn(pot, 0.25);
  Run();
  EXPECT_EQ(control->GetDelta(plain.GetId()), 0.25);
  EXPECT_EQ(control->GetDelta(shifted.GetId()), 0.0);

  SetModifiers(kModShift, true);
  Turn(pot, 0.5);
  Run();
  EXPECT_EQ(control->GetDelta(plain.GetId()), 0.0);
  EXPECT_EQ(control->GetDelta(shifted.GetId()), 0.5);
}

//==============================================================================
// Values and deltas
//==============================================================================

TEST_F(ControlTest, ValueIsWhereTheInputIsWhenRead) {
  FakeValueInput* fader = AddValueInput();
  Control* control = Create();
  Move(fader, 0.75);
  Run();

  ControlInputHandle value = Read(control, ControlInput::Type::kValue);
  EXPECT_EQ(control->GetValue(value.GetId()), 0.75);
}

TEST_F(ControlTest, DeltasAddUpOverARun) {
  FakeDeltaInput* pot = AddDeltaInput();
  Control* control = Create();
  ControlInputHandle delta = Read(control, ControlInput::Type::kDelta);

  Turn(pot, 0.25);
  Turn(pot, 0.5);
  Run();
  EXPECT_EQ(control->GetDelta(delta.GetId()), 0.75);
  Run();
  EXPECT_EQ(control->GetDelta(delta.GetId()), 0.0);
}

TEST_F(ControlTest, InputSetsTheFlagOfEachRegistrationItGoesTo) {
  FakeValueInput* fader = AddValueInput();
  Control* control = Create();
  bool plain_changed = false;
  bool shifted_changed = false;
  ControlInputHandle plain = control->RegisterInput(
      {.input_type = ControlInput::Type::kValue}, &plain_changed);
  ControlInputHandle shifted =
      control->RegisterInput({.input_type = ControlInput::Type::kValue,
                              .required_modifiers = kModShift},
                             &shifted_changed);

  Move(fader, 0.5);
  Run();
  EXPECT_TRUE(plain_changed);
  EXPECT_FALSE(shifted_changed);
}

TEST_F(ControlTest, ReadingAnInputAsAnotherTypeReadsNothing) {
  FakePressInput* button = AddPressInput();
  FakeValueInput* fader = AddValueInput();
  Control* control = Create();
  ControlInputHandle press = Read(control, ControlInput::Type::kPress);
  ControlInputHandle value = Read(control, ControlInput::Type::kValue);

  Press(button);
  Move(fader, 0.5);
  Run();
  EXPECT_EQ(control->GetValue(press.GetId()), 0.0);
  EXPECT_EQ(control->GetPressCount(value.GetId()), 0);
  EXPECT_FALSE(control->IsPressed(value.GetId()));
  EXPECT_EQ(control->GetDelta(value.GetId()), 0.0);
}

TEST_F(ControlTest, UnregisteredInputReadsNothing) {
  FakeValueInput* fader = AddValueInput();
  Control* control = Create();
  ControlInputHandle value = Read(control, ControlInput::Type::kValue);
  const InputId id = value.GetId();
  value = ControlInputHandle();

  Move(fader, 0.5);
  Run();
  EXPECT_EQ(control->GetValue(id), 0.0);
}

//==============================================================================
// Outputs
//==============================================================================

TEST_F(ControlTest, IndependentOutputsAreSetAtOnce) {
  FakeCValueOutput* cvalue = AddCValueOutput(/*mode_count=*/2);
  FakeDValueOutput* dvalue = AddDValueOutput({1, 5});
  FakeTextOutput* text = AddTextOutput(/*mode_count=*/2);
  FakeColorOutput* color = AddColorOutput(/*mode_count=*/2);
  Control* control = Create();

  control->SetCValue(0.5, /*mode=*/1);
  EXPECT_EQ(cvalue->GetValue(), 0.5);
  EXPECT_EQ(cvalue->GetMode(), 1);

  control->SetDValue(4, /*mode=*/1);
  EXPECT_EQ(dvalue->GetValue(), 4);
  EXPECT_EQ(dvalue->GetMode(), 1);

  control->SetText("Bass", /*mode=*/1);
  EXPECT_EQ(text->GetText(), "Bass");
  EXPECT_EQ(text->GetMode(), 1);

  control->SetTimelineText(TimelinePosition(2.5), /*mode=*/2);
  EXPECT_THAT(text->GetPosition(), Optional(2.5));
  EXPECT_EQ(text->GetTimelineMode(), TimelineMode::kTime);

  control->SetColor({255, 0, 0}, /*mode=*/1);
  EXPECT_EQ(color->GetColor(), (Color{255, 0, 0}));
  EXPECT_EQ(color->GetMode(), 1);
}

TEST_F(ControlTest, ModesAreTheMostOfItsOutputs) {
  AddCValueOutput(/*mode_count=*/2);
  AddDValueOutput({1, 5, 10});
  Control* control = Create();
  EXPECT_EQ(control->GetModeCount(), 3);
  EXPECT_EQ(control->GetDValueMaxValue(/*mode=*/1), 5);
  EXPECT_EQ(control->GetDValueMaxValue(/*mode=*/2), 10);
}

TEST_F(ControlTest, SettingAMissingOutputDoesNothing) {
  AddPressInput();
  Control* control = Create();
  EXPECT_EQ(control->GetModeCount(), 1);
  EXPECT_EQ(control->GetDValueMaxValue(), 0);
  control->SetCValue(0.5);
  control->SetDValue(1);
  control->SetText("Bass");
  control->SetTimelineText(TimelinePosition(2.5), /*mode=*/2);
  control->SetColor({255, 0, 0});
  Run();
}

//==============================================================================
// Bindings
//==============================================================================

// A bound output with a press is held while pressed, and is then set to the
// last value it was given.
TEST_F(ControlTest, BoundOutputIsHeldWhilePressed) {
  FakePressInput* touch = AddPressInput();
  FakeCValueOutput* output = AddCValueOutput();
  Control* control = Create(Binding::kDependent);

  Press(touch);
  Run();
  control->SetCValue(0.5);
  Run();
  control->SetCValue(1.0);
  RunFor(absl::Seconds(1));
  EXPECT_EQ(output->GetSetCount(), 0);

  // The release arrives after the control runs, so the output is set on the
  // run after it.
  Release(touch);
  Run();
  EXPECT_EQ(output->GetSetCount(), 0);
  Run();
  EXPECT_EQ(output->GetSetCount(), 1);
  EXPECT_EQ(output->GetValue(), 1.0);
}

// With a press, a bound output only waits while it is pressed, not after other
// input.
TEST_F(ControlTest, BoundOutputWithPressDoesNotWaitAfterInput) {
  AddPressInput();
  FakeValueInput* fader = AddValueInput();
  FakeCValueOutput* output = AddCValueOutput();
  Control* control = Create(Binding::kMotorized);
  ControlInputHandle value = Read(control, ControlInput::Type::kValue);

  Move(fader, 0.25);
  Run();
  control->SetCValue(1.0);
  Run();
  EXPECT_EQ(output->GetValue(), 1.0);
}

// A bound output isn't set at once, but on the next run.
TEST_F(ControlTest, BoundOutputIsSetOnTheNextRun) {
  FakeCValueOutput* output = AddCValueOutput();
  Control* control = Create(Binding::kDependent);

  control->SetCValue(1.0);
  EXPECT_EQ(output->GetSetCount(), 0);
  Run();
  EXPECT_EQ(output->GetValue(), 1.0);
}

// Without a press, a bound output waits after input: a motorized one longer
// than a dependent one.
TEST_F(ControlTest, BoundOutputWithoutPressWaitsAfterInput) {
  FakeValueInput* dependent_fader = AddValueInput();
  FakeCValueOutput* dependent_output = AddCValueOutput();
  Control* dependent = Create(Binding::kDependent);
  ControlInputHandle dependent_value =
      Read(dependent, ControlInput::Type::kValue);
  FakeValueInput* motorized_fader = AddValueInput();
  FakeCValueOutput* motorized_output = AddCValueOutput();
  Control* motorized = Create(Binding::kMotorized);
  ControlInputHandle motorized_value =
      Read(motorized, ControlInput::Type::kValue);

  Move(dependent_fader, 0.25);
  Move(motorized_fader, 0.25);
  Run();
  dependent->SetCValue(1.0);
  motorized->SetCValue(1.0);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(dependent_output->GetSetCount(), 0);
  EXPECT_EQ(motorized_output->GetSetCount(), 0);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(dependent_output->GetValue(), 1.0);
  EXPECT_EQ(motorized_output->GetSetCount(), 0);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(motorized_output->GetSetCount(), 0);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(motorized_output->GetValue(), 1.0);
}

TEST_F(ControlTest, MoreInputStartsTheWaitAgain) {
  FakeValueInput* fader = AddValueInput();
  FakeCValueOutput* output = AddCValueOutput();
  Control* control = Create(Binding::kMotorized);
  ControlInputHandle value = Read(control, ControlInput::Type::kValue);

  Move(fader, 0.25);
  Run();
  control->SetCValue(1.0);
  RunFor(absl::Milliseconds(200));
  Move(fader, 0.5);
  RunFor(absl::Milliseconds(300));
  EXPECT_EQ(output->GetSetCount(), 0);
  RunFor(absl::Milliseconds(200));
  EXPECT_EQ(output->GetValue(), 1.0);
}

TEST_F(ControlTest, DeltaInputAlsoMakesABoundOutputWait) {
  FakeDeltaInput* pot = AddDeltaInput();
  FakeDValueOutput* output = AddDValueOutput({10});
  Control* control = Create(Binding::kDependent);
  ControlInputHandle delta = Read(control, ControlInput::Type::kDelta);

  Turn(pot, 0.25);
  Run();
  control->SetDValue(5);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(output->GetSetCount(), 0);
  RunFor(absl::Milliseconds(100));
  EXPECT_EQ(output->GetValue(), 5);
}

TEST_F(ControlTest, EachOutputTypeIsHeldWhilePressed) {
  FakePressInput* dvalue_touch = AddPressInput();
  FakeDValueOutput* dvalue = AddDValueOutput({1, 5});
  Control* dvalue_control = Create(Binding::kDependent);
  Press(dvalue_touch);
  FakePressInput* text_touch = AddPressInput();
  FakeTextOutput* text = AddTextOutput(/*mode_count=*/2);
  Control* text_control = Create(Binding::kDependent);
  Press(text_touch);
  FakePressInput* timeline_touch = AddPressInput();
  FakeTextOutput* timeline = AddTextOutput();
  Control* timeline_control = Create(Binding::kDependent);
  Press(timeline_touch);
  FakePressInput* color_touch = AddPressInput();
  FakeColorOutput* color = AddColorOutput(/*mode_count=*/2);
  Control* color_control = Create(Binding::kDependent);
  Press(color_touch);
  Run();

  dvalue_control->SetDValue(4, /*mode=*/1);
  text_control->SetText("Bass", /*mode=*/1);
  timeline_control->SetTimelineText(TimelinePosition(2.5), /*mode=*/2);
  color_control->SetColor({255, 0, 0}, /*mode=*/1);
  RunFor(absl::Seconds(1));
  EXPECT_EQ(dvalue->GetSetCount(), 0);
  EXPECT_EQ(text->GetSetCount(), 0);
  EXPECT_EQ(timeline->GetSetCount(), 0);
  EXPECT_EQ(color->GetSetCount(), 0);

  Release(dvalue_touch);
  Release(text_touch);
  Release(timeline_touch);
  Release(color_touch);
  Run();
  Run();
  EXPECT_EQ(dvalue->GetValue(), 4);
  EXPECT_EQ(dvalue->GetMode(), 1);
  EXPECT_EQ(text->GetText(), "Bass");
  EXPECT_EQ(text->GetMode(), 1);
  EXPECT_THAT(timeline->GetPosition(), Optional(2.5));
  EXPECT_EQ(timeline->GetTimelineMode(), TimelineMode::kTime);
  EXPECT_EQ(color->GetColor(), (Color{255, 0, 0}));
  EXPECT_EQ(color->GetMode(), 1);
}

//==============================================================================
// Output writers
//==============================================================================

TEST_F(ControlTest, LastWriterLeavingClearsOutputs) {
  FakeCValueOutput* cvalue = AddCValueOutput();
  FakeDValueOutput* dvalue = AddDValueOutput({1, 5});
  FakeTextOutput* text = AddTextOutput();
  FakeColorOutput* color = AddColorOutput();
  // A DValue output's cleared value and mode are its own.
  dvalue->SetCleared(2, /*mode=*/1);
  Control* control = Create();

  ControlOutputHandle writer = control->RegisterOutputWriter();
  control->SetCValue(0.5);
  control->SetDValue(1);
  control->SetText("Bass");
  control->SetColor({255, 0, 0});
  Run();

  writer = ControlOutputHandle();
  EXPECT_EQ(text->GetText(), "Bass");
  Run();
  EXPECT_EQ(cvalue->GetValue(), 0.0);
  EXPECT_EQ(dvalue->GetValue(), 2);
  EXPECT_EQ(dvalue->GetMode(), 1);
  EXPECT_EQ(text->GetText(), "");
  EXPECT_EQ(color->GetColor(), (Color{0, 0, 0}));
}

TEST_F(ControlTest, OutputsAreClearedOnlyWhenTheLastWriterLeaves) {
  FakeTextOutput* text = AddTextOutput();
  Control* control = Create();
  ControlOutputHandle first = control->RegisterOutputWriter();
  ControlOutputHandle second = control->RegisterOutputWriter();
  control->SetText("Bass");
  Run();

  first = ControlOutputHandle();
  Run();
  EXPECT_EQ(text->GetText(), "Bass");
  second = ControlOutputHandle();
  Run();
  EXPECT_EQ(text->GetText(), "");
}

// A writer that replaces the last one before the next run keeps the outputs
// from changing in between.
TEST_F(ControlTest, ReplacedWriterDoesNotClearOutputs) {
  FakeTextOutput* text = AddTextOutput();
  Control* control = Create();
  ControlOutputHandle writer = control->RegisterOutputWriter();
  control->SetText("Bass");
  Run();

  writer = ControlOutputHandle();
  writer = control->RegisterOutputWriter();
  Run();
  EXPECT_EQ(text->GetText(), "Bass");
  EXPECT_EQ(text->GetSetCount(), 1);
}

// What a writer set, but wasn't sent yet, isn't sent once it has gone. The
// output is cleared instead, once it isn't held.
TEST_F(ControlTest, LeavingWritersHeldOutputIsDropped) {
  FakePressInput* touch = AddPressInput();
  FakeCValueOutput* output = AddCValueOutput();
  Control* control = Create(Binding::kDependent);
  ControlOutputHandle writer = control->RegisterOutputWriter();

  Press(touch);
  Run();
  control->SetCValue(1.0);
  Run();
  writer = ControlOutputHandle();
  RunFor(absl::Seconds(1));
  EXPECT_EQ(output->GetSetCount(), 0);

  Release(touch);
  Run();
  Run();
  EXPECT_EQ(output->GetSetCount(), 1);
  EXPECT_EQ(output->GetValue(), 0.0);
}

}  // namespace
}  // namespace jpr
