// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_reaper.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "jpr/common/guid.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/test_reset.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"

namespace jpr {

namespace {

// The return type of a REAPER API function pointer.
template <typename Function>
struct ApiFunction;

template <typename Return_, typename... Args>
struct ApiFunction<Return_ (*)(Args...)> {
  using Return = Return_;
};

// GetTrackState()'s flags.
constexpr int kTrackStateSelected = 2;
constexpr int kTrackStateMute = 8;
constexpr int kTrackStateSolo = 16;
constexpr int kTrackStateRecArm = 64;

}  // namespace

// Declares a function on the API list that the fake doesn't support yet: it
// fails the test, naming the function, and returns a zero value.
#define JPR_NOT_FAKED(name)                                           \
  static constexpr decltype(::name) name = [](auto...) {              \
    ADD_FAILURE() << "REAPER API function " #name " isn't faked yet"; \
    return ApiFunction<decltype(::name)>::Return();                   \
  };

//==============================================================================
// FakeReaper::Api
//
// A static function for each function on the API list, with the same name and
// signature, which FakeReaper::GetFunc() returns.
//==============================================================================

class FakeReaper::Api final {
 public:
  //----------------------------------------------------------------------------
  // Tracks
  //----------------------------------------------------------------------------

  // The projects have no tracks but the master.
  static int CountTracks(ReaProject* project) { return 0; }

  static MediaTrack* GetMasterTrack(ReaProject* project) {
    return ToMediaTrack(s_instance_->FindProject(project).GetMasterTrack());
  }

  static GUID* GetTrackGUID(MediaTrack* track_id) {
    return &s_instance_->GetTrack(track_id).guid;
  }

  static const char* GetTrackState(MediaTrack* track_id, int* flags) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    *flags = (track.selected ? kTrackStateSelected : 0) |
             (track.mute ? kTrackStateMute : 0) |
             (track.solo ? kTrackStateSolo : 0) |
             (track.rec_arm ? kTrackStateRecArm : 0);
    return track.name.c_str();
  }

  static bool GetTrackUIVolPan(MediaTrack* track_id, double* volume,
                               double* pan) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    *volume = track.volume;
    *pan = track.pan;
    return true;
  }

  static int GetTrackColor(MediaTrack* track_id) {
    return s_instance_->GetTrack(track_id).color;
  }

  //----------------------------------------------------------------------------
  // Text and time
  //----------------------------------------------------------------------------

  static void guidToString(const GUID* guid, char* text) {
    absl::SNPrintF(text, 64, "%s", FormatGuid(*guid));
  }

  static double time_precise() { return s_instance_->GetTime(); }

  static void ShowConsoleMsg(const char* message) {
    s_instance_->console_text_ += message;
  }

  //----------------------------------------------------------------------------
  // Not faked yet
  //----------------------------------------------------------------------------

  JPR_NOT_FAKED(AnyTrackSolo)
  JPR_NOT_FAKED(CountSelectedMediaItems)
  JPR_NOT_FAKED(CountSelectedTracks)
  JPR_NOT_FAKED(CountSelectedTracks2)
  JPR_NOT_FAKED(CreateMIDIInput)
  JPR_NOT_FAKED(CreateMIDIOutput)
  JPR_NOT_FAKED(CSurf_OnPanChangeEx)
  JPR_NOT_FAKED(CSurf_OnVolumeChangeEx)
  JPR_NOT_FAKED(format_timestr_pos)
  JPR_NOT_FAKED(GetCursorPosition)
  JPR_NOT_FAKED(GetGlobalAutomationOverride)
  JPR_NOT_FAKED(GetMediaTrackInfo_Value)
  JPR_NOT_FAKED(GetMIDIInputName)
  JPR_NOT_FAKED(GetMIDIOutputName)
  JPR_NOT_FAKED(GetNumMIDIInputs)
  JPR_NOT_FAKED(GetNumMIDIOutputs)
  JPR_NOT_FAKED(GetParentTrack)
  JPR_NOT_FAKED(GetPlayPosition)
  JPR_NOT_FAKED(GetPlayState)
  JPR_NOT_FAKED(GetSelectedTrack)
  JPR_NOT_FAKED(GetSelectedTrack2)
  JPR_NOT_FAKED(GetSetMediaTrackInfo_String)
  JPR_NOT_FAKED(GetSetTrackSendInfo)
  JPR_NOT_FAKED(GetToggleCommandState)
  JPR_NOT_FAKED(GetTrack)
  JPR_NOT_FAKED(GetTrackNumSends)
  JPR_NOT_FAKED(GetTrackReceiveUIMute)
  JPR_NOT_FAKED(GetTrackReceiveUIVolPan)
  JPR_NOT_FAKED(GetTrackSendUIMute)
  JPR_NOT_FAKED(GetTrackSendUIVolPan)
  JPR_NOT_FAKED(IsProjectDirty)
  JPR_NOT_FAKED(kbd_getTextFromCmd)
  JPR_NOT_FAKED(Main_OnCommand)
  JPR_NOT_FAKED(mkpanstr)
  JPR_NOT_FAKED(mkvolstr)
  JPR_NOT_FAKED(NamedCommandLookup)
  JPR_NOT_FAKED(PreventUIRefresh)
  JPR_NOT_FAKED(SetGlobalAutomationOverride)
  JPR_NOT_FAKED(SetOnlyTrackSelected)
  JPR_NOT_FAKED(SetTrackSelected)
  JPR_NOT_FAKED(SetTrackSendUIPan)
  JPR_NOT_FAKED(SetTrackSendUIVol)
  JPR_NOT_FAKED(SetTrackUIMute)
  JPR_NOT_FAKED(SetTrackUIRecArm)
  JPR_NOT_FAKED(SetTrackUISolo)
  JPR_NOT_FAKED(stringToGuid)
  JPR_NOT_FAKED(ToggleTrackSendUIMute)
  JPR_NOT_FAKED(Track_GetPeakInfo)
  JPR_NOT_FAKED(Undo_CanRedo2)
  JPR_NOT_FAKED(Undo_OnStateChangeEx)
};

