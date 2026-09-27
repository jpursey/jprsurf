// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/track_actions.h"
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

  void SetTrack(Track* track) {
    track_ = track;
    NotifyChanged();
  }

 private:
  friend class TrackProperties;

  Track* track_;
};

class TrackMeterProperty : public TrackProperty {
 public:
  static constexpr std::string_view kMeter = kTrackName<"meter">;

  explicit TrackMeterProperty(Track* track)
      : TrackProperty(kMeter, Type::kNormalized, track) {}

 protected:
  double ReadDouble() const override { return peak_; }

 private:
  friend class TrackProperties;
  double peak_ = 0.0;
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
class TrackProperties final : public TrackListener {
 public:
  // Selected, mute, solo, record arm, pan, and volume each have two properties,
  // which read the same value. Writing the plain one sets the value on this
  // track alone. Writing the ui_ one runs the scene's track action instead
  // (see TrackActions), which applies the modifiers, grouping, and anchors.
  static constexpr std::string_view kName = kTrackName<"name">;
  static constexpr std::string_view kColor = kTrackName<"color">;
  static constexpr std::string_view kSelected = kTrackName<"selected">;
  static constexpr std::string_view kMute = kTrackName<"mute">;
  static constexpr std::string_view kSolo = kTrackName<"solo">;
  static constexpr std::string_view kRecArm = kTrackName<"rec_arm">;
  static constexpr std::string_view kPan = kTrackName<"pan">;
  static constexpr std::string_view kVolume = kTrackName<"volume">;
  static constexpr std::string_view kMeter = TrackMeterProperty::kMeter;
  static constexpr std::string_view kUiSelected = kTrackName<"ui_selected">;
  static constexpr std::string_view kUiMute = kTrackName<"ui_mute">;
  static constexpr std::string_view kUiSolo = kTrackName<"ui_solo">;
  static constexpr std::string_view kUiRecArm = kTrackName<"ui_rec_arm">;
  static constexpr std::string_view kUiPan = kTrackName<"ui_pan">;
  static constexpr std::string_view kUiVolume = kTrackName<"ui_volume">;
  static constexpr std::string_view kTrackIsFolder = kTrackName<"is_folder">;
  // True if the track has a parent track, which is every track except the
  // master track (and the stub track).
  static constexpr std::string_view kTrackHasParent = kTrackName<"has_parent">;
  static constexpr std::string_view kTrackExists = kTrackName<"exists">;
  static constexpr std::string_view kTrackHasRoutes = kTrackName<"has_routes">;

  // The track that these properties are tied to, and the track actions they
  // use, which must not be null.
  //
  // TrackProperties are bound to the track GUID. So once created it will remain
  // bound to the same track, even if underlying the MediaTrack* changes.
  explicit TrackProperties(TrackActions* actions,
                           Track* track = TrackCache::Get().GetStubTrack());
  TrackProperties(const TrackProperties&) = delete;
  TrackProperties& operator=(const TrackProperties&) = delete;
  ~TrackProperties() override;

  // Returns the track that these properties are tied to.
  Track* GetTrack() const { return track_.get(); }

  // Sets the track that these properties are tied to.
  //
  // This will update the internal state of the properties to reflect the new
  // track.
  void SetTrack(Track* track);

  // Returns property scoped to this track with the given name, or nullptr if no
  // such property exists.
  ViewProperty* GetProperty(std::string_view name) const;

  // TrackListener implementation.
  void OnTrackChanged(Track* track) override;
  void OnTrackMeterChanged(Track* track, double peak) override;
  void OnTrackHierarchyChanged(Track* track) override;
  void OnTrackRoutesChanged(Track* track) override;

 private:
  // State
  TrackActions* const actions_;
  std::shared_ptr<Track> track_;
  mutable absl::flat_hash_map<std::string, std::unique_ptr<TrackProperty>>
      properties_;
  mutable TrackMeterProperty* meter_property_ = nullptr;

  // The only property that depends on the track's routes, if it was created.
  // Route values change often (for instance, while a send fader is moving), so
  // only it is notified when the routes change.
  mutable TrackProperty* has_routes_property_ = nullptr;
};

}  // namespace jpr