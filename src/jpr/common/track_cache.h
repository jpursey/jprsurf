// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "jpr/common/guid.h"

namespace jpr {

// This is a listener interface for track changes.
//
// This can be subscribed to the TrackCache, and then will be called whenever
// the underlying MediaTrack* changes (including to null and back again).
class TrackListener {
 public:
  virtual ~TrackListener() = default;

  // This will be called whenever the underlying MediaTrack* changes (including
  // changing to null).
  virtual void OnTrackChanged(const Guid& guid, MediaTrack* track_id) = 0;
};

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
  MediaTrack* GetTrack(const Guid& guid) const;

  // Subscribes to track changes for a track with the given GUID.
  //
  // This will be called whenever the underlying MediaTrack* changes (including
  // changing to null and back).
  void Subscribe(const Guid& guid, TrackListener* listener);

  // Subscribes to track changes for a track with the given MediaTrack* track
  // ID.
  //
  // This will be called whenever the underlying MediaTrack* changes (including
  // changing to null and back).
  //
  // Note: If the track ID is null or invalid, then this has no effect as there
  // is no GUID.
  void Subscribe(MediaTrack* track_id, TrackListener* listener);

  // Unsubscribes from track changes for the previously registered listener.
  void Unsubscribe(TrackListener* listener);

 private:
  friend class absl::NoDestructor<TrackCache>;

  TrackCache() = default;

  struct Track {
    MediaTrack* track_id = nullptr;
    absl::flat_hash_set<TrackListener*> listeners;
  };

  absl::flat_hash_map<Guid, Track> track_map_;
  absl::flat_hash_map<TrackListener*, Guid> listener_map_;
};

}  // namespace jpr