#undef JPR_NOT_FAKED

FakeReaper* FakeReaper::s_instance_ = nullptr;

FakeReaper::FakeReaper() {
  CHECK(s_instance_ == nullptr) << "Only one FakeReaper may exist at a time";
  s_instance_ = this;

  plugin_info_.caller_version = REAPER_PLUGIN_VERSION;
  plugin_info_.Register = &FakeReaper::Register;
  plugin_info_.GetFunc = &FakeReaper::GetFunc;

  AddProject();

  CHECK(LoadReaperApi(&FakeReaper::GetFunc));
  MidiPorts::SetFlushWait(absl::ZeroDuration());
  TestReset::ResetAll();
}

FakeReaper::~FakeReaper() {
  if (!surfaces_.empty()) {
    ADD_FAILURE() << surfaces_.size()
                  << " control surface(s) still open when FakeReaper was "
                     "destroyed";
    surfaces_.clear();
  }
  TestReset::ResetAll();

#define JPR_UNLOAD(name) ::name = nullptr;
  JPR_REAPER_API(JPR_UNLOAD)
#undef JPR_UNLOAD

  s_instance_ = nullptr;
}

//------------------------------------------------------------------------------
// The plugin and its control surfaces
//------------------------------------------------------------------------------

int FakeReaper::Register(const char* name, void* info) {
  if (std::string_view(name) != "csurf") {
    ADD_FAILURE() << "FakeReaper doesn't support Register(\"" << name << "\")";
    return 0;
  }
  s_instance_->surface_reg_ = static_cast<reaper_csurf_reg_t*>(info);
  return 1;
}

IReaperControlSurface* FakeReaper::AddSurface(std::string_view config) {
  if (surface_reg_ == nullptr) {
    ADD_FAILURE() << "No control surface type is registered";
    return nullptr;
  }
  int errors = 0;
  IReaperControlSurface* surface = surface_reg_->create(
      surface_reg_->type_string, std::string(config).c_str(), &errors);
  if (surface != nullptr) {
    surfaces_.emplace_back(surface);
  }
  return surface;
}

