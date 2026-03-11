// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device.h"

#include "absl/log/log.h"

namespace jpr {

Control* Device::GetControl(std::string_view name) const {
  auto it = control_map_.find(name);
  if (it == control_map_.end()) {
    return nullptr;
  }
  return it->second;
}

void Device::AddControl(Control::Options options) {
  std::string_view name = options.name;
  if (control_map_.contains(name)) {
    LOG(ERROR) << "Attempted to add multiple controls with the same name: "
               << name;
    return;
  }
  auto control = std::make_unique<Control>(run_registry_, std::move(options));
  control_map_[name] = control.get();
  controls_.push_back(std::move(control));
}

}  // namespace jpr
