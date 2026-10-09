// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/types/span.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// A route's values that undo points hold (see "Undo and saving" in
// FakeProject), which are all a test can set.
struct FakeRouteUndoValues {
  double volume = 1.0;  // As a gain: 1.0 is 0dB.
  double pan = 0.0;     // From -1.0 (left) to 1.0 (right).
  bool mute = false;

  bool operator==(const FakeRouteUndoValues&) const = default;
};

// A route in a project: a send from one track to another, or a hardware output
// from a track. A test reads and sets its values directly. Its ends are fixed,
// as FakeProject keeps each track's routes by them.
struct FakeRoute : FakeRouteUndoValues {
  FakeTrack* const source = nullptr;
  FakeTrack* const destination = nullptr;  // Null for a hardware output.
};

// The isend of SetTrackSendUIVol() and SetTrackSendUIPan() that ends an edit,
// which sets nothing and calls nothing back.
inline constexpr int kEndEdit = 1;

// An undo point, added with Undo_OnStateChangeEx(), or by REAPER (see "Undo"
// in fake_reaper.cc).
struct FakeUndoPoint {
  std::string name;
  int flags = 0;  // UNDO_STATE_* flags.
};

//==============================================================================
// FakeProject
//
// A project in the fake REAPER (see FakeReaper): its tracks, and the rest of
// the state REAPER keeps for each project. FakeReaper creates them, and a test
// builds one up with its methods.
//
// A track's own values are on its FakeTrack, which a test sets directly. What
// relates tracks to each other, or must stay unique, is the project's: the
// order of the tracks, their folders, their routes, and their GUIDs. A test
// changes those only through the project's methods, which keep them
// consistent, so it can't build a state REAPER couldn't be in.
//
// GUIDs are made from the project's number and a counter, so they are unique
// across projects, and the same on every run of a test.
//==============================================================================

class FakeProject final {
 public:
  // Every project's tempo, REAPER's default.
  static constexpr double kBeatsPerMinute = 120.0;

  // Creates a project outside any FakeReaper, such as one to write as a
  // project file (see WriteProjectFile()).
  static std::unique_ptr<FakeProject> Create();

  FakeProject(const FakeProject&) = delete;
  FakeProject& operator=(const FakeProject&) = delete;
  ~FakeProject() = default;

  //----------------------------------------------------------------------------
  // Tracks
  //----------------------------------------------------------------------------

  // The master track, which every project has.
  FakeTrack* GetMasterTrack() const { return master_track_; }

  // The project's tracks, in order, not including the master.
  int GetTrackCount() const { return static_cast<int>(tracks_.size()); }
  FakeTrack* GetTrack(int index) const { return tracks_[index]; }

  // Returns the index of `track` in the project's tracks, or -1 if it isn't
  // one of them, such as the master.
  int FindTrack(const FakeTrack* track) const;

  // Adds a track called `name` after the last track in `parent`'s folder, or
  // at the end of the project if `parent` is null, and returns it. If `parent`
  // isn't a track in the project, this fails the test, and returns null.
  FakeTrack* AddTrack(std::string_view name, FakeTrack* parent = nullptr);

  // Adds `count` tracks to the end of `folder`, or of the project if it is
  // null, and returns them. Each is named for where it is, after those already
  // there: T1, T2, and so on at the top level, and T2.1, T2.2, and so on in
  // T2.
  std::vector<FakeTrack*> AddTracks(int count, FakeTrack* folder = nullptr);

  // Deletes `track`, and its routes, as the user does in REAPER. Its child
  // tracks move up to its parent. Its pointer is never reused, and a call with
  // it fails the test.
  void DeleteTrack(FakeTrack* track);

  // Brings back the deleted track `deleted`, as undoing its deletion does, and
  // returns it. It is a new track, with a new pointer, but has the deleted
  // track's GUID and values. It is added as AddTrack() adds a track, in its old
  // folder if that is in the project, even if it was itself restored. Its
  // routes aren't restored.
  FakeTrack* RestoreTrack(const FakeTrack* deleted);

  // Returns the folder `track` is in, or null for a top level track and the
  // master, as GetParentTrack() does.
  FakeTrack* GetParentTrack(const FakeTrack* track) const;

  // Returns true if `track` is a folder: a track with tracks in it.
  bool IsFolder(const FakeTrack* track) const;

  // Shows or hides `track` in the mixer, as the user does in REAPER. Every
  // track in its folder, at any depth, is set the same, whatever it was.
  void ShowInMixer(FakeTrack* track, bool shown);

  // Returns `track`'s GUID, which a deleted track keeps, or a zero GUID if it
  // was never one of the project's tracks. It stays put for as long as the
  // project exists.
  const GUID& GetGuid(const FakeTrack* track) const;

  // Returns true if `track` is the master, or one of the project's tracks.
  bool HasTrack(const FakeTrack* track) const;

