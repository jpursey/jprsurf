// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/track.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

//==============================================================================
// RouteReference
//==============================================================================

// A reference to a route (a send or receive) of a track, or to nothing. Its
// fields are the route's properties (see RouteProperties), such as "volume"
// for route:volume, and "other_track.name" for the name of the track at the
// other end of the route.
//
// Only its owner can change what it refers to.
class RouteReference final : public ViewReference {
 public:
  // The track actions are used by the other track's fields, and must outlive
  // the reference.
  RouteReference(std::string_view name, TrackActions* actions);

  // Returns the route this refers to, or null if there is no such route. The
  // returned route is only valid until the track's routes change.
  const TrackRoute* GetRoute() const { return properties_.GetRoute(); }

  // Refers to the route of the track with the type and index, which refers to
  // nothing if the track has no such route. If this changed the track, type,
  // or index, the fields are notified, and the version changes.
  void Set(Track* track, TrackRouteType type, int index);

  ViewProperty* GetField(std::string_view name) const override;

  // Updates the other track (see TrackReference::Update()). The owner calls
  // this once per run.
  void Update() override { properties_.UpdateOtherTrack(); }

 private:
  RouteProperties properties_;
};

}  // namespace jpr
