// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/testing/fake_project.h"

namespace jpr {

// Adds a large project's tracks and routes to `project`: enough that a call
// made once per track stands out, with folders, and tracks with more routes
// than a mode's strips show. With its 12 folders, it has 128 tracks, named as
// AddTracks() names them:
// - T1 to T4, buses.
// - T5 to T16, folders with 9 tracks directly in each. The first in T6, T6.1,
//   is itself a folder of 4 tracks.
// Every track but the buses sends to a bus, in turn, so each bus has 31
// receives, and each folder also sends to each track directly in it.
//
// `folders`, at least 2, scales it: it has 10 tracks for each folder, and 8
// more.
void AddLargeProject(FakeProject* project, int folders = 12);

// How many buses AddLargeProject() adds: its first tracks.
inline constexpr int kLargeProjectBusCount = 4;

// Returns how many tracks AddLargeProject() adds with `folders`.
constexpr int GetLargeProjectTrackCount(int folders) {
  return 10 * folders + 8;
}

// Returns how many receives each bus has with `folders`.
constexpr int GetLargeProjectBusReceiveCount(int folders) {
  return (GetLargeProjectTrackCount(folders) - kLargeProjectBusCount) /
         kLargeProjectBusCount;
}

}  // namespace jpr