  // Returns true if `track` was one of the project's tracks, and was deleted.
  bool HadTrack(const FakeTrack* track) const;

  // Returns true if any track is soloed, as AnyTrackSolo() does.
  bool AnyTrackSolo() const;

  // Returns the selected tracks, in order, with the master first if
  // `include_master` is true and it is selected.
  std::vector<FakeTrack*> GetSelectedTracks(bool include_master) const;

  // Returns the first track called `name`, or null if there is none. The
  // master isn't found by name.
  FakeTrack* FindTrackByName(std::string_view name) const;

  //----------------------------------------------------------------------------
  // Routes
  //
  // A send, and the receive at its other end, are the same route, so they
  // can't disagree. Each track's receives and hardware outputs are in the
  // order they were added. Its sends are in the order of the tracks they go
  // to, then of their receives there, as REAPER keeps each route at its
  // destination, as a receive.
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
  std::vector<FakeRoute*> GetSends(const FakeTrack* track) const;
  absl::Span<FakeRoute* const> GetReceives(const FakeTrack* track) const;
  absl::Span<FakeRoute* const> GetHardwareOutputs(const FakeTrack* track) const;

  // Returns `track`'s route at `index` as the *TrackSendUI* functions index
  // them: from 0, its hardware outputs, then its sends, and from -1 down, its
  // receives. Returns null if there is none.
  FakeRoute* GetTrackSendUiRoute(const FakeTrack* track, int index) const;

  // Returns `route`'s index at its source, as the *TrackSendUI* functions
  // index it: a hardware output's among the outputs, and a send's after them.
  int GetTrackSendUiIndex(const FakeRoute* route) const;

  // Returns a send's index at its destination, among its receives, from 0, as
  // the GetTrackReceiveUI* functions index it.
  int GetReceiveIndex(const FakeRoute* route) const;

  //----------------------------------------------------------------------------
  // Transport
  //----------------------------------------------------------------------------

  // What GetPlayState() returns: &1 playing, &2 paused, and &4 recording.
  int GetPlayState() const { return play_state_; }
  void SetPlayState(int play_state) { play_state_ = play_state; }

  // The play position, and the edit cursor's, in seconds, as
  // GetPlayPosition() and GetCursorPosition() return them.
  double GetPlayPosition() const { return play_position_; }
  void SetPlayPosition(double position) { play_position_ = position; }
  double GetCursorPosition() const { return cursor_position_; }
  void SetCursorPosition(double position) { cursor_position_ = position; }

  //----------------------------------------------------------------------------
  // Automation
  //----------------------------------------------------------------------------

  // The automation override, as GetGlobalAutomationOverride() returns it: -1
  // for none, or an automation mode. Despite the name, each project has its
  // own.
  int GetAutomationOverride() const { return automation_override_; }
  void SetAutomationOverride(int mode) { automation_override_ = mode; }

  //----------------------------------------------------------------------------
  // Undo and saving
  //
  // As in REAPER, an undo point holds the project's undoable state: each
  // track's FakeTrackUndoValues (the master's too), and each route's
  // FakeRouteUndoValues. Selection and the automation override aren't
  // undoable. A point is only added if that state
  // changed since the last one. Edit: Undo restores the state of the point
  // before the last, and Edit: Redo the state of the point after, each
  // dropping any change made since the last point. A new point drops those
  // Redo would redo.
  //
  // Undo stops at the state the project had when the first change through
  // the API (or an action's handler) was made. A test's own changes before
  // that are part of it, and those after it are part of the next point.
  //----------------------------------------------------------------------------

  // The undo points Edit: Undo would undo, in the order they were added, and
  // how many there are.
  std::vector<FakeUndoPoint> GetUndoPoints() const;
  int GetUndoCount() const { return undo_count_; }

  // The name of the undo point Edit: Redo would redo, as Undo_CanRedo2()
  // returns it, or empty if there is none.
  std::string_view GetRedo() const;

  // Adds an undo point called `name`, as Undo_OnStateChangeEx() does, if
  // `flags` (UNDO_STATE_*) has UNDO_STATE_TRACKCFG, and the undoable state
  // changed since the last point. Returns true if it added one.
  bool AddUndoPoint(std::string_view name, int flags);

  // Undoes or redoes an undo point, as Edit: Undo and Edit: Redo do, and
  // returns true, or returns false if there is none.
  bool Undo();
  bool Redo();

  // Whether the project has changes to save, as IsProjectDirty() returns. An
  // undo point sets it, as Undo and Redo do.
  bool IsDirty() const { return dirty_; }
  void SetDirty(bool dirty) { dirty_ = dirty; }

  //----------------------------------------------------------------------------
  // Media items
  //----------------------------------------------------------------------------

