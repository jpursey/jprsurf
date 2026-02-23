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

void Device::AddControl(std::unique_ptr<Control> control) {
  std::string_view name = control->GetName();
  if (control_map_.contains(name)) {
    LOG(ERROR) << "Attempted to add multiple controls with the same name: "
               << name;
    return;
  }
  control_map_[name] = control.get();
  controls_.push_back(std::move(control));
}

}  // namespace jpr
