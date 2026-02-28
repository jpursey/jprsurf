// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_control.h"

#include <string_view>

#include "absl/strings/str_cat.h"
#include "jpr/device/device.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {

ViewControl::ViewControl(std::string_view device_name, Control* control)
    : control_(control),
      name_(absl::StrCat(device_name, "/", control->GetName())) {}

void ViewControl::AddMapping(ViewMapping* mapping) {
  mappings_.insert(mapping);
}

void ViewControl::RemoveMapping(ViewMapping* mapping) {
  mappings_.erase(mapping);
}

}  // namespace jpr