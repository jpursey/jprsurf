// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track_cache.h"

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

TrackCache& TrackCache::Get() {
  static absl::NoDestructor<TrackCache> instance;
  return *instance;
}

TrackCache::TrackCache() {
  // The stub track is a special track that represents no track at all. It has
  // an empty GUID and a null track ID, and it holds all default values. This is
  // used as a placeholder for tracks that have been deleted in REAPER, but may
  // still be referenced by listeners or through undo actions.
  stub_track_ = std::make_shared<Track>(Track::Private(), Guid(), nullptr);
  track_id_map_[nullptr] = stub_track_.get();
}

void TrackCache::Refresh() {
  // Clear the cache and retain the old_track_map to find changes and notify
  // listeners.
  TrackMap old_track_map;
  std::swap(track_map_, old_track_map);

  // Save old child track lists so we can detect hierarchy changes after the
  // rebuild and notify affected tracks. Moving leaves child_tracks_ empty,
  // which is the desired initial state for the rebuild.
  absl::flat_hash_map<Track*, std::vector<Track*>> old_child_tracks;
  for (auto& [guid, track] : old_track_map) {
    if (!track->child_tracks_.empty()) {
      old_child_tracks[track.get()] = std::move(track->child_tracks_);
      track->child_tracks_.clear();
    }
  }

  // Reset the track ID map and top level tracks, and recreate it as we iterate
  // over the tracks.
  track_id_map_.clear();
  all_tracks_.clear();

  // Add the stub track to the null ID.
  track_id_map_[nullptr] = stub_track_.get();

  // Update the master track, this is a special track that is not included in
  // the track list in REAPER and so needs to be queried independently.
  MediaTrack* master_track_id = ::GetMasterTrack(nullptr);
  if (master_track_id == nullptr) {
    LOG(ERROR) << "Failed to get master track!";
    master_track_ = stub_track_.get();
  } else {
    Guid master_guid(GetTrackGUID(master_track_id));
    std::shared_ptr<Track>& new_master_track = track_map_[master_guid];

    auto it = old_track_map.find(master_guid);
    if (it == old_track_map.end()) {
      // Master track is new and doesn't exist in the old cache, so just
      // initialize it.
      new_master_track = std::make_shared<Track>(Track::Private(), master_guid,
                                                 master_track_id);
    } else {
      // Move the old master track to the new cache.
      new_master_track = std::move(it->second);
      old_track_map.erase(it);
    }
    track_id_map_[master_track_id] = new_master_track.get();

    // Did we change the master track?
    if (master_track_ == nullptr || master_track_->GetGuid() != master_guid) {
      master_track_ = new_master_track.get();
    }

    // Always refresh the master track.
    master_track_->DoRefresh(master_track_id);
  }

  // Iterate over all tracks to build the new cache.
  const int track_count = CountTracks(nullptr);
  all_tracks_.reserve(track_count);
  for (int i = 0; i < track_count; ++i) {
    MediaTrack* track_id = ::GetTrack(nullptr, i);
    if (track_id == nullptr) {
      continue;
    }
    Guid guid(GetTrackGUID(track_id));
    std::shared_ptr<Track>& new_track = track_map_[guid];

    auto it = old_track_map.find(guid);
    if (it == old_track_map.end()) {
      // Track is new and doesn't exist in the old cache, so just initialize it.
      new_track = std::make_shared<Track>(Track::Private(), guid, track_id);
      track_id_map_[track_id] = new_track.get();
      AddTrack(new_track.get());
      continue;
    }

    // Move the old track to the new cache.
    new_track = std::move(it->second);
    const bool track_id_changed = (new_track->GetTrackId() != track_id);
    new_track->track_id_ = track_id;
    old_track_map.erase(it);
    track_id_map_[track_id] = new_track.get();
    AddTrack(new_track.get());

    // If the underlying track pointer hasn't changed, we don't bother
    // refreshing the properties as an optimization.
    if (track_id_changed) {
      new_track->DoRefresh(track_id);
    }
  }

  // Now clear the ID for the remaining tracks from the old cache, as they no
  // longer exist in REAPER. We keep the track however, as it may come back
  // through an undo action or project load, and we want to retain the cached
  // state and listeners for that track.
  for (auto& [guid, old_track] : old_track_map) {
    std::shared_ptr<Track>& new_track = track_map_[guid];
    new_track = std::move(old_track);
    new_track->parent_track_ = nullptr;
    for (Track::FilterState& state : new_track->filter_state_) {
      state = {};
    }
    new_track->sends_.clear();
    new_track->receives_.clear();
    new_track->DoRefresh(nullptr);
  }

  // Read visibility and routing for the new track list and recompute the
  // per-filter indices. The track list itself changed, so this is done
  // unconditionally. Routes are read here, after every track has been added to
  // the track ID map, so the other end of every route can be looked up.
  for (Track* track : all_tracks_) {
    track->UpdateVisibility();
    track->UpdateRoutes();
  }
  RebuildTrackIndices();

  // Notify tracks whose child hierarchy changed. This is done after the full
  // rebuild so that listeners see the final state.
  for (const auto& [guid, track] : track_map_) {
    auto it = old_child_tracks.find(track.get());
    if (it == old_child_tracks.end()) {
      // Track had no children before; notify only if it has children now.
      if (!track->child_tracks_.empty()) {
        track->NotifyHierarchyChanged();
      }
    } else {
      // Track had children before; notify if the child list differs.
      if (track->child_tracks_ != it->second) {
        track->NotifyHierarchyChanged();
      }
    }
  }
}

