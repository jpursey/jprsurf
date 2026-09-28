// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_anchor_property.h"

#include "absl/memory/memory.h"
#include "jpr/scene/scene.h"

namespace jpr {

TrackAnchorProperty* TrackAnchorProperty::AddToView(View* view,
                                                    std::string_view name,
                                                    const Config& config) {
  return view->AddUserProperty(
      absl::WrapUnique(new TrackAnchorProperty(view, name, config)));
}

TrackAnchorProperty::TrackAnchorProperty(View* view, std::string_view name,
                                         const Config& config)
    : ViewProperty(name, Type::kToggle),
      view_(view),
      anchor_(&view->GetScene()->GetTrackActions().GetAnchor(config.action)),
      modifier_(config.modifier) {}

void TrackAnchorProperty::WriteBool(bool value) {
  if (!value) {
    view_->ReleaseAnchor(anchor_);
    return;
  }

  // An empty strip has no place in a range.
  Track* track = view_->GetTrack();
  if (!track->Exists()) {
    return;
  }
  view_->SetAnchor(anchor_->Hold(track, modifier_));
}

}  // namespace jpr
