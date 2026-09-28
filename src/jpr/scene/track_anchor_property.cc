// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_anchor_property.h"

#include "jpr/common/anchor.h"
#include "jpr/common/track.h"
#include "jpr/scene/scene.h"

namespace jpr {

TrackAnchorProperty::TrackAnchorProperty(std::string_view name,
                                         const Config& config)
    : ViewProperty(name, Type::kToggle),
      action_(config.action),
      modifier_(config.modifier) {}

void TrackAnchorProperty::WriteBool(bool value) {
  if (view_ == nullptr) {
    return;
  }
  Anchor<Track>& anchor =
      view_->GetScene()->GetTrackActions().GetAnchor(action_);
  if (!value) {
    view_->ReleaseAnchor(&anchor);
    return;
  }

  // An empty strip has no place in a range.
  Track* track = view_->GetTrack();
  if (!track->Exists()) {
    return;
  }
  view_->SetAnchor(anchor.Hold(track, modifier_));
}

}  // namespace jpr
