// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
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
  // calls SetTrackListChange(), as REAPER does when the project loads, and
  // runs until the X-Touches show the project. A test builds its project
  // first.
  void AddSurface();

  // Runs the surface until the X-Touches show the project as it is now. That
  // takes two runs, as each run reads the project before it acts: what a run's
  // actions change on other tracks is read on the next, and a fader's position
  // is sent when its device next runs.
  void RunUntilShown();

  //----------------------------------------------------------------------------
  // Strips
  //
  // Strips are numbered across the surface: 0-7 on the extender, and 8-15 on
  // the X-Touch, or 0-7 on the X-Touch alone.
  //----------------------------------------------------------------------------

  // Returns the X-Touch `strip` is on, and its strip there.
  FakeXTouch& GetXTouch(int strip);
  static int GetXTouchStrip(int strip) { return strip % 8; }

  // Returns the name `strip` shows, without the spaces after it.
  std::string GetName(int strip);

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
  std::optional<FakeXTouch> xtouch_ext_;  // Strips 1-8, if there is one.
  FakeXTouch xtouch_;                     // Strips 9-16, or 1-8 alone.
  std::unique_ptr<TestControlSurface> surface_;

 private:
  // Runs until whatever a button press started is done, and shown.
  void Settle();
};

}  // namespace jpr
