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

  // Reset the track ID map and top level tracks, and recreate it as we iterate
  // over the tracks.
  track_id_map_.clear();
  top_level_tracks_.clear();

  // Add the stub track to the null ID.
  track_id_map_[nullptr] = stub_track_.get();

  // Update the master track, this is a special track that is not included in
  // the track list in REAPER and so needs to be queried independently.
  MediaTrack* master_track_id = ::GetMasterTrack(nullptr);
  if (master_track_id == nullptr) {
    LOG(ERROR) << "Failed to get master track!";
    master_track_ = nullptr;
  } else {
    Guid master_guid(GetTrackGUID(master_track_id));
    if (master_track_ == nullptr || master_track_->GetGuid() != master_guid) {
      master_track_ = std::make_shared<Track>(Track::Private(), master_guid,
                                              master_track_id);
    }
    track_id_map_[master_track_id] = master_track_.get();
    master_track_->DoRefresh(master_track_id);
  }

  // This helper adds the given track to its parent track's child track list, if
  // it has a parent track. This is used to maintain the child track lists for
  // all tracks as we refresh the cache.
  auto add_to_parent = [this](Track* track) {
    MediaTrack* parent_id = GetParentTrack(track->GetTrackId());
    if (parent_id == nullptr) {
      track->parent_track_ = nullptr;
      track->index_ = static_cast<int>(top_level_tracks_.size());
      top_level_tracks_.push_back(track);
      return;
    }
    Track* parent_track = GetTrack(parent_id);
    if (parent_track == nullptr) {
      LOG(ERROR) << "Failed to find parent track for track " << track->GetGuid()
                 << " with parent ID " << parent_id;
      track->parent_track_ = nullptr;
      track->index_ = 0;
      return;
    }
    track->parent_track_ = parent_track;
    track->index_ = static_cast<int>(parent_track->child_tracks_.size());
    parent_track->child_tracks_.push_back(track);
  };

  // Iterate over all tracks to build the new cache.
  const int track_count = CountTracks(nullptr);
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
      add_to_parent(new_track.get());
      continue;
    }

    // Move the old track to the new cache.
    new_track = std::move(it->second);
    old_track_map.erase(it);
    track_id_map_[track_id] = new_track.get();
    add_to_parent(new_track.get());

    // Clear child tracks. They will be added back in as we iterate over the
    // tracks, and this ensures that any tracks that are no longer children will
    // be removed.
    new_track->child_tracks_.clear();

    // If the underlying track pointer hasn't changed, then we don't need to
    // notify listeners.
    if (new_track->GetTrackId() == track_id) {
      continue;
    }

    // The underlying track pointer has changed, so update the cache and notify
    // listeners.
    new_track->DoRefresh(track_id);
  }

  // Now clear the ID for the remaining tracks from the old cache, as they no
  // longer exist in REAPER. We keep the track however, as it may come back
  // through an undo action or project load, and we want to retain the cached
  // state and listeners for that track.
  for (auto& [guid, old_track] : old_track_map) {
    std::shared_ptr<Track>& new_track = track_map_[guid];
    new_track = std::move(old_track);
    new_track->child_tracks_.clear();
    new_track->parent_track_ = nullptr;
    new_track->index_ = 0;
    new_track->DoRefresh(nullptr);
  }
}

Track* TrackCache::GetTrack(const Guid& guid) const {
  auto it = track_map_.find(guid);
  return it != track_map_.end() ? it->second.get() : nullptr;
}

Track* TrackCache::GetTrack(MediaTrack* track_id) {
  auto it = track_id_map_.find(track_id);
  return it != track_id_map_.end() ? it->second : nullptr;
}

}  // namespace jpr
