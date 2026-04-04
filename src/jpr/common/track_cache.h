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

  // Returns the stub track, which is a special non-null track that represents
  // no track at all. The track GUID is empty and the track ID is null, and it
  // holds all default values.
  Track* GetStubTrack() const { return stub_track_.get(); }

  // Returns the master track. This always exists in REAPER. All top-level
  // tracks are represented as children of the master track.
  Track* GetMasterTrack() const { return master_track_.get(); }

  // Returns the track with the given GUID, or nullptr if no such track exists.
  Track* GetTrack(const Guid& guid) const;

  // Returns the track with the given track ID, or nullptr if no such track
  // exists.
  Track* GetTrack(MediaTrack* track_id) const;

  // Returns all tracks in the project, in order, not including the master
  // track.
  int GetTrackCount() const { return static_cast<int>(all_tracks_.size()); }
  absl::Span<Track* const> GetTracks() const { return all_tracks_; }

  // The last touched track, which is the track that is the root for
  // shift-selection. This may be updated by Track when modified by
  // ViewMappings, or by the ControlSurface when it receives an update from
  // REAPER.
  Track* GetLastTouchedTrack() const { return last_touched_track_; }
  void SetLastTouchedTrack(Track* track) { last_touched_track_ = track; }

 private:
  friend class absl::NoDestructor<TrackCache>;

  using TrackMap = absl::flat_hash_map<Guid, std::shared_ptr<Track>>;
  using TrackIdMap = absl::flat_hash_map<MediaTrack*, Track*>;

  TrackCache();

  // This helper callsed by Refresh() adds the given track to the relevant track
  // lists and to its parent track's child track list, if it has a parent track.
  void AddTrack(Track* track);

  TrackMap track_map_;
  TrackIdMap track_id_map_;
  Track* last_touched_track_ = nullptr;
  std::vector<Track*> all_tracks_;
  std::shared_ptr<Track> master_track_;
  std::shared_ptr<Track> stub_track_;
};

}  // namespace jpr
