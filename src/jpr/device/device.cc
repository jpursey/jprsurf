// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device.h"

#include <memory>
#include <utility>

#include "absl/log/log.h"

namespace jpr {

Device::Device(RunRegistry& run_registry,
               absl::Span<const ControlOverride> overrides)
    : run_registry_(run_registry),
      overrides_(GetOverridesByControl(overrides)) {}

Control* Device::GetControl(std::string_view name) const {
  auto it = control_map_.find(name);
  if (it == control_map_.end()) {
    return nullptr;
  }
  return it->second;
}

bool Device::IsUsed(std::string_view control, ControlInput::Type type) const {
  auto it = overrides_.find(control);
  return it == overrides_.end() || !it->second.inputs.IsSet(type);
}

bool Device::IsUsed(std::string_view control, ControlOutput::Type type) const {
  auto it = overrides_.find(control);
  return it == overrides_.end() || !it->second.outputs.IsSet(type);
}

Control* Device::AddControl(Control::Options options) {
  std::string_view name = options.name;
  if (control_map_.contains(name)) {
    LOG(ERROR) << "Attempted to add multiple controls with the same name: "
               << name;
    return nullptr;
  }
  auto control = std::make_unique<Control>(run_registry_, std::move(options));
  Control* added = control.get();
  control_map_[name] = added;
  controls_.push_back(std::move(control));
  return added;
}

}  // namespace jpr
