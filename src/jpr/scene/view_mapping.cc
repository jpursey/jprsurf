// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include "jpr/scene/view_control.h"
#include "jpr/scene/view_property.h"

namespace jpr {

ViewMapping::ViewMapping(Type type, ViewProperty* property,
                         ViewControl* control)
    : type_(type), property_(property), control_(control) {}

ViewMapping::~ViewMapping() { Deactivate(); }

void ViewMapping::Activate() {
  if (active_) {
    return;
  }
  active_ = true;
  property_->AddMapping(this);
  control_->AddMapping(this);
}

void ViewMapping::Deactivate() {
  if (!active_) {
    return;
  }
  active_ = false;
  property_->RemoveMapping(this);
  control_->RemoveMapping(this);
}

void ViewMapping::ReadControl() {
  // TODO: Implement this to read the control value and update the property.
}

void ViewMapping::WriteControl() {
  // TODO: Implement this to read the property value and update the control.
}

}  // namespace jpr
