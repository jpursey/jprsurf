// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// RecordingSurface
//
// A control surface that records every call REAPER makes on it, as text, so a
// test can check what REAPER calls back. Tracks are recorded by name (see
// GetTrackText()), and values as they read, such as
// "SetSurfaceMute(Drums, true)". Its runs aren't recorded, and nor are the
// Extended() calls about state the fake doesn't hold (the mixer's scroll, and
// input monitoring).
//
// It reads track names through REAPER's API, so it works the same under the
// fake REAPER and in REAPER. Its type is registered with REAPER as "RECORDING"
// (see GetRegistration()), and only one may exist at a time.
//==============================================================================

class RecordingSurface final : public IReaperControlSurface {
 public:
  // The surface type's registration, for Register("csurf"), which creates a
  // RecordingSurface.
  static reaper_csurf_reg_t* GetRegistration();

  // Returns the RecordingSurface that exists, or null if none does.
  static RecordingSurface* Get() { return s_instance_; }

  // Returns how `track` is recorded: its name, or "master".
  static std::string GetTrackText(MediaTrack* track);

  // Returns how a bool is recorded: "true" or "false".
  static const char* GetBoolText(bool value) {
    return value ? "true" : "false";
  }

  RecordingSurface();
  RecordingSurface(const RecordingSurface&) = delete;
  RecordingSurface& operator=(const RecordingSurface&) = delete;
  ~RecordingSurface() override;

  // Returns the calls recorded since the last time, and forgets them.
  std::vector<std::string> TakeCalls();

  // Sets what the surface does when it runs, replacing what it did, even from
  // inside a run. It does nothing until this is called.
  void SetOnRun(absl::AnyInvocable<void()> on_run);

  // IReaperControlSurface
  const char* GetTypeString() override { return "RECORDING"; }
  const char* GetDescString() override { return "Recording surface"; }
  const char* GetConfigString() override { return ""; }
  void Run() override;
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
  // Records `function` called with `track` and `value`.
  template <typename Value>
  void Record(std::string_view function, MediaTrack* track,
              const Value& value) {
    calls_.push_back(
        absl::StrCat(function, "(", GetTrackText(track), ", ", value, ")"));
  }

  // The one instance that exists, if any.
  static RecordingSurface* s_instance_;

  std::vector<std::string> calls_;
  absl::AnyInvocable<void()> on_run_;
  int on_run_set_count_ = 0;  // SetOnRun() calls, to see one during a run.
};

}  // namespace jpr
