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
#include "jpr/scene/track_actions.h"
#include "jpr/scene/track_reference.h"
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
// The properties change whenever the track's routes change (see
// TrackListener::OnTrackRoutesChanged()), or the track itself changes.
//
// The track at the other end of the route is a reference, kOtherTrack, whose
// fields are named with it, such as "route:other_track.name" for its
// track:name.
class RouteProperties final : public TrackListener {
 public:
  static constexpr std::string_view kVolume = kRouteName<"volume">;
  static constexpr std::string_view kPan = kRouteName<"pan">;
  static constexpr std::string_view kMute = kRouteName<"mute">;
  static constexpr std::string_view kExists = kRouteName<"exists">;

  // The reference to the track at the other end of the route, which refers to
  // nothing if there is no such route.
  static constexpr std::string_view kOtherTrack = kRouteName<"other_track">;

  // The track actions are used by the other track's fields, and must outlive
  // the properties.
  explicit RouteProperties(TrackActions* actions,
                           Track* track = TrackCache::Get().GetStubTrack(),
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

  // Returns the property scoped to this route with the given name, including
  // the other track's fields, or nullptr if no such property exists.
  ViewProperty* GetProperty(std::string_view name) const;

  // Updates the other track (see TrackReference::Update()). The owner calls
  // this once per run.
  void UpdateOtherTrack() { other_track_.Update(); }

  // TrackListener implementation.
  void OnTrackChanged(Track* track) override;
  void OnTrackRoutesChanged(Track* track) override;

 private:
  // Points the other track at the route's, and notifies every property.
  void OnRouteChanged();

  std::shared_ptr<Track> track_;
  TrackRouteType type_;
  int index_;
  mutable absl::flat_hash_map<std::string, std::unique_ptr<RouteProperty>>
      properties_;
  TrackReference other_track_;
};

}  // namespace jpr
