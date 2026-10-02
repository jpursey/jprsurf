// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

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
// REAPER would make itself.
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
};

// Returns the MediaTrack* the fake hands out for `track`.
inline MediaTrack* ToMediaTrack(FakeTrack* track) {
  return reinterpret_cast<MediaTrack*>(track);
}

}  // namespace jpr
