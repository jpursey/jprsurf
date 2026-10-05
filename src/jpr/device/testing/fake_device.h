// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string_view>

#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/device.h"
#include "jpr/device/testing/fake_control_io.h"

namespace jpr {

//==============================================================================
// FakeDevice
//
// A device of fake controls, which a test adds by name, then drives and reads
// through their fake inputs and outputs (see fake_control_io.h), with no MIDI
// or hardware behind them. It is for testing what is built on devices, without
// depending on any particular device.
//==============================================================================

class FakeDevice final : public Device {
 public:
  // The fakes a control is made of, as Control::Options takes them. The
  // control has no input or output for any that is null.
  struct ControlOptions {
    std::string_view name;
    std::unique_ptr<FakeValueInput> value_input;
    std::unique_ptr<FakeDeltaInput> delta_input;
    std::unique_ptr<FakePressInput> press_input;
    Control::Binding binding = Control::Binding::kIndependent;
    std::unique_ptr<FakeCValueOutput> cvalue_output;
    std::unique_ptr<FakeDValueOutput> dvalue_output;
    std::unique_ptr<FakeTextOutput> text_output;
    std::unique_ptr<FakeColorOutput> color_output;
  };

  // A control added to the device, and its fakes, which are null for any it
  // doesn't have. The device owns them all.
  struct FakeControl {
    Control* control = nullptr;
    FakeValueInput* value_input = nullptr;
    FakeDeltaInput* delta_input = nullptr;
    FakePressInput* press_input = nullptr;
    FakeCValueOutput* cvalue_output = nullptr;
    FakeDValueOutput* dvalue_output = nullptr;
    FakeTextOutput* text_output = nullptr;
    FakeColorOutput* color_output = nullptr;
  };

  // `run_registry` runs the controls, and must outlive this.
  explicit FakeDevice(RunRegistry& run_registry) : Device(run_registry) {}

  // Adds a control made of the fakes, and returns it. If the name is taken,
  // the control isn't added (and Device logs an error), and every pointer
  // returned is null.
  FakeControl AddControl(ControlOptions options);

  // Each adds a control of a common kind, as AddControl() does:
  // - A button: a press input with release, and a light (a DValue output, off
  //   or on).
  // - A fader: a value input, a touch (a press input with release), and a
  //   CValue output, motorized.
  // - A pot: a delta input, and a CValue output (its ring). A pot that can be
  //   pushed has a button of its own, as Control::Options recommends.
  // - A display: text and color outputs.
  FakeControl AddButton(std::string_view name);
  FakeControl AddFader(std::string_view name);
  FakeControl AddPot(std::string_view name);
  FakeControl AddDisplay(std::string_view name);
};

}  // namespace jpr
