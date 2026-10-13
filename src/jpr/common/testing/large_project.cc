// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/large_project.h"

#include <vector>

namespace jpr {

void AddLargeProject(FakeProject* project) {
  constexpr int kBusCount = 4;
  const std::vector<FakeTrack*> buses = project->AddTracks(kBusCount);
  const std::vector<FakeTrack*> folders = project->AddTracks(12);
  for (FakeTrack* folder : folders) {
    const std::vector<FakeTrack*> tracks = project->AddTracks(9, folder);
    if (folder == folders[1]) {
      project->AddTracks(4, tracks[0]);
    }
  }

  for (int i = kBusCount; i < project->GetTrackCount(); ++i) {
    FakeTrack* track = project->GetTrack(i);
    project->AddSend(track, buses[(i - kBusCount) % kBusCount]);
    FakeTrack* parent = project->GetParentTrack(track);
    if (parent != nullptr) {
      project->AddSend(parent, track);
    }
  }
}

}  // namespace jpr
