// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "absl/time/time.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// FakeReaper creates TestControlSurfaces, which check calls with it.
class FakeReaper;

//==============================================================================
// TestControlSurface
//
// A control surface the fake REAPER created (see FakeReaper::AddSurface()),
// which a test makes REAPER's calls on, as REAPER would. Each call is passed on
// to the surface, and is an entry point of its own: when it returns, the fake
// checks what it did (see "Checks" in FakeReaper).
//
// Destroying it destroys the surface, as REAPER does when the user removes it,
// or on exit. It must be destroyed before the fake.
//==============================================================================

class TestControlSurface final : public IReaperControlSurface {
 public:
  TestControlSurface(const TestControlSurface&) = delete;
  TestControlSurface& operator=(const TestControlSurface&) = delete;
  ~TestControlSurface() override;

  // Advances the fake's clock by one run, and runs the surface, as REAPER does
  // about FakeReaper::kRunsPerSecond times a second.
  void Run() override;

  // Runs until at least `duration` has passed. A whole number of seconds is
  // exactly FakeReaper::kRunsPerSecond runs each.
  void RunFor(absl::Duration duration);

  // The rest of IReaperControlSurface, each passed on to the surface.
  const char* GetTypeString() override;
  const char* GetDescString() override;
  const char* GetConfigString() override;
  void CloseNoReset() override;
  void SetTrackListChange() override;
  void SetSurfaceVolume(MediaTrack* track, double volume) override;
  void SetSurfacePan(MediaTrack* track, double pan) override;
  void SetSurfaceMute(MediaTrack* track, bool mute) override;
  void SetSurfaceSelected(MediaTrack* track, bool selected) override;
  void SetSurfaceSolo(MediaTrack* track, bool solo) override;
  void SetSurfaceRecArm(MediaTrack* track, bool rec_arm) override;
  void SetPlayState(bool play, bool pause, bool rec) override;
  void SetRepeatState(bool repeat) override;
  void SetTrackTitle(MediaTrack* track, const char* title) override;
  bool GetTouchState(MediaTrack* track, int is_pan) override;
  void SetAutoMode(int mode) override;
  void ResetCachedVolPanStates() override;
  void OnTrackSelection(MediaTrack* track) override;
  bool IsKeyDown(int key) override;
  int Extended(int call, void* param1, void* param2, void* param3) override;

 private:
  friend class FakeReaper;

  // Takes ownership of `surface`, which `reaper` created.
  TestControlSurface(FakeReaper* reaper, IReaperControlSurface* surface);

  // Ends a call to the surface: the fake checks it.
  void EndCall();

  // Destroys the surface, as an entry point, and leaves the fake. This is
  // called when this is destroyed, or by the fake if this outlives it.
  void Close();

  FakeReaper* reaper_;
  std::unique_ptr<IReaperControlSurface> surface_;
};

}  // namespace jpr
