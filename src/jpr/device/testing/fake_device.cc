// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/testing/fake_device.h"

#include <memory>
#include <utility>

namespace jpr {

FakeDevice::FakeControl FakeDevice::AddControl(ControlOptions options) {
  FakeControl fakes = {
      .value_input = options.value_input.get(),
      .delta_input = options.delta_input.get(),
      .press_input = options.press_input.get(),
      .cvalue_output = options.cvalue_output.get(),
      .dvalue_output = options.dvalue_output.get(),
      .text_output = options.text_output.get(),
      .color_output = options.color_output.get(),
  };
  fakes.control = Device::AddControl({
      .name = options.name,
      .value_input = std::move(options.value_input),
      .delta_input = std::move(options.delta_input),
      .press_input = std::move(options.press_input),
      .binding = options.binding,
      .cvalue_output = std::move(options.cvalue_output),
      .dvalue_output = std::move(options.dvalue_output),
      .text_output = std::move(options.text_output),
      .color_output = std::move(options.color_output),
  });
  if (fakes.control == nullptr) {
    return {};
  }
  return fakes;
}

FakeDevice::FakeControl FakeDevice::AddButton(std::string_view name) {
  return AddControl({
      .name = name,
      .press_input = std::make_unique<FakePressInput>(),
      .dvalue_output = std::make_unique<FakeDValueOutput>(),
  });
}

FakeDevice::FakeControl FakeDevice::AddFader(std::string_view name) {
  return AddControl({
      .name = name,
      .value_input = std::make_unique<FakeValueInput>(),
      .press_input = std::make_unique<FakePressInput>(),
      .binding = Control::Binding::kMotorized,
      .cvalue_output = std::make_unique<FakeCValueOutput>(),
  });
}

FakeDevice::FakeControl FakeDevice::AddPot(std::string_view name) {
  return AddControl({
      .name = name,
      .delta_input = std::make_unique<FakeDeltaInput>(),
      .cvalue_output = std::make_unique<FakeCValueOutput>(),
  });
}

FakeDevice::FakeControl FakeDevice::AddDisplay(std::string_view name) {
  return AddControl({
      .name = name,
      .text_output = std::make_unique<FakeTextOutput>(),
      .color_output = std::make_unique<FakeColorOutput>(),
  });
}

}  // namespace jpr
