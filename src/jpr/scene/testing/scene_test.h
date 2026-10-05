// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/scene/scene.h"

namespace jpr {

//==============================================================================
// SceneTest
//
// The fixture for tests of the scene: the fake REAPER, a SurfaceNotifier, a
// scene, and a FakeDevice whose controls the scene maps. The scene runs as the
// plugin runs it, from a control surface's runs (see Scene), so the track cache
// follows what REAPER calls back: the track list, the selection, and the last
// touched track.
//
// A test adds the controls it needs to the device, builds its project, views,
// and mappings, and then adds the surface (AddSurface()), as REAPER does at
// startup.
//
// Anything logged at ERROR or above fails the test, unless the test takes it
// from log_error_guard_.
//==============================================================================

class SceneTest : public ::testing::Test {
 protected:
  // The name the device is added to the scene with (see GetControlName()).
  static constexpr std::string_view kDeviceName = "Device";

  SceneTest();
  ~SceneTest() override;

  // Calls RemoveSurface(), before any fixture's members go.
  void TearDown() override;

  //----------------------------------------------------------------------------
  // Setting up
  //----------------------------------------------------------------------------

  // Returns the name the scene knows the device's control `name` by, such as
  // "Device/Play".
  static std::string GetControlName(std::string_view name);

  // Adds the surface, as REAPER does at startup, which activates the scene, and
  // runs the devices and the scene on each run. Then calls
  // SetTrackListChange(), as REAPER does when the project loads, and runs until
  // the controls show the project.
  void AddSurface();

  // Removes the surface, if there is one, as REAPER does at exit, which
  // deactivates the scene.
  void RemoveSurface();

  //----------------------------------------------------------------------------
  // Running
  //----------------------------------------------------------------------------

  // Runs the surface until the controls show the project as it is now. That
  // takes two runs, as each run reads the project before it acts: what a run's
  // actions change is read, and shown, on the next.
  void RunUntilShown();

  //----------------------------------------------------------------------------
  // Input
  //
  // Input arrives on the next run, after the controls run and before the
  // scene does, as a device's MIDI input does. A test gives input through
  // these, never on a control's fake inputs directly: the controls clear what
  // arrived since their last run when they run, so the scene would never see
  // it.
  //----------------------------------------------------------------------------

  void Press(const FakeDevice::FakeControl& control);
  void Release(const FakeDevice::FakeControl& control);

  //----------------------------------------------------------------------------
  // Presses
  //
  // Each presses and releases the control, and then runs until whatever the
  // press started is done, including a press held back in case it is a double
  // press, and is shown.
  //----------------------------------------------------------------------------

  void Tap(const FakeDevice::FakeControl& control);

  // Two presses within the double press time.
  void DoublePress(const FakeDevice::FakeControl& control);

  // Held past the long press time.
  void LongPress(const FakeDevice::FakeControl& control);

  // Pressed, and held past the long press time, but not released: the test
  // releases it, after pressing whatever it holds the control for.
  void Hold(const FakeDevice::FakeControl& control);

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  Runner device_runner_{"Device"};
  Runner scene_runner_{"Scene"};
  Scene scene_{"Scene"};

  FakeDevice* const device_;  // Owned by the scene.

  std::unique_ptr<TestControlSurface> surface_;

 private:
  class Listener;

  // Adds the device to the scene, and returns it.
  FakeDevice* AddDevice();

  // Runs until whatever a press started is done, and shown.
  void Settle();

  // Gives the controls the input given since the last run.
  void DeliverInput();

  // The input given since the last run, which arrives on the next.
  std::vector<absl::AnyInvocable<void()>> input_;
};

}  // namespace jpr
