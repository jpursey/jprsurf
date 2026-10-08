// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/recording_surface.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

IReaperControlSurface* CreateRecordingSurface(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  return new RecordingSurface;
}

reaper_csurf_reg_t g_registration = {"RECORDING", "Recording surface",
                                     &CreateRecordingSurface, nullptr};

// Formats a double that may be null.
std::string GetOptionalText(void* value) {
  return value != nullptr ? absl::StrCat(*static_cast<double*>(value)) : "null";
}

// Returns the name of an Extended() call about a route.
std::string_view GetRouteCallText(int call) {
  switch (call) {
    case CSURF_EXT_SETSENDVOLUME:
      return "SETSENDVOLUME";
    case CSURF_EXT_SETSENDPAN:
      return "SETSENDPAN";
    case CSURF_EXT_SETRECVVOLUME:
      return "SETRECVVOLUME";
    case CSURF_EXT_SETRECVPAN:
      return "SETRECVPAN";
  }
  return "?";
}

}  // namespace

RecordingSurface* RecordingSurface::s_instance_ = nullptr;

reaper_csurf_reg_t* RecordingSurface::GetRegistration() {
  return &g_registration;
}

std::string RecordingSurface::GetTrackText(MediaTrack* track) {
  if (track == GetMasterTrack(nullptr)) {
    return "master";
  }
  int flags = 0;
  return GetTrackState(track, &flags);
}

RecordingSurface::RecordingSurface() {
  if (s_instance_ != nullptr) {
    ADD_FAILURE() << "A second RecordingSurface was created";
    return;
  }
  s_instance_ = this;
}

RecordingSurface::~RecordingSurface() {
  if (s_instance_ == this) {
    s_instance_ = nullptr;
  }
}

std::vector<std::string> RecordingSurface::TakeCalls() {
  return std::exchange(calls_, {});
}

void RecordingSurface::Run() {
  if (on_run_ == nullptr) {
    return;
  }

  // Held here while it runs, so it can replace itself.
  const int set_count = on_run_set_count_;
  absl::AnyInvocable<void()> on_run = std::move(on_run_);
  on_run();
  if (on_run_set_count_ == set_count) {
    on_run_ = std::move(on_run);
  }
}

void RecordingSurface::SetOnRun(absl::AnyInvocable<void()> on_run) {
  on_run_ = std::move(on_run);
  ++on_run_set_count_;
}

void RecordingSurface::SetTrackListChange() {
  calls_.push_back("SetTrackListChange()");
}

void RecordingSurface::SetSurfaceVolume(MediaTrack* track, double volume) {
  Record("SetSurfaceVolume", track, volume);
}

void RecordingSurface::SetSurfacePan(MediaTrack* track, double pan) {
  Record("SetSurfacePan", track, pan);
}

void RecordingSurface::SetSurfaceMute(MediaTrack* track, bool mute) {
  Record("SetSurfaceMute", track, GetBoolText(mute));
}

void RecordingSurface::SetSurfaceSelected(MediaTrack* track, bool selected) {
  Record("SetSurfaceSelected", track, GetBoolText(selected));
}

void RecordingSurface::SetSurfaceSolo(MediaTrack* track, bool solo) {
  Record("SetSurfaceSolo", track, GetBoolText(solo));
}

void RecordingSurface::SetSurfaceRecArm(MediaTrack* track, bool rec_arm) {
  Record("SetSurfaceRecArm", track, GetBoolText(rec_arm));
}

void RecordingSurface::SetPlayState(bool play, bool pause, bool rec) {
  calls_.push_back(absl::StrCat("SetPlayState(", GetBoolText(play), ", ",
                                GetBoolText(pause), ", ", GetBoolText(rec),
                                ")"));
}

void RecordingSurface::SetRepeatState(bool repeat) {
  calls_.push_back(absl::StrCat("SetRepeatState(", GetBoolText(repeat), ")"));
}

void RecordingSurface::SetTrackTitle(MediaTrack* track, const char* title) {
  Record("SetTrackTitle", track, title);
}

bool RecordingSurface::GetTouchState(MediaTrack* track, int is_pan) {
  Record("GetTouchState", track, is_pan);
  return false;
}

void RecordingSurface::SetAutoMode(int mode) {
  calls_.push_back(absl::StrCat("SetAutoMode(", mode, ")"));
}

void RecordingSurface::ResetCachedVolPanStates() {
  calls_.push_back("ResetCachedVolPanStates()");
}

void RecordingSurface::OnTrackSelection(MediaTrack* track) {
  calls_.push_back(absl::StrCat("OnTrackSelection(", GetTrackText(track), ")"));
}

bool RecordingSurface::IsKeyDown(int key) {
  calls_.push_back(absl::StrCat("IsKeyDown(", key, ")"));
  return false;
}

int RecordingSurface::Extended(int call, void* param1, void* param2,
                               void* param3) {
  switch (call) {
    case CSURF_EXT_SETLASTTOUCHEDTRACK:
      calls_.push_back(
          absl::StrCat("Extended(SETLASTTOUCHEDTRACK, ",
                       GetTrackText(static_cast<MediaTrack*>(param1)), ")"));
      break;
    case CSURF_EXT_SETPAN_EX:
      calls_.push_back(
          absl::StrCat("Extended(SETPAN_EX, ",
                       GetTrackText(static_cast<MediaTrack*>(param1)), ", ",
                       *static_cast<double*>(param2), ", ",
                       *static_cast<int*>(param3), ")"));
      break;
    case CSURF_EXT_SETSENDVOLUME:
    case CSURF_EXT_SETSENDPAN:
    case CSURF_EXT_SETRECVVOLUME:
    case CSURF_EXT_SETRECVPAN:
      calls_.push_back(
          absl::StrCat("Extended(", GetRouteCallText(call), ", ",
                       GetTrackText(static_cast<MediaTrack*>(param1)), ", ",
                       *static_cast<int*>(param2), ", ",
                       *static_cast<double*>(param3), ")"));
      break;
    case CSURF_EXT_SETBPMANDPLAYRATE:
      calls_.push_back(absl::StrCat("Extended(SETBPMANDPLAYRATE, ",
                                    GetOptionalText(param1), ", ",
                                    GetOptionalText(param2), ")"));
      break;
    case CSURF_EXT_SETMIXERSCROLL:
    case CSURF_EXT_SETINPUTMONITOR:
      // About state the fake doesn't hold (see SurfaceNotifier).
      break;
    default:
      calls_.push_back(
          absl::StrFormat("Extended(0x%08x)", static_cast<unsigned>(call)));
      break;
  }
  return 0;
}

}  // namespace jpr
