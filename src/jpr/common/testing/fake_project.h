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

// A route in a project: a send from one track to another, or a hardware output
// from a track. A test reads and sets its values directly. Its ends are fixed,
// as FakeProject keeps each track's routes by them.
struct FakeRoute {
  FakeTrack* const source = nullptr;
  FakeTrack* const destination = nullptr;  // Null for a hardware output.
  double volume = 1.0;                     // As a gain: 1.0 is 0dB.
  double pan = 0.0;                        // From -1.0 (left) to 1.0 (right).
  bool mute = false;
};

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

  // Deletes `track`, and its routes, as the user does in REAPER. Its child
  // tracks move up to its parent. Its pointer is never reused, and a call with
  // it fails the test.
  void DeleteTrack(FakeTrack* track);

  // Returns the folder `track` is in, or null for a top level track and the
  // master, as GetParentTrack() does.
  FakeTrack* GetParentTrack(const FakeTrack* track) const;

  // Returns true if `track` is the master, or one of the project's tracks.
  bool HasTrack(const FakeTrack* track) const;

  // Returns true if `track` was one of the project's tracks, and was deleted.
  bool HadTrack(const FakeTrack* track) const;

  //----------------------------------------------------------------------------
  // Routes
  //
  // A send, and the receive at its other end, are the same route, so they
  // can't disagree. Each track's sends, receives, and hardware outputs are in
  // the order they were added.
  //----------------------------------------------------------------------------

  // Adds a send from `source` to `destination`, and returns it. They must be
  // different tracks, and neither may be the master, which REAPER routes with
  // its own setting instead.
  FakeRoute* AddSend(FakeTrack* source, FakeTrack* destination);

  // Adds a hardware output from `source`, and returns it.
  FakeRoute* AddHardwareOutput(FakeTrack* source);

  // Deletes `route`, as the user does in REAPER.
  void DeleteRoute(FakeRoute* route);

  // Returns the sends from `track`, the receives into it, or its hardware
  // outputs.
  absl::Span<FakeRoute* const> GetSends(const FakeTrack* track) const {
    return GetTrackRoutes(track).sends;
  }
  absl::Span<FakeRoute* const> GetReceives(const FakeTrack* track) const {
    return GetTrackRoutes(track).receives;
  }
  absl::Span<FakeRoute* const> GetHardwareOutputs(
      const FakeTrack* track) const {
    return GetTrackRoutes(track).hardware_outputs;
  }

  //----------------------------------------------------------------------------
  // Undo
  //----------------------------------------------------------------------------

  // The undo points added to the project, in order.
  absl::Span<const FakeUndoPoint> GetUndoPoints() const { return undo_points_; }

 private:
  friend class FakeReaper;

  // A track's routes, each in the order they were added.
  struct TrackRoutes {
    std::vector<FakeRoute*> sends;
    std::vector<FakeRoute*> receives;
    std::vector<FakeRoute*> hardware_outputs;
  };

  // `number` is the project's number in FakeReaper, starting from 1.
  explicit FakeProject(int number);

  // Returns a new GUID for something in the project.
  GUID MakeGuid();

  // Returns the index of `track` in tracks_, or -1 if it isn't there.
  int FindTrack(const FakeTrack* track) const;

  // Returns true if `track` is in `folder`, directly or in one of its folders.
  bool IsInFolder(const FakeTrack* track, const FakeTrack* folder) const;

  // Adds a route from `source` to `destination`, which AddSend() and
  // AddHardwareOutput() have checked.
  FakeRoute* AddRoute(FakeTrack* source, FakeTrack* destination);

  // Removes `route` from the routes of the tracks at each end, and deletes it.
  void RemoveRoute(FakeRoute* route);

  // Returns `track`'s routes, which are empty if it has none.
  const TrackRoutes& GetTrackRoutes(const FakeTrack* track) const;

  const int number_;
  unsigned long next_guid_ = 1;
  FakeTrack master_track_;

  // The tracks, in order, and the folder each is in (null at the top level).
  std::vector<std::unique_ptr<FakeTrack>> tracks_;
  absl::flat_hash_map<const FakeTrack*, FakeTrack*> parents_;

  // Deleted tracks, which are kept so their pointers are never reused.
  absl::flat_hash_set<const FakeTrack*> deleted_tracks_;
  std::vector<std::unique_ptr<FakeTrack>> deleted_track_storage_;

  // The routes, and each track's routes, which only AddRoute() and
  // RemoveRoute() change.
  std::vector<std::unique_ptr<FakeRoute>> routes_;
  absl::flat_hash_map<const FakeTrack*, TrackRoutes> track_routes_;

  std::vector<FakeUndoPoint> undo_points_;
};

// Returns the ReaProject* the fake hands out for `project`.
inline ReaProject* ToReaProject(FakeProject* project) {
  return reinterpret_cast<ReaProject*>(project);
}

}  // namespace jpr
