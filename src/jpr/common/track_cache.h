// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "jpr/common/guid.h"
#include "jpr/common/track.h"

namespace jpr {

// This singleton class maintains the cached state for all tracks in REAPER.
//
// Tracks can be looked up by their GUID, which is stable across track renaming,
// project changes, and any other destructive change to the track organization
// in REAPER.
//
// This requires refreshing anytime the track list changes.
class TrackCache final {
 public:
  // Returns the singleton instance of the track cache.
  static TrackCache& Get();

  TrackCache(const TrackCache&) = delete;
  TrackCache& operator=(const TrackCache&) = delete;
  ~TrackCache() = default;

  // Refreshes the track cache by querying REAPER for the current track list and
  // updating the cache accordingly. This should be called whenever the track
  // list changes.
  void Refresh();

  // Returns the track with the given GUID, or nullptr if no such track exists.
  Track* GetTrack(const Guid& guid) const;

  // Returns the track with the given track ID, or nullptr if no such track
  // exists.
  Track* GetTrack(MediaTrack* track_id);

  // The top level tracks in the project, in order. These are tracks that have
  // no parent track, and are the roots of the track hierarchy in REAPER.
  int GetTopLevelTrackCount() const {
    return static_cast<int>(top_level_tracks_.size());
  }
  absl::Span<Track* const> GetTopLevelTracks() const {
    return top_level_tracks_;
  }

 private:
  friend class absl::NoDestructor<TrackCache>;

  using TrackMap = absl::flat_hash_map<Guid, std::shared_ptr<Track>>;
  using TrackIdMap = absl::flat_hash_map<MediaTrack*, Track*>;

  TrackCache() = default;

  TrackMap track_map_;
  TrackIdMap track_id_map_;
  std::vector<Track*> top_level_tracks_;
};

}  // namespace jpr
