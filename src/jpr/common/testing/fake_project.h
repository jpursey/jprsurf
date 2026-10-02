// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// FakeProject
//
// A project in the fake REAPER (see FakeReaper): its tracks, and the rest of
// the state REAPER keeps for each project. FakeReaper creates them, and a test
// builds one up with its methods, and reads and sets its tracks' fields
// directly.
//
// GUIDs are made from the project's number and a counter, so they are unique
// across projects, and the same on every run of a test.
//==============================================================================

class FakeProject final {
 public:
  FakeProject(const FakeProject&) = delete;
  FakeProject& operator=(const FakeProject&) = delete;
  ~FakeProject() = default;

  // The master track, which every project has.
  FakeTrack* GetMasterTrack() { return &master_track_; }

  // Returns true if `track` is one of this project's tracks.
  bool HasTrack(const FakeTrack* track) const {
    return track == &master_track_;
  }

 private:
  friend class FakeReaper;

  // `number` is the project's number in FakeReaper, starting from 1.
  explicit FakeProject(int number);

  // Returns a new GUID for something in the project.
  GUID MakeGuid();

  const int number_;
  unsigned long next_guid_ = 1;
  FakeTrack master_track_;
};

// Returns the ReaProject* the fake hands out for `project`.
inline ReaProject* ToReaProject(FakeProject* project) {
  return reinterpret_cast<ReaProject*>(project);
}

}  // namespace jpr
