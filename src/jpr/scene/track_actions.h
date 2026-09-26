// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <iterator>

#include "jpr/common/anchor.h"
#include "jpr/common/track.h"

namespace jpr {

// The standard behavior of a surface's track controls: select, mute, solo,
// record arm, volume, and pan. These are like clicking or changing the property
// in REAPER's UI, with the modifiers below, which are similar to REAPER's but
// not the same. Where the list has more than one behavior, the first one whose
// modifiers are all held applies.
//
// Select, mute, solo, and record arm each also have an anchor (see
// GetAnchor()). If the property's anchor is held on another track, the action
// acts on the range between the anchor track and this track instead, the same
// as Shift, but from the anchor track rather than the last touched track. This
// takes precedence over all modifiers, which are ignored.
//
// Ranges only include tracks in the track filter, and do nothing if either end
// is not in it. They ignore grouping, and don't set the last touched track.
//
// Volume and pan:
// - Ctrl: Changes only this track, ignoring grouping and ganging. (In REAPER
//   this is Shift.)
// - Default: Changes this track, and its grouped and ganged tracks.
//
// Select:
// - Shift: Selects the tracks between the last touched track and this track
//   that share the last touched track's parent, and unselects all others. (Not
//   available in REAPER.)
// - Ctrl+Shift: The same, but including all tracks between them, whatever their
//   parent. (In REAPER this is Shift.)
// - Ctrl: Toggles the selection of this track alone. Selecting it sets the last
//   touched track. (The same as REAPER.)
// - Default: Selects only this track, and sets the last touched track. If it is
//   already the only selected track, this unselects it instead.
//
// Mute, solo, and record arm:
// - Ctrl+Alt: Turns the property off for every track, and on for this track
//   alone. Sets the last touched track. (Not available in REAPER.)
// - Shift+Alt: Turns the property off for every track, and on for this track
//   and its grouped and ganged tracks. Sets the last touched track. (Not
//   available in REAPER.)
// - Alt: Turns the property off for every track. (In REAPER this is Ctrl.)
// - Shift: Sets the tracks between the last touched track and this track that
//   share the last touched track's parent to the last touched track's value.
//   (Not available in REAPER.)
// - Ctrl+Shift: The same, but including all tracks between them, whatever their
//   parent. (Not available in REAPER.)
// - Opt: Toggles this track. If it is selected, this also toggles each other
//   selected track, and sets the last touched track. Grouping is ignored. (Not
//   available in REAPER.)
// - Ctrl: Toggles this track alone, ignoring grouping and ganging. Sets the
//   last touched track. (In REAPER this is Shift.)
// - Default: Toggles this track, and sets its grouped and ganged tracks to the
//   same value. Sets the last touched track. (The same as REAPER.)
class TrackActions final {
 public:
  // Ranges only include tracks in the filter.
  explicit TrackActions(TrackFilter filter);
  TrackActions(const TrackActions&) = delete;
  TrackActions& operator=(const TrackActions&) = delete;
  ~TrackActions() = default;

  TrackFilter GetTrackFilter() const { return filter_; }

  // The anchor for a property's action. An anchor held on a track that no
  // longer exists is treated as not held.
  Anchor<Track>& GetAnchor(TrackBoolProperty property) {
    return anchors_[static_cast<int>(property)];
  }

  // Runs the action for selecting, toggling, or changing the property of the
  // track, with the modifiers above. UiToggle() is for mute, solo, and record
  // arm.
  void UiSelect(Track* track);
  void UiToggle(Track* track, TrackBoolProperty property);
  void UiSetVolume(Track* track, double volume);
  void UiSetPan(Track* track, double pan);

 private:
  // Returns the track holding the property's anchor, or null if it isn't held
  // on another track that exists.
  Track* GetOtherAnchorTrack(TrackBoolProperty property, const Track* track);

  void SelectRange(Track* track, Track* root, bool same_parent);
  void SetRange(Track* track, Track* root, bool same_parent,
                TrackBoolProperty property);

  TrackFilter filter_;
  Anchor<Track> anchors_[std::size(kTrackBoolProperties)];
};

}  // namespace jpr
