// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {

//==============================================================================
// SurfaceTest
//
// A fixture for tests of the whole surface: the plugin, loaded into the fake
// REAPER as REAPER loads it, with a fake X-Touch and extender connected. Tests
// press, move, and turn the X-Touches' controls, run the surface, and check
// the fake's tracks, the actions it ran, and what the X-Touches show.
//
// REAPER's calls back to the surface from inside its functions are made by a
// SurfaceNotifier, and the fake has REAPER's actions (see AddReaperActions()).
// Anything logged at ERROR or above, from the fixture's construction to its
// destruction, fails the test, unless the test takes it from
// log_error_guard_.
//==============================================================================

class SurfaceTest : public ::testing::Test {
 protected:
  // With an extender to the left of the X-Touch, as JPRSurf expects, unless
  // `extender` is false, for the X-Touch alone.
  explicit SurfaceTest(bool extender = true);

  // Removes the surface, and unloads the plugin.
  ~SurfaceTest() override;

  // Loads the plugin, and adds the surface, as REAPER does at startup. Then
  // calls SetTrackListChange() and runs once, as REAPER does when the project
  // loads. A test builds its project first.
  void AddSurface();

  //----------------------------------------------------------------------------
  // The project
  //----------------------------------------------------------------------------

  // Adds `count` tracks to the end of `folder`, or of the current project if
  // it is null, and returns them. Each is named for where it is, after those
  // already there: T1, T2, and so on at the top level, and T2.1, T2.2, and so
  // on in T2.
  std::vector<FakeTrack*> AddTracks(int count, FakeTrack* folder = nullptr);

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

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::optional<FakeXTouch> xtouch_ext_;  // Strips 1-8, if there is one.
  FakeXTouch xtouch_;                     // Strips 9-16, or 1-8 alone.
  std::unique_ptr<TestControlSurface> surface_;

 private:
  // Runs until whatever a button press started is done.
  void Settle();
};

}  // namespace jpr
