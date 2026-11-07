// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/log/check.h"
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

// The groups a track leads and follows in for one property, as masks of groups
// 1 to 64, where group 1 is the lowest bit (see GroupMask()).
struct FakeTrackGroups {
  uint64_t lead = 0;
  uint64_t follow = 0;

  bool operator==(const FakeTrackGroups&) const = default;
};

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

  // As I_RECMODE: REAPER's record mode, such as 0 input and 2 none (monitoring
  // only). REAPER holds any number, so the fake does too. The master's is
  // always 0.
  int rec_mode = 0;

  // As I_RECMON: 0 off, 1 on, and 2 auto (tape), and GetTrackState()'s
  // monitoring flags. REAPER's new tracks have it on, but the fake's are off,
  // so a test's tracks have no monitoring flag unless it sets one. REAPER holds
  // any number, which shows no flag. The master's starts on, and never shows a
  // flag.
  int monitor_mode = 0;

  // As I_AUTOMODE: 0 trim/read, 1 read, 2 touch, 3 write, 4 latch, and 5 latch
  // preview.
  int auto_mode = 0;

  // As GetTrackColor(): 0 for none, or 0x01BBGGRR. The master's reads as 0.
  int color = 0;

  // The groups the track leads and follows in, for each property the fake
  // groups (see kGroupProperties). A grouped change to a track reaches the
  // followers of each group it leads (see "Track changes" in fake_reaper.cc
  // for how). JoinGroup() makes a track lead and follow a group in all of
  // them. The master's are empty.
  FakeTrackGroups volume_groups;
  FakeTrackGroups pan_groups;
  FakeTrackGroups mute_groups;
  FakeTrackGroups solo_groups;
  FakeTrackGroups rec_arm_groups;

  // As B_SHOWINMIXER and B_SHOWINTCP, the Track Manager's settings, and
  // GetTrackState()'s hidden flags. The master isn't in the Track Manager: its
  // show_in_tcp is the project's View: Toggle master track visible (40075),
  // and only GetTrackState() shows it, as its B_SHOWINTCP reads 1 either way.
  bool show_in_mixer = true;
  bool show_in_tcp = true;

  bool operator==(const FakeTrackUndoValues&) const = default;
};

struct FakeTrack : FakeTrackUndoValues {
  bool selected = false;

  // How a folder is collapsed in the track panel, as I_FOLDERCOMPACT (0
  // normal, 1 small, and 2 collapsed), and whether it is collapsed in the
  // mixer (which only the project file holds). Neither changes any track's
  // hidden flags. Only the project file sets them, so the fake doesn't model
  // their undo.
  int folder_compact = 0;
  bool folder_collapsed_in_mixer = false;

  // As Track_GetPeakInfo() for the left and right channels: 1.0 is 0dB.
  std::array<double, 2> peak = {};
};

// One of FakeTrack's groups, such as &FakeTrack::rec_arm_groups.
using FakeGroupsMember = FakeTrackGroups FakeTrackUndoValues::*;

// A property the fake groups.
struct FakeGroupProperty {
  // As GetSetTrackGroupMembership() names it, before _LEAD or _FOLLOW.
  std::string_view name;
  FakeGroupsMember groups;

  // True if a grouped change reaches tracks as REAPER's does for any groups
  // (see "Track changes" in fake_reaper.cc). Otherwise, it is faked only where
  // each track leads and follows the same group, and no more than one.
  bool faked_in_full = false;
};

// The properties the fake groups, in the order of the project file's
// GROUP_FLAGS, which has each one's leads, then its follows.
inline constexpr FakeGroupProperty kGroupProperties[] = {
    {"VOLUME", &FakeTrackUndoValues::volume_groups},
    {"PAN", &FakeTrackUndoValues::pan_groups},
    {"MUTE", &FakeTrackUndoValues::mute_groups},
    {"SOLO", &FakeTrackUndoValues::solo_groups},
    {"RECARM", &FakeTrackUndoValues::rec_arm_groups, /*faked_in_full=*/true},
};

// Returns FakeTrackGroups' mask of `group`, from 1 to 64.
inline uint64_t GroupMask(int group) {
  CHECK(group >= 1 && group <= 64) << "Group " << group;
  return uint64_t{1} << (group - 1);
}

// Makes `track` lead and follow `group`, from 1 to 64, in every property the
// fake groups, as REAPER's track grouping dialog does with all of them
// checked.
inline void JoinGroup(FakeTrack* track, int group) {
  const uint64_t mask = GroupMask(group);
  for (const FakeGroupProperty& property : kGroupProperties) {
    FakeTrackGroups& groups = track->*property.groups;
    groups.lead |= mask;
    groups.follow |= mask;
  }
}

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
