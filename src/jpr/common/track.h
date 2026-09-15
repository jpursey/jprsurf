// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
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

//==============================================================================
// Track filtering
//
// REAPER independently tracks whether each track is visible in the mixer
// control panel (MCP) and in the track control panel (TCP), which the user
// manages through the Track Manager. There is only ever one track list and one
// child track list; a filter selects which of those tracks are included, and
// each Track caches its index and child count under every filter so callers can
// work in whichever space is relevant to them.
//==============================================================================

enum class TrackFilter {
  kAll,  // Every track that exists in REAPER.
  kMcp,  // Only tracks visible in the mixer control panel.
  kTcp,  // Only tracks visible in the track control panel.
};

// The number of TrackFilter values. TrackFilter values are contiguous starting
// at zero, and are used directly as indices into the per-filter track state.
inline constexpr int kTrackFilterCount = 3;

// Converts a track filter to its index within the per-filter track state.
inline constexpr int GetTrackFilterIndex(TrackFilter filter) {
  return static_cast<int>(filter);
}

//==============================================================================
// Track listener
//==============================================================================

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

  // This will be called whenever the track's peak meter value changes. The
  // peak value is a linear value in the range [0.0, +inf), where 1.0
  // corresponds to 0 dB, 0.5 corresponds to -6 dB, and so on. The peak value
  // may be greater than 1.0 if the track is clipping, and may be 0.0 if the
  // track is silent.
  virtual void OnTrackMeterChanged(Track* track, double peak) {}

  // This will be called whenever the track's child hierarchy changes (children
  // added, removed, or reordered). This is called after the full hierarchy has
  // been rebuilt, so GetChildTracks() will return the new child list.
  virtual void OnTrackHierarchyChanged(Track* track) {}

  // This will be called whenever the track's sends or receives change. This
  // includes routes being added or removed (when the TrackCache is refreshed),
  // and their volume, pan, or mute changing (when they are set, or refreshed
  // with Track::RefreshRoutes()).
  virtual void OnTrackRoutesChanged(Track* track) {}
};

//==============================================================================
// Track routes
//==============================================================================

// The kinds of routes between tracks. Hardware outputs are not included.
enum class TrackRouteType {
  kSend,     // From a track to another track.
  kReceive,  // Into a track from another track.
};

// A send from a track to another track, or a receive into a track from another
// track.
struct TrackRoute {
  // The destination track for a send, or the source track for a receive. This
  // is never null.
  Track* other_track;

  // The route's volume (1.0 is unity gain), pan (-1.0 to 1.0), and mute, as
  // shown in REAPER's UI.
  double volume = 0.0;
  double pan = 0.0;
  bool mute = false;

  bool operator==(const TrackRoute&) const = default;
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

  // Returns true if this track is included by the given filter. This is always
  // true for kAll, and is always true for the master track, which has no
  // per-panel visibility state of this form. This is updated whenever
  // TrackCache::Refresh() or TrackCache::RefreshVisibility() is called.
  //
  // This does *not* imply Exists(): the stub track and tracks that have been
  // deleted report the default, which is visible. Callers that need a track
  // they can actually operate on must check Exists() as well.
  bool IsVisible(TrackFilter filter) const {
    return filter_state_[GetTrackFilterIndex(filter)].visible;
  }

  // Refreshes the track by querying REAPER for the current track ID for this
  // track's GUID and updates its internal state.
  void Refresh();

  // Refreshes the track's peak meter values by querying REAPER for the current
  // peak levels for this track, and notifies listeners unconditionally of the
  // new value.
  void RefreshMeter();

