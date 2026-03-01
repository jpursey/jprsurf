// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "jpr/common/color.h"
#include "jpr/common/guid.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

class TrackCache;

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
class Track final {
 public:
  Track(const Track&) = delete;
  Track& operator=(const Track&) = delete;
  ~Track() = default;

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

 private:
  friend class TrackCache;

  // Called by the TrackCache when a track is added or updated in the cache.
  Track(const Guid& guid, MediaTrack* track_id);

  // Performs the actual refresh logic to update both the track ID and the
  // corresponding cached state for this track.
  void DoRefresh(MediaTrack* track_id);

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
};

}  // namespace jpr