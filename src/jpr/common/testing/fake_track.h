// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <array>
#include <string>

#include "jpr/common/track_state.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// FakeTrack
//
// A track in the fake REAPER (see FakeReaper). Each MediaTrack* the fake hands
// out points to one.
//
// It holds the track's own values, which a test reads and sets directly.
// Setting one is a change REAPER doesn't report to the surface; a test that
// needs the report makes the call REAPER would make itself. What relates
// tracks to each other, or must stay unique, is FakeProject's (see its class
// comment).
//
// Its values that undo points hold are in FakeTrackUndoValues, and the rest in
// FakeTrack itself (see "Undo and saving" in FakeProject).
//==============================================================================

struct FakeTrackUndoValues {
  // The master's is MASTER, which P_NAME can't read or set.
  std::string name;

  double volume = 1.0;  // As a gain: 1.0 is 0dB.
  double pan = 0.0;     // From -1.0 (left) to 1.0 (right).
  bool mute = false;
  bool solo = false;

  // With solo, soloed in place, as SetTrackUISolo() solos by default. The
  // master's solo is never in place.
  bool solo_in_place = false;

  bool rec_arm = false;  // The master can't be armed.

  // As I_AUTOMODE: 0 trim/read, 1 read, 2 touch, 3 write, 4 latch, and 5 latch
  // preview.
  int auto_mode = 0;

  // As GetTrackColor(): 0 for none, or 0x01BBGGRR. The master's reads as 0.
  int color = 0;

  // The track's group, or 0 for none. A grouped change to the mute, solo, rec
  // arm, volume, or pan of a track in a group changes every track in it (see
  // "Track changes" in fake_reaper.cc for how). This is simpler than REAPER's
  // groups, where each property has its own leaders and followers.
  int group = 0;

  // As B_SHOWINMIXER and B_SHOWINTCP, and GetTrackState()'s hidden flags. The
  // master's show_in_tcp is the project's View: Toggle master track visible,
  // and only GetTrackState() shows it: its B_SHOWINTCP reads 1 either way.
  bool show_in_mixer = true;
  bool show_in_tcp = true;

  bool operator==(const FakeTrackUndoValues&) const = default;
};

struct FakeTrack : FakeTrackUndoValues {
  bool selected = false;

  // As Track_GetPeakInfo() for the left and right channels: 1.0 is 0dB.
  std::array<double, 2> peak = {};
};

// CSURF_EXT_SETPAN_EX's pan mode for REAPER 4 and later's balance, which every
// fake track has, as every track in the traces did.
inline constexpr int kBalancePanMode = 3;

// Returns true if a change to `track` with SetTrackUI*()'s `group_flags` (see
// track_state.h) also changes the other selected tracks.
inline bool IsGanged(const FakeTrack& track, int group_flags) {
  return (group_flags & kPreventSelectionGanging) == 0 && track.selected;
}

// Returns true if a change with `group_flags` also changes the tracks in a
// group with the tracks it changes.
inline bool IsGrouped(int group_flags) {
  return (group_flags & kPreventTrackGrouping) == 0;
}

// Returns the MediaTrack* the fake hands out for `track`.
inline MediaTrack* ToMediaTrack(FakeTrack* track) {
  return reinterpret_cast<MediaTrack*>(track);
}

// Returns the track `track_id` points to, which must be one the fake handed
// out (see ToMediaTrack()).
inline FakeTrack* ToFakeTrack(MediaTrack* track_id) {
  return reinterpret_cast<FakeTrack*>(track_id);
}

}  // namespace jpr
