// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_pick_property.h"

#include "jpr/common/track.h"

namespace jpr {

void TrackPickProperty::TriggerAction() {
  Track* track = nullptr;
  if (source_ != nullptr) {
    track = source_->GetTrack();
  } else if (view_ != nullptr) {
    track = view_->GetTrack();
  }
  if (track == nullptr || !track->Exists()) {
    return;
  }
  reference_.Set(track);
}

}  // namespace jpr
