// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/time/time.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// FakeReaper
//
// REAPER's state in memory, loaded as the REAPER API (see reaper_api.h) for as
// long as it exists, so code that calls REAPER can be unit tested.
//
// It fakes exactly the functions on the API list, over its model of the
// project: a setter stores the value, and a getter returns it. It holds
// REAPER's state, not its behavior, so it never calls a control surface by
// itself. A test makes the calls REAPER would make on the surface AddSurface()
// returns (see "Seen in traces" in docs/testing_and_profiling.md for what
// REAPER sends). A test that needs one function to behave otherwise hooks it
// over the fake with gb::FunctionHook.
//
// Creating the fake, and destroying it, resets the process state of every
// library linked into the test (see TestReset). Only one may exist at a time.
//
// Checks
// ------
// The fake fails the test, with ADD_FAILURE(), naming what broke, on:
// - A call to a function on the list that the fake doesn't support yet.
// - A track or project pointer that isn't in an open project.
// - A second control surface created while one is open: JPRSurf has one
//   surface, with a ControlSurfaceListener for each use.
// - A MIDI port created while it is open, or used after it is destroyed.
// - A control surface or MIDI port still open when the fake is destroyed.
// - The end of an entry point that broke a rule for using REAPER. An entry
//   point is each call on a TestControlSurface, and the test's own calls, which
//   are checked when the fake is destroyed. Each must leave PreventUIRefresh()
//   balanced, and follow the TrackBatch rule: an entry point that changes the
//   mute, solo, rec arm, or selection of several tracks makes them one change,
//   in one PreventUIRefresh() scope, or one call outside of any. Each change
//   costs a refresh of REAPER's UI, and JPRSurf adds its own undo point for
//   each.
//==============================================================================

class FakeReaper final {
 public:
  // How many times a second REAPER runs a control surface.
  static constexpr int kRunsPerSecond = 30;

  // Loads the fake as the REAPER API, and resets the process state.
  FakeReaper();

  FakeReaper(const FakeReaper&) = delete;
  FakeReaper& operator=(const FakeReaper&) = delete;

  // Checks the test's own calls (see Checks), and fails the test if a control
  // surface is still open, closing it. Then resets the process state, and
  // unloads the API. A MIDI port still open fails the test as it is destroyed
  // with the fake.
  ~FakeReaper();

  //----------------------------------------------------------------------------
  // The plugin and its control surface
  //----------------------------------------------------------------------------

  // What REAPER passes the plugin's entry point. Register("csurf") records the
  // control surface type; registering anything else fails the test.
  reaper_plugin_info_t& GetPluginInfo() { return plugin_info_; }

  // Creates a control surface of the registered type from `config`, as REAPER
  // does at startup, or when the user adds one in its preferences. Returns
  // null if the type refused to create one, or if no type is registered, or
  // if a surface is already open and the type created another (each of which
  // fails the test). Destroying the surface removes it.
  std::unique_ptr<TestControlSurface> AddSurface(std::string_view config = {});

  //----------------------------------------------------------------------------
  // Time
  //----------------------------------------------------------------------------

  // What time_precise() returns, in seconds. A test never waits on real time:
  // the clock only moves when a surface runs (see TestControlSurface::Run()),
  // or the test advances it.
  double GetTime() const;

  // Advances the clock by `duration`.
  void AdvanceTime(absl::Duration duration) { advanced_time_ += duration; }

  //----------------------------------------------------------------------------
  // Projects
  //----------------------------------------------------------------------------

  // The open projects, one for each of REAPER's project tabs, in order. The
  // fake starts with one empty project.
  int GetProjectCount() const {
    return static_cast<int>(open_projects_.size());
  }
  FakeProject& GetProjectAt(int index) { return *open_projects_[index]; }

  // The current project, which the API works on when it is given no project.
  FakeProject& GetProject() { return *open_projects_[current_project_]; }

  // Opens a new, empty project in a new tab, makes it current, and returns
  // it, as File: New project tab does. The other projects stay open, and
  // their tracks keep their pointers.
  FakeProject& AddProject();

