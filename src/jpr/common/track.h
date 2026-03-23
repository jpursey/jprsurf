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
  ~Track();

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

  // Sets the track's properties directly with no additional side effects. These
  // will be applied to the underlying track in REAPER, and will also update the
  // internal cached state for this track.
  void SetName(std::string_view name);
  void SetVolume(double volume);
  void SetPan(double pan);
  void SetSelected(bool selected);
  void SetMute(bool mute);
  void SetSolo(bool solo);
  void SetRecArm(bool record);

  // Actions which run the corresponding UI behavior for each property (the
  // equivalent of clicking or changing the property in REAPER's UI, whenever
  // possible).
  //
  // Modifiers are also applied to these actions if mapped on the control
  // surface, which provide similar behavior to REAPER, but the mappings are
  // different. Mappings are checked in the declared order as listed below. The
  // first set of modifiers that match will trigger the behavior.
  //
  // Volume and Pan:
  // - Ctrl: Changes only this track, ignoring any grouping or ganging.
  //   (in REAPER this is Shift)
  // - Default: Changes this track and any grouped or ganged tracks
  //   proportionally.
  //
  // Selected:
  // - Ctrl+Shift: Selects all tracks between the last selected track and this
  //   regardless of parent track. (In REAPER this is Shift)
  // - Shift: Selects all tracks between the last selected track and this track
  //   that share the same parent track. (Not available in REAPER)
  // - Ctrl: Toggles selection of this track without affecting any other tracks.
  //   Sets the last touched track. (Same ias in REAPER)
  // - Default: Selects this track and unselects all other tracks. Sets the last
  //   touched track.
  //
  // Mute, Solo, and Rec Arm:
  // - Ctrl+Alt: Clears the property for all tracks, but sets the property on
  //   for this track alone, ignoring any ganged or grouped tracks. Sets the
  //   last touched track. (Not available in REAPER)
  // - Shift+Alt: Clears the property for all tracks, but sets the property on
  //   for this track and any ganged or grouped tracks. Sets the last touched
  //   track. (Not available in REAPER)
  // - Alt: Clears the property for all tracks, including this track. (In REAPER
  //   this is Ctrl).
  // - Ctrl+Shift: Sets the property for all tracks between the last touched
  //   track and this track to be the value of the last touced track's property
  //   value regardless of parent track. Track grouping is ignored. (Not
  //   available in REAPER)
  // - Shift: Sets the property for all tracks between the last touched track
  //   and this track to be the value of the last touced track's property value
  //   if they have the same parent track. Track grouping is ignored. (Not
  //   available in REAPER)
  // - Option: Toggles the property for this track and any ganged tracks (they
  //   each get individually toggled). Track grouping is ignored. (Not available
  //   in REAPER)
  // - Ctrl: Toggles the property for this track, but ignores any grouping or
  //   ganging. Sets the last touched track. (In REAPER this is Shift)
  // - Default: Toogles the property for this track, and sets all ganged /
  //   grouped tracks to the same value. Sets the last touched track. (Same as
  //   in REAPER)
  void UiVolume(double volume);
  void UiPan(double pan);
  void UiSelected();
  void UiMute();
  void UiSolo();
  void UiRecArm();

  // Parent track of this track.
  //
  // If the track is a top level track, or does not currently exist in REAPER,
  // this will return nullptr. This is updated whenever TrackCache::Refresh()
  // is called.
  Track* GetParentTrack() const { return parent_track_; }

  // Returns the index of this track within its parent track.
  //
  // If this is a top level track, this will return the index within all
  // top-level tracks. If the track does not currently exist in REAPER, this
  // will return zero. This is updated whenever TrackCache::Refresh() is called.
  int GetIndex() const { return index_; }

  // Returns the global index of this track within all tracks in the project.
  int GetGlobalIndex() const { return global_index_; }

  // Immediate child tracks of this track in order. These are updated
  // whenever TrackCache::Refresh() is called.
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
  using SetPropertyFn = int (*)(MediaTrack* track, int value, int ingroupflags);
  using GetPropertyFn = bool (*)(MediaTrack* track);

  // Performs the actual refresh logic to update both the track ID and the
  // corresponding cached state for this track.
  void DoRefresh(MediaTrack* track_id);

  // Notifies all listeners subscribed to this track of a change.
  void NotifyListeners();

  // Toggles the internal selected state, potentially sets the last touched
  // track, and notifies listeners.
  void DoToggleSelected();

  // Generic form for a UI property.
  void DoUiProperty(bool& property, const char* undo_entry,
                    GetPropertyFn get_property, SetPropertyFn set_property);

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

  // Track hierarchy.
  Track* parent_track_ = nullptr;
  int global_index_ = 0;  // Index of the track within the project.
  int index_ = 0;         // Index of the track within the parent.
  std::vector<Track*> child_tracks_;

  // Listeners subscribed to this track for changes.
  absl::flat_hash_set<TrackListener*> listeners_;
};

}  // namespace jpr