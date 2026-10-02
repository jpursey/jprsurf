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

#include "absl/time/time.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
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
// itself. A test that needs a call REAPER would make on the surface makes it
// (see "Seen in traces" in docs/testing_and_profiling.md for what REAPER
// sends). A test that needs one function to behave otherwise hooks it over the
// fake with gb::FunctionHook.
//
// A call to a function on the list that the fake doesn't support yet fails the
// test, naming it, as does a track pointer that isn't the fake's, and a
// control surface left open when the fake is destroyed.
//
// Creating the fake, and destroying it, resets the process state of every
// library linked into the test (see TestReset). Only one may exist at a time.
//==============================================================================

class FakeReaper final {
 public:
  // How many times a second REAPER runs its control surfaces: Run() advances
  // the clock by 1/kRunsPerSecond of a second.
  static constexpr int kRunsPerSecond = 30;

  // Loads the fake as the REAPER API, and resets the process state.
  FakeReaper();

  FakeReaper(const FakeReaper&) = delete;
  FakeReaper& operator=(const FakeReaper&) = delete;

  // Fails the test if a control surface is still open, and destroys it. Then
  // resets the process state, and unloads the API.
  ~FakeReaper();

  //----------------------------------------------------------------------------
  // The plugin and its control surfaces
  //----------------------------------------------------------------------------

  // What REAPER passes the plugin's entry point. Register("csurf") records the
  // control surface type; registering anything else fails the test.
  reaper_plugin_info_t& GetPluginInfo() { return plugin_info_; }

  // Creates a control surface of the registered type from `config`, as REAPER
  // does at startup, or when the user adds one in its preferences. Returns
  // null if the type refused to create one, or if no type is registered
  // (which fails the test).
  IReaperControlSurface* AddSurface(std::string_view config = {});

  // Destroys a surface AddSurface() returned, as REAPER does on exit, or when
  // the user removes it.
  void RemoveSurface(IReaperControlSurface* surface);

  //----------------------------------------------------------------------------
  // Time
  //----------------------------------------------------------------------------

  // Advances the clock by one run, and runs every control surface.
  void Run();

  // Runs until at least `duration` has passed.
  void RunFor(absl::Duration duration);

  // What time_precise() returns, in seconds. Only Run() and RunFor() advance
  // it, so a test never waits on real time.
  double GetTime() const;

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
  // What the code under test did
  //----------------------------------------------------------------------------

  // Everything written with ShowConsoleMsg(), in order.
  const std::string& GetConsoleText() const { return console_text_; }

 private:
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

  // Creates a project, numbered in the order they are created.
  FakeProject* CreateProject();

  // The one instance that exists, if any.
  static FakeReaper* s_instance_;

  reaper_plugin_info_t plugin_info_ = {};
  reaper_csurf_reg_t* surface_reg_ = nullptr;
  std::vector<std::unique_ptr<IReaperControlSurface>> surfaces_;
  int64_t run_count_ = 0;  // The clock, in runs.

  // Every project the fake has had, open or closed. Closed projects are kept,
  // so their tracks' pointers are never reused, and a call with one is caught.
  std::vector<std::unique_ptr<FakeProject>> projects_;

  // The open projects, in tab order, and the index of the current one.
  std::vector<FakeProject*> open_projects_;
  int current_project_ = 0;

  FakeTrack unknown_track_;  // See GetTrack().
  std::string console_text_;
};

}  // namespace jpr
