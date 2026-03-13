// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/view_property.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

class TrackProperties;

//==============================================================================
// TrackProperty
//==============================================================================

// This class represents a single property of a track in REAPER. It is owned by
// the TrackProperties class.
class TrackProperty : public ViewProperty {
 public:
  Track* GetTrack() const { return track_; }

 protected:
  explicit TrackProperty(std::string_view name, Type type, Track* track)
      : ViewProperty(name, type), track_(track) {}

  void SetTrack(Track* track) { track_ = track; }

 private:
  friend class TrackProperties;

  Track* track_;
};

//==============================================================================
// TrackProperties
//==============================================================================

// This class represents a set of ViewProperties that are associated with a
// single track in REAPER.
//
// Unlike global properties stored directly in the Scene, these properties are
// transient and may be created and destroyed based on a number of factors:
// - TrackProperties are created on demand based on a view requesting the
//   properties for a specific track.
// - TrackProperties are destroyed when the track is removed from the project,
//   or no views are mapped to the track anymore.
class TrackProperties final {
 public:
  static constexpr std::string_view kName = "track_name";
  static constexpr std::string_view kSelected = "track_selected";
  static constexpr std::string_view kMute = "track_mute";
  static constexpr std::string_view kSolo = "track_solo";
  static constexpr std::string_view kRecArm = "track_recarm";
  static constexpr std::string_view kPan = "track_pan";
  static constexpr std::string_view kVolume = "track_volume";

  // The track that these properties are tied to.
  //
  // TrackProperties are bound to the track GUID. So once created it will remain
  // bound to the same track, even if underlying the MediaTrack* changes.
  explicit TrackProperties(Track* track);
  TrackProperties(const TrackProperties&) = delete;
  TrackProperties& operator=(const TrackProperties&) = delete;
  ~TrackProperties();

  // Returns the track that these properties are tied to.
  Track* GetTrack() const { return track_.get(); }

  // Sets the track that these properties are tied to.
  //
  // This will update the internal state of the properties to reflect the new
  // track.
  void SetTrack(Track* track);

  // Returns property scoped to this track with the given name, or nullptr if no
  // such property exists.
  ViewProperty* GetProperty(std::string_view name);

 private:
  // State
  std::shared_ptr<Track> track_;
  absl::flat_hash_map<std::string, std::unique_ptr<TrackProperty>> properties_;
};

}  // namespace jpr