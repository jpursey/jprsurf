// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "jpr/common/track.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

//==============================================================================
// TrackReference
//==============================================================================

// A reference to a track, or to nothing, with optional rules for changing by
// itself. Its fields are the track's properties (see TrackProperties), such as
// "name" for track:name.
//
// Only its owner, and whoever the owner gives non-const access to, can change
// what it refers to. Anything else gets const access (see
// Scene::GetReference()). Fields may still be written through const access, as
// that changes REAPER, not the reference.
class TrackReference final : public ViewReference {
 public:
  // Rules for the reference to change by itself, by the names of other track
  // references (see Scene::AddTrackReference()).
  struct Config {
    // A reference whose track this one starts as, and returns to whenever it
    // refers to nothing, or its track is deleted. A hidden track is kept.
    // Without one, it starts as, and returns to, nothing.
    std::string fallback;

    // A reference to follow: whenever the track it refers to changes to one
    // that is on the surface (it exists, isn't the master track, and the
    // scene's track filter includes it), this one changes to it too.
    std::string follow;
  };

  // The track actions are used by the fields that run them (such as
  // track:ui_mute), and decide which tracks are on the surface. The fallback
  // and follow are null if there are none. All must outlive the reference.
  TrackReference(std::string_view name, TrackActions* actions,
                 const TrackReference* fallback = nullptr,
                 const TrackReference* follow = nullptr);

  // Returns the track this refers to, or null if it refers to nothing.
  Track* GetTrack() const;

  // Refers to the track, or to the fallback's track if it is null or doesn't
  // exist. The fields are notified if the track changed.
  void Set(Track* track);

  ViewProperty* GetField(std::string_view name) const override;

  // Applies the follow, then the fallback, and then refreshes the track's
  // state from REAPER if any field is watched (see
  // TrackProperties::IsWatched()), and its meter if the meter field is. The
  // owner calls this once per run, after it updates the references the rules
  // refer to.
  void Update();

 private:
  // Returns the fallback's track, or null if there is no fallback.
  Track* GetFallbackTrack() const;

  const TrackFilter filter_;
  const TrackReference* const fallback_;
  const TrackReference* const follow_;

  // The follow's track when it was last checked.
  Track* followed_track_ = nullptr;

  // Refers to the stub track while the reference refers to nothing.
  TrackProperties properties_;
};

}  // namespace jpr
