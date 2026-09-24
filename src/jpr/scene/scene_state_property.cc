// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene_state_property.h"

#include "jpr/scene/scene.h"

namespace jpr {

void SceneStateProperty::OnRegistered() {
  scene_->RegisterProperty(this);

  // The state isn't updated while nothing uses the property, and the mapping
  // that registered it writes its control immediately, so bring it up to date
  // first.
  UpdateState();
}

void SceneStateProperty::OnUnregistered() { scene_->UnregisterProperty(this); }

}  // namespace jpr
