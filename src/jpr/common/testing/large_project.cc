// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/large_project.h"

#include <vector>

namespace jpr {

void AddLargeProject(FakeProject* project, int folders) {
  const std::vector<FakeTrack*> buses =
      project->AddTracks(kLargeProjectBusCount);
  const std::vector<FakeTrack*> folder_tracks = project->AddTracks(folders);
  for (FakeTrack* folder : folder_tracks) {
    const std::vector<FakeTrack*> tracks = project->AddTracks(9, folder);
    if (folder == folder_tracks[1]) {
      project->AddTracks(4, tracks[0]);
    }
  }

  for (int i = kLargeProjectBusCount; i < project->GetTrackCount(); ++i) {
    FakeTrack* track = project->GetTrack(i);
    project->AddSend(
        track, buses[(i - kLargeProjectBusCount) % kLargeProjectBusCount]);
    FakeTrack* parent = project->GetParentTrack(track);
    if (parent != nullptr) {
      project->AddSend(parent, track);
    }
  }
}

}  // namespace jpr
