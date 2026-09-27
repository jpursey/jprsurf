// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/polled_toggle_property.h"

namespace jpr {

void PolledToggleProperty::UpdateState() {
  bool value = read_();
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool PolledToggleProperty::ReadBool() const { return value_; }

void PolledToggleProperty::WriteBool(bool value) {
  if (write_ == nullptr || value == value_) {
    return;
  }
  write_(value);
  UpdateState();
}

}  // namespace jpr
