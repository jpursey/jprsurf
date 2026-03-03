// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_properties.h"

#include "absl/log/check.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

TrackProperties::TrackProperties(Track* track)
    : track_(track->GetShared()) {}

TrackProperties::~TrackProperties() = default;

void TrackProperties::SetTrack(Track* track) {
  track_ = track->GetShared();

  // TODO: Update internal state of properties to reflect the new track.
}

ViewProperty* TrackProperties::GetProperty(std::string_view name) {
  auto it = properties_.find(name);

  // TODO: Auto-create properties on demand for valid property names, if it
  // doesn't already exist.
  return it != properties_.end() ? it->second.get() : nullptr;
}

}  // namespace jpr