bool TrackCache::RefreshVisibility() {
  // A track changing visibility changes the filtered child list of its parent,
  // which is what hierarchy listeners care about. There is no need to diff
  // anything: the visibility scan already knows exactly which tracks moved.
  bool changed = false;
  absl::flat_hash_set<Track*> changed_parents;
  for (Track* track : all_tracks_) {
    if (!track->UpdateVisibility()) {
      continue;
    }
    changed = true;
    if (track->parent_track_ != nullptr) {
      changed_parents.insert(track->parent_track_);
    }
  }
  if (!changed) {
    return false;
  }

  RebuildTrackIndices();
  for (Track* parent_track : changed_parents) {
    parent_track->NotifyHierarchyChanged();
  }
  return true;
}

void TrackCache::RebuildTrackIndices() {
  for (int i = 0; i < kTrackFilterCount; ++i) {
    // Index within the filtered project track list.
    int global_index = 0;
    for (Track* track : all_tracks_) {
      Track::FilterState& state = track->filter_state_[i];
      state.global_index =
          state.visible ? std::optional<int>(global_index++) : std::nullopt;
    }

    // Index within the parent's filtered child list, and the number of children
    // the filter includes. Every track's index is assigned by the pass over its
    // parent, so the master track is walked as well as the track list itself.
    // Tracks whose parent could not be resolved are left as AddTrack() set
    // them.
    master_track_->filter_state_[i].child_count =
        AssignChildIndices(master_track_, i);
    for (Track* track : all_tracks_) {
      track->filter_state_[i].child_count = AssignChildIndices(track, i);
    }
  }
}

int TrackCache::AssignChildIndices(Track* track, int filter_index) {
  int index = 0;
  for (Track* child : track->child_tracks_) {
    Track::FilterState& state = child->filter_state_[filter_index];
    state.index = state.visible ? std::optional<int>(index++) : std::nullopt;
  }
  return index;
}

void TrackCache::AddTrack(Track* track) {
  all_tracks_.push_back(track);

  MediaTrack* parent_id = GetParentTrack(track->GetTrackId());
  if (parent_id == nullptr) {
    parent_id = master_track_->GetTrackId();
  }

  Track* parent_track = GetTrack(parent_id);
  if (parent_track == nullptr) {
    LOG(ERROR) << "Failed to find parent track for track " << track->GetGuid()
               << " with parent ID " << parent_id;
  }
  track->parent_track_ = parent_track;

  // The stub track never holds children, and a track whose parent could not be
  // resolved has no place in any child list. RebuildTrackIndices() assigns
  // indices by walking parents, so neither is ever reached there and both must
  // be cleared here.
  if (parent_track == nullptr || parent_track == stub_track_.get()) {
    for (Track::FilterState& state : track->filter_state_) {
      state.index = std::nullopt;
    }
    return;
  }
  parent_track->child_tracks_.push_back(track);
}

Track* TrackCache::GetTrack(const Guid& guid) const {
  auto it = track_map_.find(guid);
  return it != track_map_.end() ? it->second.get() : nullptr;
}

Track* TrackCache::GetTrack(MediaTrack* track_id) const {
  auto it = track_id_map_.find(track_id);
  return it != track_id_map_.end() ? it->second : nullptr;
}

Track* TrackCache::GetOnlySelectedTrack() const {
  // CountSelectedTracks and GetSelectedTrack both exclude the master track.
  if (CountSelectedTracks(nullptr) != 1) {
    return nullptr;
  }
  return GetTrack(GetSelectedTrack(nullptr, 0));
}

}  // namespace jpr