  // How many media items are selected, as CountSelectedMediaItems() returns.
  // The fake has no media items otherwise.
  int GetSelectedItemCount() const { return selected_item_count_; }
  void SetSelectedItemCount(int count) { selected_item_count_ = count; }

 private:
  friend class FakeReaper;

  // What the project keeps for each track, the master included, apart from
  // the values a test sets on the track itself.
  struct TrackRecord {
    std::unique_ptr<FakeTrack> track;  // Kept when the track is deleted.
    GUID guid = {};
    FakeTrack* parent = nullptr;  // Null at the top level, and for the master.
    std::vector<FakeRoute*> receives;
    std::vector<FakeRoute*> hardware_outputs;
    bool deleted = false;
  };

  // The project's undoable state, as an undo point holds it. The project
  // itself has no undoable values yet. The first would go in a
  // FakeProjectUndoValues member, which this would hold a copy of, as it does
  // each track's and route's.
  struct UndoState {
    absl::flat_hash_map<FakeTrack*, FakeTrackUndoValues> tracks;
    absl::flat_hash_map<FakeRoute*, FakeRouteUndoValues> routes;

    bool operator==(const UndoState&) const = default;
  };

  // An undo point, and the state it holds.
  struct UndoStep {
    FakeUndoPoint point;
    UndoState state;
  };

  // `number` is the project's number in FakeReaper, starting from 1.
  explicit FakeProject(int number);

  // Returns a new GUID for something in the project.
  GUID MakeGuid();

  // Returns `track`'s record, or null if it was never one of the project's
  // tracks.
  TrackRecord* FindRecord(const FakeTrack* track);
  const TrackRecord* FindRecord(const FakeTrack* track) const;

  // Returns the track in the project with `guid`, or null if there is none.
  FakeTrack* FindTrackByGuid(const GUID& guid) const;

  // Creates a track with `guid` in `parent`, and its record, but doesn't add
  // it to the order.
  FakeTrack* CreateTrack(const GUID& guid, FakeTrack* parent);

  // Adds a track with `guid` after the last track in `parent`'s folder, or at
  // the end of the project if `parent` is null, and returns it.
  FakeTrack* InsertTrack(const GUID& guid, FakeTrack* parent);

  // Returns true if `track` is in `folder`, directly or in one of its folders.
  bool IsInFolder(const FakeTrack* track, const FakeTrack* folder) const;

  // Adds a route from `source` to `destination`, which AddSend() and
  // AddHardwareOutput() have checked.
  FakeRoute* AddRoute(FakeTrack* source, FakeTrack* destination);

  // Removes `route` from the records of the tracks at each end, and from the
  // undo points, and deletes it.
  void RemoveRoute(FakeRoute* route);

  // Keeps the undoable state as where Undo stops, if it isn't kept yet. The
  // fake calls this before each change through the API.
  void BeforeChange();

  // For a change from a control surface (CSurf_On*ChangeEx()): if REAPER is
  // holding the undo point of another kind of change, adds it, then holds
  // `name`, the undo point of this kind (see "Undo" in fake_reaper.cc).
  void HoldSurfaceChange(const char* name);

  // Returns the undoable state, or sets it.
  UndoState GetUndoState() const;
  void SetUndoState(const UndoState& state);

  // Returns true if there is an undo point Redo would redo.
  bool HasRedo() const;

  // Returns the state of the last undo point, or where Undo stops if there is
  // none. BeforeChange() must have been called.
  const UndoState& GetLastUndoState() const;

  const int number_;
  unsigned long next_guid_ = 1;

  // Every track the project has had, deleted or not, by its pointer. A
  // node_hash_map, so a record (and the GUID GetTrackGUID() returns) never
  // moves.
  absl::node_hash_map<const FakeTrack*, TrackRecord> records_;

  FakeTrack* master_track_ = nullptr;
  std::vector<FakeTrack*> tracks_;  // In order, not including the master.

  std::vector<std::unique_ptr<FakeRoute>> routes_;

  int play_state_ = 0;
  double play_position_ = 0.0;
  double cursor_position_ = 0.0;

  int automation_override_ = -1;

  // Where Undo stops, once a change has been made (see BeforeChange()).
  std::optional<UndoState> undo_start_;

  // The undo points: the first undo_count_ are those Undo would undo, and the
  // rest those Redo would redo.
  std::vector<UndoStep> undo_steps_;
  int undo_count_ = 0;

  // The name of the undo point REAPER is holding for a change from a control
  // surface, or null if it isn't holding one.
  const char* held_surface_change_ = nullptr;

  bool dirty_ = false;

  int selected_item_count_ = 0;
};

// Returns the ReaProject* the fake hands out for `project`.
inline ReaProject* ToReaProject(FakeProject* project) {
  return reinterpret_cast<ReaProject*>(project);
}

}  // namespace jpr