  // Gets the current cached state for this track. These are updated whenever
  // Refresh() is called.
  std::string_view GetName() const { return name_; }
  Color GetColor() const { return color_; }
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
  // - (Ctrl+)Shift: Selects all tracks between the last selected track and this
  //   track. If Ctrl is not pressed, this only includes tracks that share the
  //   same parent track (not available in REAPER). If Ctrl is pressed, this
  //   includes all tracks regardless of parent track (in REAPER this is Shift).
  //   This does nothing at all if the last selected track or this track is not
  //   on the control surface, as there is no range to select. Holding shift
  //   always means "select a range", never one of the behaviors below.
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
  // - (Ctrl+)Shift: Sets the property for all tracks between the last touched
  //   track and this track to be the value of the last touched track's property
  //   value. Track grouping is ignored. If Ctrl is not pressed, this only
  //   includes tracks that share the same parent track. If Ctrl is pressed,
  //   this includes all tracks regardless of parent track. This does nothing at
  //   all if the last touched track or this track is not on the control
  //   surface, as there is no range to act on. Holding shift without alt always
  //   means "set a range", never one of the behaviors below. (Not available in
  //   REAPER)
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

  // Returns the index of this track within its parent track's filtered child
  // track list, or nullopt if the track has no place in that list.
  //
  // If this is a top level track, this will return the index within all
  // top-level tracks.
  //
  // Nullopt covers every reason the track is not in the list: it is excluded by
  // the filter, it does not currently exist in REAPER, or it has no parent (the
  // master and stub tracks). A returned value is always a valid index into
  // GetParentTrack()->GetChildTracks(filter), so callers that have one may
  // dereference it without a further check. This is updated whenever
  // TrackCache::Refresh() or TrackCache::RefreshVisibility() is called.
  //
  // Note that IsVisible() is not sufficient to imply a value here, as the
  // master track is always visible but never appears in a track list.
  std::optional<int> GetIndex(TrackFilter filter) const {
    return filter_state_[GetTrackFilterIndex(filter)].index;
  }

  // Returns the global index of this track within all filtered tracks in the
  // project, or nullopt if the track has no place in that list. This has the
  // same semantics as GetIndex(), against TrackCache::GetTracks(filter).
  std::optional<int> GetGlobalIndex(TrackFilter filter) const {
    return filter_state_[GetTrackFilterIndex(filter)].global_index;
  }

  // All immediate child tracks of this track, in order, regardless of filter.
  // Callers that want only the children included by a filter should iterate
  // these and skip any for which IsVisible(filter) is false; the count of those
  // is cached by GetChildTrackCount(). This is updated whenever
  // TrackCache::Refresh() is called.
  absl::Span<Track* const> GetChildTracks() const { return child_tracks_; }

  // The number of immediate child tracks included by the filter. This is
  // updated whenever TrackCache::Refresh() or TrackCache::RefreshVisibility()
  // is called.
  int GetChildTrackCount(TrackFilter filter) const {
    return filter_state_[GetTrackFilterIndex(filter)].child_count;
  }

  // Sends from this track to other tracks, and receives into this track from
  // other tracks, in REAPER's order. The index of a route in these lists is its
  // send or receive index in REAPER. These are empty for the master track and
  // for tracks that do not currently exist. The lists are rebuilt whenever
  // TrackCache::Refresh() is called, and route values are also updated by
  // RefreshRoutes().
  absl::Span<const TrackRoute> GetSends() const { return sends_; }
  absl::Span<const TrackRoute> GetReceives() const { return receives_; }
  absl::Span<const TrackRoute> GetRoutes(TrackRouteType type) const {
    return type == TrackRouteType::kSend ? sends_ : receives_;
  }

  // Re-reads the volume, pan, and mute of every route from REAPER, and notifies
  // listeners if any changed. REAPER does not report these changes reliably, so
  // this must be polled, but only for tracks whose routes are actually shown.
  void RefreshRoutes();

  // Sets the volume, pan, or mute of a route, as if it was changed in REAPER's
  // UI. These do nothing if the track does not exist or has no such route.
  void SetRouteVolume(TrackRouteType type, int index, double volume);
  void SetRoutePan(TrackRouteType type, int index, double pan);
  void SetRouteMute(TrackRouteType type, int index, bool mute);

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

