// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track_cache.h"

#include "absl/base/no_destructor.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

TrackCache& TrackCache::Get() {
  static absl::NoDestructor<TrackCache> instance;
  return *instance;
}

void TrackCache::Refresh() {
  // Clear the cache and retain the old_track_map to find changes and notify
  // listeners.
  absl::flat_hash_map<Guid, Track> old_track_map;
  std::swap(track_map_, old_track_map);

  // Iterate over all tracks to build the new cache.
  const int track_count = CountTracks(nullptr);
  for (int i = 0; i < track_count; ++i) {
    MediaTrack* track_id = ::GetTrack(nullptr, i);
    if (track_id == nullptr) {
      continue;
    }
    Guid guid(GetTrackGUID(track_id));
    Track& new_track = track_map_[guid];

    auto it = old_track_map.find(guid);
    if (it == old_track_map.end()) {
      // Track is new and doesn't exist in the old cache, so just initialize it.
      new_track.track_id = track_id;
      continue;
    }

    // Move the old track to the new cache.
    new_track = std::move(it->second);
    old_track_map.erase(it);

    // If the underlying track pointer hasn't changed, then we don't need to
    // notify listeners.
    if (new_track.track_id == track_id) {
      continue;
    }

    // The underlying track pointer has changed, so update the cache and notify
    // listeners.
    new_track.track_id = track_id;
    for (TrackListener* listener : new_track.listeners) {
      listener->OnTrackChanged(guid, track_id);
    }
  }

  // Now notify listeners for any tracks that were removed. We also keep these
  // in the new cache, so we can restore them if they come back.
  for (auto& [guid, old_track] : old_track_map) {
    Track& new_track = track_map_[guid];
    new_track = std::move(old_track);
    new_track.track_id = nullptr;
    for (TrackListener* listener : new_track.listeners) {
      listener->OnTrackChanged(guid, nullptr);
    }
  }
}

MediaTrack* TrackCache::GetTrackId(const Guid& guid) const {
  auto it = track_map_.find(guid);
  return it != track_map_.end() ? it->second.track_id : nullptr;
}

void TrackCache::Subscribe(const Guid& guid, TrackListener* listener) {
  if (guid.IsEmpty() || listener == nullptr) {
    return;
  }
  Track& track = track_map_[guid];
  track.listeners.insert(listener);
  listener_map_[listener] = guid;
  listener->OnTrackChanged(guid, track.track_id);
}

void TrackCache::Subscribe(MediaTrack* track_id, TrackListener* listener) {
  if (track_id == nullptr) {
    return;
  }
  Subscribe(Guid(GetTrackGUID(track_id)), listener);
}

void TrackCache::Unsubscribe(TrackListener* listener) {
  auto it = listener_map_.find(listener);
  if (it == listener_map_.end()) {
    return;
  }
  const Guid& guid = it->second;
  auto track_it = track_map_.find(guid);
  if (track_it != track_map_.end()) {
    track_it->second.listeners.erase(listener);
  }
  listener_map_.erase(it);
}

}  // namespace jpr
