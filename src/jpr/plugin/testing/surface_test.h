// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {

//==============================================================================
// SurfaceTest
//
// A fixture for tests of the whole surface, whatever its config: the plugin,
// loaded into the fake REAPER as REAPER loads it. Each config's tests derive a
// fixture from it that connects the config's fake devices. Tests press, move,
// and turn the devices' controls, run the surface, and check the fake's
// tracks, the actions it ran, and what the devices show.
//
// REAPER's calls back to the surface from inside its functions are made by a
// SurfaceNotifier, and the fake has REAPER's actions (see AddReaperActions()).
// Anything logged at ERROR or above, from the fixture's construction to its
// destruction, fails the test, unless the test takes it from
// log_error_guard_.
//==============================================================================

class SurfaceTest : public ::testing::Test {
 protected:
  SurfaceTest();

  // Calls RemoveSurface(), before any fixture's members go, so the surface
  // goes before the fake devices a fixture derived from this holds.
  void TearDown() override;

  // Loads the plugin, and adds the surface, as REAPER does at startup. Then
  // calls SetTrackListChange(), as REAPER does when the project loads, and
  // runs until the devices show the project. A test builds its project first,
  // and connects its devices.
  void AddSurface();

  // Removes the surface, if there is one, and unloads the plugin, if it is
  // loaded, as REAPER does at exit.
  void RemoveSurface();

  // Runs the surface until the devices show the project as it is now. That
  // takes two runs, as each run reads the project before it acts: what a run's
  // actions change on other tracks is read on the next, and a fader's position
  // is sent when its device next runs.
  void RunUntilShown();

  //----------------------------------------------------------------------------
  // The project
  //----------------------------------------------------------------------------

  // Adds `count` tracks to the end of `folder`, or of the current project if
  // it is null, and returns them, named for where they are (see
  // FakeProject::AddTracks()).
  std::vector<FakeTrack*> AddTracks(int count, FakeTrack* folder = nullptr) {
    return reaper_.GetProject().AddTracks(count, folder);
  }

  //----------------------------------------------------------------------------
  // Presses
  //
  // Each presses and releases a button, and then runs until whatever the press
  // started is done, including a press held back in case it is a double press.
  //----------------------------------------------------------------------------

  void Tap(FakeXTouch& xtouch, FakeXTouch::Button button);
  void Tap(FakeXTouch& xtouch, FakeXTouch::StripButton button, int strip);

  // Two presses within the double press time.
  void DoublePress(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                   int strip);

  // Held past the long press time.
  void LongPress(FakeXTouch& xtouch, FakeXTouch::Button button);

  // Pressed, and held past the long press time, but not released: the test
  // releases it, after pressing whatever it holds the button for.
  void Hold(FakeXTouch& xtouch, FakeXTouch::StripButton button, int strip);

  //----------------------------------------------------------------------------
  // Moves
  //----------------------------------------------------------------------------

  // Touches the fader, moves it to `position`, and lets go, as a hand does,
  // running the surface after the move, and until it is shown after letting
  // go.
  void MoveFader(FakeXTouch& xtouch, int fader, int position);

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;

 private:
  // Runs until whatever a button press started is done, and shown.
  void Settle();
};

}  // namespace jpr