  // Everything about this track under one TrackFilter, held as one entry per
  // filter in filter_state_.
  //
  // Visibility is the input, read from REAPER by UpdateVisibility(); the rest
  // is derived from it by TrackCache::RebuildTrackIndices(). Note that the
  // child count is meaningful even for a track the filter excludes, and for the
  // master track, neither of which appears in a track list at all.
  struct FilterState {
    // True if the filter includes this track.
    bool visible = true;

    // Index of this track within TrackCache::GetTracks() once the filter is
    // applied, or nullopt if the filter excludes it, it does not exist, or it
    // is the master track.
    std::optional<int> global_index;

    // Index of this track within its parent's child tracks once the filter is
    // applied, or nullopt if there is no such place: the filter excludes it, it
    // does not exist, or it has no parent track.
    std::optional<int> index;

    // Number of this track's immediate child tracks the filter includes.
    int child_count = 0;
  };

  // Performs the actual refresh logic to update both the track ID and the
  // corresponding cached state for this track.
  void DoRefresh(MediaTrack* track_id);

  // Re-reads this track's panel visibility flags from REAPER, returning true if
  // any of them changed. This is called only by the TrackCache, which owns
  // recomputing the per-filter indices when visibility changes.
  bool UpdateVisibility();

  // Rebuilds the send and receive lists from REAPER, returning true if they
  // changed. This is called only by the TrackCache during Refresh() for tracks
  // that exist, once every existing track can be looked up by its track ID.
  // The TrackCache notifies listeners once the refresh is complete.
  bool UpdateRoutes();

  // Resets this track after it has been removed from REAPER, so it no longer
  // has a track ID, parent, place in any track list, or routes. This is called
  // only by the TrackCache during Refresh(). Like UpdateRoutes(), it returns
  // true if the routes changed, and the TrackCache notifies listeners once the
  // refresh is complete.
  bool OnRemoved();

  // Returns the routes of one type from REAPER. This must only be called for a
  // track that exists, once hardware_output_count_ is up to date.
  std::vector<TrackRoute> BuildRoutes(TrackRouteType type) const;

  // Returns the index REAPER's *TrackSendUI* functions (SetTrackSendUIVol,
  // SetTrackSendUIPan, and ToggleTrackSendUIMute) use for a route.
  int GetTrackSendUiIndex(TrackRouteType type, int index) const;

  // Reads the UI volume, pan, and mute of a route into `route`, returning false
  // (and leaving `route` unchanged) if REAPER has no such route.
  bool ReadRouteValues(TrackRouteType type, int index, TrackRoute& route) const;

  // Returns the route, or null if this track does not exist or has no such
  // route.
  TrackRoute* GetMutableRoute(TrackRouteType type, int index);

  // Notifies all listeners subscribed to this track of a change.
  void NotifyListeners();

  // Notifies all listeners subscribed to this track that the child hierarchy
  // has changed.
  void NotifyHierarchyChanged();

  // Notifies all listeners subscribed to this track that its routes changed.
  void NotifyRoutesChanged();

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
  Color color_ = {0, 0, 0};
  double volume_ = 0.0;  // Default is silent so faders will be at the bottom.
  double pan_ = 0.0;
  bool selected_ = false;
  bool mute_ = false;
  bool solo_ = false;
  bool rec_arm_ = false;

  // Track hierarchy. There is a single child track list holding every child,
  // and membership of a filtered list is derived from each child's visibility.
  // The indices and counts are the part that cannot be derived without a scan,
  // so those are cached alongside it, one entry per TrackFilter.
  Track* parent_track_ = nullptr;
  FilterState filter_state_[kTrackFilterCount];
  std::vector<Track*> child_tracks_;

  // Track routing, rebuilt by UpdateRoutes(). REAPER notifies the TrackCache
  // when hardware outputs are added or removed, so the count stays up to date.
  int hardware_output_count_ = 0;
  std::vector<TrackRoute> sends_;
  std::vector<TrackRoute> receives_;

  // Listeners subscribed to this track for changes.
  absl::flat_hash_set<TrackListener*> listeners_;
};

}  // namespace jpr