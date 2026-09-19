// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "jpr/common/anchor.h"
#include "jpr/common/guid.h"
#include "jpr/common/track.h"

namespace jpr {

// Track actions that each have their own anchor in the TrackCache. While an
// action's anchor is held on a track, that action on another track acts on the
// range of tracks between them.
enum class TrackAnchor {
  kSelect,
  kMute,
  kSolo,
  kRecArm,
};

// The number of TrackAnchor values. TrackAnchor values are contiguous starting
// at zero.
inline constexpr int kTrackAnchorCount = 4;

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

  // Re-reads the panel visibility of every track from REAPER, recomputing the
  // per-filter indices and notifying affected tracks if anything changed.
  // Returns true if any track's visibility changed.
  //
  // REAPER provides no control surface notification when a track is shown or
  // hidden in the mixer or track control panel, so this must be polled.
  bool RefreshVisibility();

  // The filter used when enumerating tracks for display on the control surface,
  // and for ranged track operations driven from it.
  //
  // This defaults to kMcp, as a physical control surface is the equivalent of
  // REAPER's mixer control panel.
  TrackFilter GetSurfaceFilter() const { return surface_filter_; }
  void SetSurfaceFilter(TrackFilter filter) { surface_filter_ = filter; }

  // Returns the stub track, which is a special non-null track that represents
  // no track at all. The track GUID is empty and the track ID is null, and it
  // holds all default values.
  Track* GetStubTrack() const { return stub_track_.get(); }

  // Returns the master track. This always exists in REAPER. All top-level
  // tracks are represented as children of the master track.
  Track* GetMasterTrack() const { return master_track_; }

  // Returns the track with the given GUID, or nullptr if no such track exists.
  Track* GetTrack(const Guid& guid) const;

  // Returns the track with the given track ID, or nullptr if no such track
  // exists.
  Track* GetTrack(MediaTrack* track_id) const;

  // Returns all tracks in the project, in order, not including the master
  // track, regardless of filter. Callers that want only the tracks included by
  // a filter should iterate these and use Track::GetGlobalIndex(filter), which
  // is nullopt for tracks the filter excludes.
  int GetTrackCount() const { return static_cast<int>(all_tracks_.size()); }
  absl::Span<Track* const> GetTracks() const { return all_tracks_; }

  // The last touched track, which is the track that is the root for
  // shift-selection. This may be updated by Track when modified by
  // ViewMappings, or by the ControlSurface when it receives an update from
  // REAPER.
  Track* GetLastTouchedTrack() const { return last_touched_track_; }
  void SetLastTouchedTrack(Track* track) { last_touched_track_ = track; }

  // The anchor for a track action. The anchor is cleared if its track is
  // removed from REAPER.
  Anchor<Track>& GetAnchor(TrackAnchor type) {
    return anchors_[static_cast<int>(type)];
  }

  // Returns the selected track if exactly one track is selected, or nullptr if
  // no tracks or multiple tracks are selected. The master track is never
  // returned, and its selection state is ignored.
  //
  // Like the other track accessors, this does not apply any filter. Callers
  // that only want tracks on the surface must also check
  // Track::IsVisible(GetSurfaceFilter()).
  //
  // This queries REAPER directly rather than the cached Track state, as that is
  // only refreshed for tracks mapped on the surface.
  Track* GetOnlySelectedTrack() const;

 private:
  friend class absl::NoDestructor<TrackCache>;

  using TrackMap = absl::flat_hash_map<Guid, std::shared_ptr<Track>>;
  using TrackIdMap = absl::flat_hash_map<MediaTrack*, Track*>;

  TrackCache();

  // This helper called by Refresh() adds the given track to the track list and
  // to its parent track's child track list, if it has a parent track.
  void AddTrack(Track* track);

  // Recomputes the per-filter indices and child counts for every track from the
  // track lists and the current per-track visibility. This is called by
  // Refresh() and RefreshVisibility() once the lists and visibility are up to
  // date.
  void RebuildTrackIndices();

  // Assigns the index within the given filter for each child of the track, and
  // returns how many of those children the filter includes.
  static int AssignChildIndices(Track* track, int filter_index);

  // Maps for all Tracks that have ever existed in this REAPER session. This is
  // never cleared, with deleted tracks remaining in a "non-existing" state,
  // which is restored when/if they come back (for instance, via an Undo
  // operation, or project reload).
  TrackMap track_map_;

  // Current set of live tracks mapped to the backing Track class. This is
  // cleared and rebuilt on every Refresh(), but the Track objects themselves
  // are retained in track_map_ to preserve their state and listeners across
  // track deletions and additions.
  TrackIdMap track_id_map_;

  // The last touched track, which may be set to indicate the starting point
  // for shift-selection.
  Track* last_touched_track_ = nullptr;

  // Anchors for each TrackAnchor action.
  Anchor<Track> anchors_[kTrackAnchorCount];

  // All non-master tracks that currently exist in REAPER, in order.
  std::vector<Track*> all_tracks_;

  // Filter used when enumerating tracks for the control surface.
  TrackFilter surface_filter_ = TrackFilter::kMcp;

  // The single master track, which is not included in the track list. This
  // is the root of all tracks.
  Track* master_track_ = nullptr;

  // Single stub track used to represent no track at all. It will never exist,
  // but can be used as a placeholder for track mapping.
  std::shared_ptr<Track> stub_track_;
};

}  // namespace jpr
