// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "jpr/common/color.h"
#include "jpr/common/guid.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

class Track;
class TrackCache;

// This is a listener interface for track changes.
//
// This can be subscribed to the TrackCache, and then will be called whenever
// the underlying MediaTrack* changes (including to null and back again).
class TrackListener {
 public:
  virtual ~TrackListener() = default;

  // This will be called whenever the underlying MediaTrack* changes (including
  // changing to null).
  virtual void OnTrackChanged(Track* track) = 0;
};

// Represents a track in REAPER, identified by a GUID.
//
// This class provides methods to get and set various properties of the track,
// such as its name, volume, pan, selection state, mute state, solo state, and
// record arm state.
//
// This supports both currently existing tracks, as well as tracks that no
// longer exist (but may come back, for instance through an undo action or
// project load). In the latter case, all properties will return to default
// values and setting new values will have no effect.
class Track final : public std::enable_shared_from_this<Track> {
 public:
  // Called by the TrackCache when a track is added or updated in the cache.
  Track(const Track&) = delete;
  Track& operator=(const Track&) = delete;
  ~Track() = default;

  // Strong and weak pointer references to the track.
  std::shared_ptr<Track> GetShared() { return shared_from_this(); }
  std::weak_ptr<Track> GetWeak() { return shared_from_this(); }

  // Attributes
  Guid GetGuid() const { return guid_; }
  MediaTrack* GetTrackId() const { return track_id_; }

  // Returns true if this track currently exists in REAPER. If false, all
  // properties will return default values and setting new values will have no
  // effect.
  bool Exists() const { return track_id_ != nullptr; }

  // Refreshes the track by querying REAPER for the current track ID for this
  // track's GUID and updates its internal state.
  void Refresh();

  // Gets the current cached state for this track. These are updated whenever
  // Refresh() is called.
  std::string_view GetName() const { return name_; }
  double GetVolume() const { return volume_; }
  double GetPan() const { return pan_; }
  bool GetSelected() const { return selected_; }
  bool GetMute() const { return mute_; }
  bool GetSolo() const { return solo_; }
  bool GetRecArm() const { return rec_arm_; }

  // Sets the track's properties. These will be applied to the underlying track
  // in REAPER, and will also update the internal cached state for this track.
  void SetName(std::string_view name);
  void SetVolume(double volume);
  void SetPan(double pan);
  void SetSelected(bool selected);
  void SetMute(bool mute);
  void SetSolo(bool solo);
  void SetRecArm(bool record);

  // Immediate child tracks of this track in order. These are updated whenever
  // Refresh() is called, or when the track hierarchy changes in REAPER.
  int GetChildTrackCount() const {
    return static_cast<int>(child_tracks_.size());
  }
  absl::Span<Track* const> GetChildTracks() const { return child_tracks_; }

  // Subscribes to track changes for this track.
  //
  // This will be called whenever the underlying MediaTrack* changes (including
  // changing to null and back). It also will be called if any of the cached
  // properties of the track change, such as its name, volume, pan, etc.
  void Subscribe(TrackListener* listener);

  // Unsubscribes from track changes for the previously registered listener.
  void Unsubscribe(TrackListener* listener);

 private:
  friend class TrackCache;

  // Private construction parameters for a track. This is used to construct
  // tracks from the TrackCache.
  struct Private {
    explicit Private() = default;
  };

 public:
  // Must be public to allow construction with std::make_shared.
  Track(Private, const Guid& guid, MediaTrack* track_id);

 private:
  // Performs the actual refresh logic to update both the track ID and the
  // corresponding cached state for this track.
  void DoRefresh(MediaTrack* track_id);

  // Notifies all listeners subscribed to this track of a change.
  void NotifyListeners();

  // Track identification. The Guid may be empty and the track_id may be null.
  Guid guid_;
  MediaTrack* track_id_ = nullptr;

  // Cached state for the track. These are updated whenever Refresh() is called,
  // or when set explicitly through the Set*() methods.
  std::string name_;
  double volume_ = 0.0;  // Default is silent so faders will be at the bottom.
  double pan_ = 0.0;
  bool selected_ = false;
  bool mute_ = false;
  bool solo_ = false;
  bool rec_arm_ = false;

  // Child tracks
  std::vector<Track*> child_tracks_;

  // Listeners subscribed to this track for changes.
  absl::flat_hash_set<TrackListener*> listeners_;
};

}  // namespace jpr