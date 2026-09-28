// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/route_reference.h"

#include "absl/strings/str_cat.h"

namespace jpr {

RouteReference::RouteReference(std::string_view name, TrackActions* actions)
    : ViewReference(name, SubjectKind::kRoute), properties_(actions) {}

void RouteReference::Set(Track* track, TrackRouteType type, int index) {
  if (track == properties_.GetTrack() && type == properties_.GetType() &&
      index == properties_.GetIndex()) {
    return;
  }
  ChangeVersion();
  properties_.SetRoute(track, type, index);
}

ViewProperty* RouteReference::GetField(std::string_view name) const {
  return properties_.GetProperty(absl::StrCat(kRouteNamespace, name));
}

}  // namespace jpr
