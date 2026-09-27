// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_condition.h"

namespace jpr {

void ViewCondition::Watch(bool watch) {
  changed_ = false;
  if (watch == watching_) {
    return;
  }
  watching_ = watch;
  if (watch) {
    property_->RegisterFlag(&changed_);
  } else {
    property_->UnregisterFlag(&changed_);
  }
}

}  // namespace jpr
