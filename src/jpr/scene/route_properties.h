// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/view_property.h"

namespace jpr {

class RouteProperties;

//==============================================================================
// RouteProperty
//==============================================================================

// This class represents a single property of a track route (a send or receive)
// in REAPER. It is owned by the RouteProperties class, which determines the
// route it refers to.
class RouteProperty : public ViewProperty {
 protected:
  RouteProperty(std::string_view name, Type type,
                const RouteProperties* route_properties)
      : ViewProperty(name, type), route_properties_(route_properties) {}

  // Returns the RouteProperties that own this property.
  const RouteProperties& GetRouteProperties() const {
    return *route_properties_;
  }

 private:
  friend class RouteProperties;

  const RouteProperties* route_properties_;
};

//==============================================================================
// RouteProperties
//==============================================================================

// This class represents a set of ViewProperties that are associated with a
// single route (a send or receive) of a track in REAPER, identified by the
// track, the route type, and the route's index.
//
// Like TrackProperties, these properties are created on demand. They read and
// write the route through its Track, and are notified whenever the track's
// routes change, or the track itself changes.
//
// The track at the other end of the route is not represented here. A view can
// show it (its name, color, etc.) with TrackProperties for that track.
class RouteProperties final : public TrackListener {
 public:
  static constexpr std::string_view kVolume = "route_volume";
  static constexpr std::string_view kPan = "route_pan";
  static constexpr std::string_view kMute = "route_mute";
  static constexpr std::string_view kExists = "route_exists";

  explicit RouteProperties(Track* track = TrackCache::Get().GetStubTrack(),
                           TrackRouteType type = TrackRouteType::kSend,
                           int index = 0);
  RouteProperties(const RouteProperties&) = delete;
  RouteProperties& operator=(const RouteProperties&) = delete;
  ~RouteProperties() override;

  // The route that these properties are tied to.
  Track* GetTrack() const { return track_.get(); }
  TrackRouteType GetType() const { return type_; }
  int GetIndex() const { return index_; }

  // Returns the route, or null if the track has no such route (for instance,
  // if the index is past the end of the track's routes). The returned route is
  // only valid until the track's routes change.
  const TrackRoute* GetRoute() const;

  // Sets the route that these properties are tied to, and notifies every
  // property of the change.
  void SetRoute(Track* track, TrackRouteType type, int index);

  // Returns the property scoped to this route with the given name, or nullptr
  // if no such property exists.
  ViewProperty* GetProperty(std::string_view name) const;

  // TrackListener implementation.
  void OnTrackChanged(Track* track) override;
  void OnTrackRoutesChanged(Track* track) override;

 private:
  void NotifyChanged();

  std::shared_ptr<Track> track_;
  TrackRouteType type_;
  int index_;
  mutable absl::flat_hash_map<std::string, std::unique_ptr<RouteProperty>>
      properties_;
};

}  // namespace jpr