  // Makes the open project at `index` current, as clicking its tab does.
  void SwitchProjectTo(int index);

  // Replaces the current project with a new, empty one in the same tab, and
  // returns it, as File: New project does. The old project is closed: a call
  // with one of its tracks fails the test.
  FakeProject& NewProject();

  //----------------------------------------------------------------------------
  // MIDI ports
  //----------------------------------------------------------------------------

  // Lists a MIDI input or output port called `name` after those already
  // listed, as REAPER lists the ports on the machine, and returns it.
  FakeMidiInput* AddMidiInput(std::string_view name);
  FakeMidiOutput* AddMidiOutput(std::string_view name);

  //----------------------------------------------------------------------------
  // What the code under test did
  //----------------------------------------------------------------------------

  // Everything written with ShowConsoleMsg(), in order.
  const std::string& GetConsoleText() const { return console_text_; }

 private:
  friend class TestControlSurface;

  // The fake's implementation of each function on the API list.
  class Api;

  // REAPER's plugin_info functions.
  static void* GetFunc(const char* name);
  static int Register(const char* name, void* info);

  // Returns the project `project` points to, or the current project if it is
  // null. If it isn't an open project, this fails the test, and returns the
  // current project so the caller can carry on.
  FakeProject& FindProject(ReaProject* project);

  // Returns the track `track_id` points to. If it isn't a track in an open
  // project, this fails the test, and returns a scratch track so the caller
  // can carry on.
  FakeTrack& GetTrack(MediaTrack* track_id);

  // Returns the open project `track` is in, which GetTrack() has checked.
  FakeProject& GetProjectOf(const FakeTrack& track);

  // Returns the open project `track` is in, or null if none is.
  FakeProject* FindOpenProject(const FakeTrack* track);

  // Creates a project, numbered in the order they are created.
  FakeProject* CreateProject();

  // For TestControlSurface: advances the clock by one run, and forgets a
  // surface it destroyed.
  void AdvanceRun() { ++run_count_; }
  void RemoveSurface(TestControlSurface* surface);

  //----------------------------------------------------------------------------
  // Checks (see the class comment)
  //----------------------------------------------------------------------------

  // PreventUIRefresh(), and a change the TrackBatch rule covers.
  void OnPreventUIRefresh(int count);
  void OnBatchedChange(const FakeTrack* track);

  // Ends a PreventUIRefresh() scope, counting it as one change if anything
  // changed in it.
  void EndBatch();

  // Checks the entry point that just ended, and starts the next one.
  void CheckEntryPoint();

  // The one instance that exists, if any.
  static FakeReaper* s_instance_;

  reaper_plugin_info_t plugin_info_ = {};
  reaper_csurf_reg_t* surface_reg_ = nullptr;
  TestControlSurface* surface_ = nullptr;  // The open surface, if any.

  // The clock: whole runs, and anything AdvanceTime() added.
  int64_t run_count_ = 0;
  absl::Duration advanced_time_;

  // Every project the fake has had, open or closed. Closed projects are kept,
  // so their tracks' pointers are never reused, and a call with one is caught.
  std::vector<std::unique_ptr<FakeProject>> projects_;

  // The open projects, in tab order, and the index of the current one.
  std::vector<FakeProject*> open_projects_;
  int current_project_ = 0;

  // The MIDI ports, in the order they are listed.
  std::vector<std::unique_ptr<FakeMidiInput>> midi_inputs_;
  std::vector<std::unique_ptr<FakeMidiOutput>> midi_outputs_;

  FakeTrack unknown_track_;  // See GetTrack().
  GUID unknown_guid_ = {};   // The unknown track's GUID (see GetTrackGUID()).
  std::string console_text_;

  // The current entry point's changes, for the TrackBatch rule.
  int batch_depth_ = 0;            // PreventUIRefresh()'s count.
  bool changed_in_batch_ = false;  // A change in the current scope.
  int change_count_ = 0;
  absl::flat_hash_set<const FakeTrack*> changed_tracks_;
};

}  // namespace jpr
