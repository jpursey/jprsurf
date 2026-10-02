// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <array>
#include <string>

#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// FakeTrack
//
// A track in the fake REAPER (see FakeReaper). Each MediaTrack* the fake hands
// out points to one.
//
// A test reads and sets its fields directly. Setting one is a change REAPER
// doesn't report to the surface; a test that needs the report makes the call
// REAPER would make itself. The order and parents of a project's tracks are
// FakeProject's, which keeps them consistent.
//==============================================================================

struct FakeTrack {
  GUID guid = {};
  std::string name;
  int color = 0;        // As GetTrackColor(): 0 for none, or 0x01BBGGRR.
  double volume = 1.0;  // As a gain: 1.0 is 0dB.
  double pan = 0.0;     // From -1.0 (left) to 1.0 (right).
  bool selected = false;
  bool mute = false;
  bool solo = false;
  bool rec_arm = false;

  // As I_AUTOMODE: 0 trim/read, 1 read, 2 touch, 3 write, 4 latch, and 5 latch
  // preview.
  int auto_mode = 0;

  // The track's group, or 0 for none. A grouped change to the mute, solo, rec
  // arm, volume, or pan of a track in a group changes every track in it. This
  // is simpler than REAPER's groups, where each property has its own leaders
  // and followers.
  int group = 0;

  // As B_SHOWINMIXER and B_SHOWINTCP.
  bool show_in_mixer = true;
  bool show_in_tcp = true;

  // As Track_GetPeakInfo() for the left and right channels: 1.0 is 0dB.
  std::array<double, 2> peak = {};
};

// Returns the MediaTrack* the fake hands out for `track`.
inline MediaTrack* ToMediaTrack(FakeTrack* track) {
  return reinterpret_cast<MediaTrack*>(track);
}

}  // namespace jpr
