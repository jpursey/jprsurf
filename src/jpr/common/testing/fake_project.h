// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// An undo point added with Undo_OnStateChangeEx().
struct FakeUndoPoint {
  std::string name;
  int flags = 0;  // UNDO_STATE_* flags.
};

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

  //----------------------------------------------------------------------------
  // Tracks
  //----------------------------------------------------------------------------

  // The master track, which every project has.
  FakeTrack* GetMasterTrack() { return &master_track_; }

  // The project's tracks, in order, not including the master.
  int GetTrackCount() const { return static_cast<int>(tracks_.size()); }
  FakeTrack* GetTrack(int index) { return tracks_[index].get(); }

  // Adds a track called `name` after the last track in `parent`'s folder, or
  // at the end of the project if `parent` is null, and returns it.
  FakeTrack* AddTrack(std::string_view name, FakeTrack* parent = nullptr);

  // Deletes `track`, as the user does in REAPER. Its child tracks move up to
  // its parent. Its pointer is never reused, and a call with it fails the
  // test.
  void DeleteTrack(FakeTrack* track);

  // Returns the folder `track` is in, or null for a top level track and the
  // master, as GetParentTrack() does.
  FakeTrack* GetParentTrack(const FakeTrack* track) const;

  // Returns true if `track` is the master, or one of the project's tracks.
  bool HasTrack(const FakeTrack* track) const;

  // Returns true if `track` was one of the project's tracks, and was deleted.
  bool HadTrack(const FakeTrack* track) const;

  //----------------------------------------------------------------------------
  // Undo
  //----------------------------------------------------------------------------

  // The undo points added to the project, in order.
  absl::Span<const FakeUndoPoint> GetUndoPoints() const { return undo_points_; }

 private:
  friend class FakeReaper;

  // `number` is the project's number in FakeReaper, starting from 1.
  explicit FakeProject(int number);

  // Returns a new GUID for something in the project.
  GUID MakeGuid();

  // Returns the index of `track` in tracks_, or -1 if it isn't there.
  int FindTrack(const FakeTrack* track) const;

  // Returns true if `track` is in `folder`, directly or in one of its folders.
  bool IsInFolder(const FakeTrack* track, const FakeTrack* folder) const;

  const int number_;
  unsigned long next_guid_ = 1;
  FakeTrack master_track_;

  // The tracks, in order, and the folder each is in (null at the top level).
  std::vector<std::unique_ptr<FakeTrack>> tracks_;
  absl::flat_hash_map<const FakeTrack*, FakeTrack*> parents_;

  // Deleted tracks, which are kept so their pointers are never reused.
  absl::flat_hash_set<const FakeTrack*> deleted_tracks_;
  std::vector<std::unique_ptr<FakeTrack>> deleted_track_storage_;

  std::vector<FakeUndoPoint> undo_points_;
};

// Returns the ReaProject* the fake hands out for `project`.
inline ReaProject* ToReaProject(FakeProject* project) {
  return reinterpret_cast<ReaProject*>(project);
}

}  // namespace jpr
