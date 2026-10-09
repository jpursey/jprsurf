// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/reaper_api.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// GetTrackState()'s flags. The hidden flags are what REAPER shows, which
// B_SHOWINTCP and B_SHOWINMIXER don't always say: the master's read as shown
// even while it is hidden.
inline constexpr int kTrackStateFolder = 1;
inline constexpr int kTrackStateSelected = 2;
inline constexpr int kTrackStateFxEnabled = 4;
inline constexpr int kTrackStateMute = 8;
inline constexpr int kTrackStateSolo = 16;
inline constexpr int kTrackStateSoloInPlace = 32;  // With kTrackStateSolo.
inline constexpr int kTrackStateRecArm = 64;
inline constexpr int kTrackStateMonitorOn = 128;
inline constexpr int kTrackStateMonitorAuto = 256;
inline constexpr int kTrackStateHiddenInTcp = 512;
inline constexpr int kTrackStateHiddenInMixer = 1024;

// Returns `track`'s GetTrackState() flags.
inline int GetTrackStateFlags(MediaTrack* track) {
  int flags = 0;
  GetTrackState(track, &flags);
  return flags;
}

}  // namespace jpr