void FakeReaper::RemoveSurface(IReaperControlSurface* surface) {
  auto it = std::ranges::find_if(
      surfaces_, [surface](const auto& open) { return open.get() == surface; });
  if (it == surfaces_.end()) {
    ADD_FAILURE() << "RemoveSurface() was given a surface FakeReaper didn't "
                     "create, or already removed";
    return;
  }
  surfaces_.erase(it);
}

//------------------------------------------------------------------------------
// Time
//------------------------------------------------------------------------------

void FakeReaper::Run() {
  ++run_count_;
  for (const auto& surface : surfaces_) {
    surface->Run();
  }
}

void FakeReaper::RunFor(absl::Duration duration) {
  // Rounds up to whole runs, in integers, so a whole number of seconds is
  // exactly kRunsPerSecond runs each.
  constexpr int64_t kNanosecondsPerSecond = 1'000'000'000;
  const int64_t runs = (absl::ToInt64Nanoseconds(duration) * kRunsPerSecond +
                        kNanosecondsPerSecond - 1) /
                       kNanosecondsPerSecond;
  for (int64_t i = 0; i < runs; ++i) {
    Run();
  }
}

double FakeReaper::GetTime() const {
  // REAPER's clock doesn't start at zero either.
  constexpr double kStartTime = 1000.0;
  return kStartTime + static_cast<double>(run_count_) / kRunsPerSecond;
}

//------------------------------------------------------------------------------
// Projects
//------------------------------------------------------------------------------

FakeProject& FakeReaper::AddProject() {
  open_projects_.push_back(CreateProject());
  current_project_ = GetProjectCount() - 1;
  return GetProject();
}

void FakeReaper::SwitchProjectTo(int index) {
  if (index < 0 || index >= GetProjectCount()) {
    ADD_FAILURE() << "SwitchProjectTo(" << index << ") with only "
                  << GetProjectCount() << " project(s) open";
    return;
  }
  current_project_ = index;
}

FakeProject& FakeReaper::NewProject() {
  open_projects_[current_project_] = CreateProject();
  return GetProject();
}

FakeProject* FakeReaper::CreateProject() {
  const int number = static_cast<int>(projects_.size()) + 1;
  projects_.push_back(absl::WrapUnique(new FakeProject(number)));
  return projects_.back().get();
}

FakeProject& FakeReaper::FindProject(ReaProject* project) {
  if (project == nullptr) {
    return GetProject();
  }
  // The pointer is only compared, until it is known to be a project.
  auto it = std::ranges::find(open_projects_,
                              reinterpret_cast<FakeProject*>(project));
  if (it == open_projects_.end()) {
    ADD_FAILURE() << "ReaProject " << project
                  << " isn't an open project in FakeReaper";
    return GetProject();
  }
  return **it;
}

FakeTrack& FakeReaper::GetTrack(MediaTrack* track_id) {
  // The pointer is only compared, until it is known to be a track.
  FakeTrack* track = reinterpret_cast<FakeTrack*>(track_id);
  if (std::ranges::any_of(open_projects_, [track](const FakeProject* project) {
        return project->HasTrack(track);
      })) {
    return *track;
  }
  if (std::ranges::any_of(projects_, [track](const auto& project) {
        return project->HasTrack(track);
      })) {
    ADD_FAILURE() << "MediaTrack " << track_id
                  << " is a track in a project that is no longer open";
  } else {
    ADD_FAILURE() << "MediaTrack " << track_id
                  << " isn't a track in FakeReaper";
  }
  unknown_track_ = {};
  return unknown_track_;
}

void* FakeReaper::GetFunc(const char* name) {
  // Every function on the list must have a member of Api with the same name
  // and signature, or this doesn't compile.
  const std::string_view requested(name);
#define JPR_FAKE_FUNCTION(name)                                               \
  if (requested == #name) {                                                   \
    return reinterpret_cast<void*>(static_cast<decltype(::name)>(Api::name)); \
  }
  JPR_REAPER_API(JPR_FAKE_FUNCTION)
#undef JPR_FAKE_FUNCTION
  return nullptr;
}

}  // namespace jpr
