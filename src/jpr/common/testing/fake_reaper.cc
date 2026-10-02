// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_reaper.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
#include "jpr/common/testing/test_control_surface.h"

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

  static int CountTracks(ReaProject* project) {
    return s_instance_->FindProject(project).GetTrackCount();
  }

  static MediaTrack* GetTrack(ReaProject* project, int index) {
    FakeProject& fake_project = s_instance_->FindProject(project);
    if (index < 0 || index >= fake_project.GetTrackCount()) {
      return nullptr;
    }
    return ToMediaTrack(fake_project.GetTrack(index));
  }

  static MediaTrack* GetMasterTrack(ReaProject* project) {
    return ToMediaTrack(s_instance_->FindProject(project).GetMasterTrack());
  }

  static MediaTrack* GetParentTrack(MediaTrack* track_id) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    return ToMediaTrack(
        s_instance_->GetProjectOf(track).GetParentTrack(&track));
  }

  static GUID* GetTrackGUID(MediaTrack* track_id) {
    return &s_instance_->GetTrack(track_id).guid;
  }

  static double GetMediaTrackInfo_Value(MediaTrack* track_id,
                                        const char* name) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    const std::string_view parameter(name);
    if (parameter == "B_SHOWINMIXER") {
      return track.show_in_mixer ? 1.0 : 0.0;
    }
    if (parameter == "B_SHOWINTCP") {
      return track.show_in_tcp ? 1.0 : 0.0;
    }
    if (parameter == "I_AUTOMODE") {
      return track.auto_mode;
    }
    ADD_FAILURE() << "GetMediaTrackInfo_Value(\"" << parameter
                  << "\") isn't faked yet";
    return 0.0;
  }

  static bool GetSetMediaTrackInfo_String(MediaTrack* track_id,
                                          const char* name, char* value,
                                          bool set) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    if (std::string_view(name) == "P_NAME" && set) {
      track.name = value;
      return true;
    }
    ADD_FAILURE() << "GetSetMediaTrackInfo_String(\"" << name
                  << "\", set=" << set << ") isn't faked yet";
    return false;
  }

  static double Track_GetPeakInfo(MediaTrack* track_id, int channel) {
    const FakeTrack& track = s_instance_->GetTrack(track_id);
    if (channel < 0 || channel >= static_cast<int>(track.peak.size())) {
      ADD_FAILURE() << "Track_GetPeakInfo() for channel " << channel
                    << " isn't faked yet";
      return 0.0;
    }
    return track.peak[channel];
  }

  // Tracks have no routes.
  static int GetTrackNumSends(MediaTrack* track_id, int category) {
    s_instance_->GetTrack(track_id);
    return 0;
  }

  //----------------------------------------------------------------------------
  // Track changes
  //
  // A grouped change also changes every other track in the same group (see
  // FakeTrack::group). SetTrackUI*()'s group flags group the change unless &1
  // ("prevent track grouping") is set. Selection ganging (&2) isn't modeled.
  //----------------------------------------------------------------------------

  static void PreventUIRefresh(int count) {
    s_instance_->OnPreventUIRefresh(count);
  }

  static int SetTrackUIMute(MediaTrack* track_id, int mute, int group_flags) {
    return SetTrackBool(track_id, &FakeTrack::mute, mute, group_flags);
  }

  static int SetTrackUISolo(MediaTrack* track_id, int solo, int group_flags) {
    return SetTrackBool(track_id, &FakeTrack::solo, solo, group_flags);
  }

  static int SetTrackUIRecArm(MediaTrack* track_id, int rec_arm,
                              int group_flags) {
    return SetTrackBool(track_id, &FakeTrack::rec_arm, rec_arm, group_flags);
  }

  static double CSurf_OnVolumeChangeEx(MediaTrack* track_id, double volume,
                                       bool relative, bool allow_gang) {
    return SetTrackDouble(track_id, &FakeTrack::volume, volume, relative,
                          allow_gang, "CSurf_OnVolumeChangeEx");
  }

  static double CSurf_OnPanChangeEx(MediaTrack* track_id, double pan,
                                    bool relative, bool allow_gang) {
    return SetTrackDouble(track_id, &FakeTrack::pan, pan, relative, allow_gang,
                          "CSurf_OnPanChangeEx");
  }

  //----------------------------------------------------------------------------
  // Selection
  //----------------------------------------------------------------------------

  static void SetTrackSelected(MediaTrack* track_id, bool selected) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    track.selected = selected;
  }

  static void SetOnlyTrackSelected(MediaTrack* track_id) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    ReaProject* project = ToReaProject(&s_instance_->GetProjectOf(track));
    for (FakeTrack* other : GetSelectedTracks(project, /*want_master=*/true)) {
      other->selected = false;
    }
    track.selected = true;
  }

  static int CountSelectedTracks(ReaProject* project) {
    return static_cast<int>(
        GetSelectedTracks(project, /*want_master=*/false).size());
  }

  static int CountSelectedTracks2(ReaProject* project, bool want_master) {
    return static_cast<int>(GetSelectedTracks(project, want_master).size());
  }

  static MediaTrack* GetSelectedTrack(ReaProject* project, int index) {
    return GetSelectedTrack2(project, index, /*want_master=*/false);
  }

  static MediaTrack* GetSelectedTrack2(ReaProject* project, int index,
                                       bool want_master) {
    const std::vector<FakeTrack*> selected =
        GetSelectedTracks(project, want_master);
    if (index < 0 || index >= static_cast<int>(selected.size())) {
      return nullptr;
    }
    return ToMediaTrack(selected[index]);
  }

  //----------------------------------------------------------------------------
  // Undo
  //----------------------------------------------------------------------------

  static void Undo_OnStateChangeEx(const char* name, int flags,
                                   int track_parameter) {
    s_instance_->GetProject().undo_points_.push_back({name, flags});
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
  JPR_NOT_FAKED(CreateMIDIInput)
  JPR_NOT_FAKED(CreateMIDIOutput)
  JPR_NOT_FAKED(format_timestr_pos)
  JPR_NOT_FAKED(GetCursorPosition)
  JPR_NOT_FAKED(GetGlobalAutomationOverride)
  JPR_NOT_FAKED(GetMIDIInputName)
  JPR_NOT_FAKED(GetMIDIOutputName)
  JPR_NOT_FAKED(GetNumMIDIInputs)
  JPR_NOT_FAKED(GetNumMIDIOutputs)
  JPR_NOT_FAKED(GetPlayPosition)
  JPR_NOT_FAKED(GetPlayState)
  JPR_NOT_FAKED(GetSetTrackSendInfo)
  JPR_NOT_FAKED(GetToggleCommandState)
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
  JPR_NOT_FAKED(SetGlobalAutomationOverride)
  JPR_NOT_FAKED(SetTrackSendUIPan)
  JPR_NOT_FAKED(SetTrackSendUIVol)
  JPR_NOT_FAKED(stringToGuid)
  JPR_NOT_FAKED(ToggleTrackSendUIMute)
  JPR_NOT_FAKED(Undo_CanRedo2)

 private:
  // SetTrackUI*()'s group flag that keeps a change to its own track.
  static constexpr int kPreventTrackGrouping = 1;

  // Sets a track's mute, solo, or rec arm as SetTrackUIMute() and the like
  // do: `value` toggles it if negative, and otherwise sets it to `value > 0`.
  // Returns the new value.
  static int SetTrackBool(MediaTrack* track_id, bool FakeTrack::*property,
                          int value, int group_flags) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    s_instance_->OnBatchedChange(&track);
    const bool new_value = (value < 0) ? !(track.*property) : (value > 0);
    const bool grouped = (group_flags & kPreventTrackGrouping) == 0;
    for (FakeTrack* changed : GetGroupedTracks(track, grouped)) {
      changed->*property = new_value;
    }
    return new_value ? 1 : 0;
  }

  // Sets a track's volume or pan as CSurf_OnVolumeChangeEx() and the like do,
  // for `function`. Returns the new value.
  static double SetTrackDouble(MediaTrack* track_id,
                               double FakeTrack::*property, double value,
                               bool relative, bool grouped,
                               const char* function) {
    FakeTrack& track = s_instance_->GetTrack(track_id);
    if (relative) {
      ADD_FAILURE() << function << "() with relative isn't faked yet";
      return track.*property;
    }
    for (FakeTrack* changed : GetGroupedTracks(track, grouped)) {
      changed->*property = value;
    }
    return value;
  }

  // Returns `track`, and if `grouped`, every other track in its group.
  static std::vector<FakeTrack*> GetGroupedTracks(FakeTrack& track,
                                                  bool grouped) {
    std::vector<FakeTrack*> tracks = {&track};
    if (!grouped || track.group == 0) {
      return tracks;
    }
    FakeProject& project = s_instance_->GetProjectOf(track);
    for (int i = 0; i < project.GetTrackCount(); ++i) {
      FakeTrack* other = project.GetTrack(i);
      if (other != &track && other->group == track.group) {
        tracks.push_back(other);
      }
    }
    return tracks;
  }

  // Returns the selected tracks in `project`, in order, with the master first
  // if `want_master` is true and it is selected.
  static std::vector<FakeTrack*> GetSelectedTracks(ReaProject* project,
                                                   bool want_master) {
    FakeProject& fake_project = s_instance_->FindProject(project);
    std::vector<FakeTrack*> selected;
    if (want_master && fake_project.GetMasterTrack()->selected) {
      selected.push_back(fake_project.GetMasterTrack());
    }
    for (int i = 0; i < fake_project.GetTrackCount(); ++i) {
      if (fake_project.GetTrack(i)->selected) {
        selected.push_back(fake_project.GetTrack(i));
      }
    }
    return selected;
  }
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
  CheckEntryPoint();
  if (surface_ != nullptr) {
    ADD_FAILURE() << "A control surface is still open when FakeReaper is "
                     "destroyed";
    surface_->Close();
  }
  TestReset::ResetAll();

#define JPR_UNLOAD(name) ::name = nullptr;
  JPR_REAPER_API(JPR_UNLOAD)
#undef JPR_UNLOAD

  s_instance_ = nullptr;
}

//------------------------------------------------------------------------------
// The plugin and its control surface
//------------------------------------------------------------------------------

int FakeReaper::Register(const char* name, void* info) {
  if (std::string_view(name) != "csurf") {
    ADD_FAILURE() << "FakeReaper doesn't support Register(\"" << name << "\")";
    return 0;
  }
  s_instance_->surface_reg_ = static_cast<reaper_csurf_reg_t*>(info);
  return 1;
}

std::unique_ptr<TestControlSurface> FakeReaper::AddSurface(
    std::string_view config) {
  if (surface_reg_ == nullptr) {
    ADD_FAILURE() << "No control surface type is registered";
    return nullptr;
  }
  int errors = 0;
  IReaperControlSurface* surface = surface_reg_->create(
      surface_reg_->type_string, std::string(config).c_str(), &errors);
  CheckEntryPoint();
  if (surface == nullptr) {
    return nullptr;
  }
  if (surface_ != nullptr) {
    ADD_FAILURE() << "A second control surface was created while one is open. "
                     "JPRSurf has one surface, with a listener for each use.";
    delete surface;
    CheckEntryPoint();
    return nullptr;
  }
  auto test_surface = absl::WrapUnique(new TestControlSurface(this, surface));
  surface_ = test_surface.get();
  return test_surface;
}

void FakeReaper::RemoveSurface(TestControlSurface* surface) {
  if (surface == surface_) {
    surface_ = nullptr;
  }
}

//------------------------------------------------------------------------------
// Time
//------------------------------------------------------------------------------

double FakeReaper::GetTime() const {
  // REAPER's clock doesn't start at zero either.
  constexpr double kStartTime = 1000.0;
  return kStartTime + static_cast<double>(run_count_) / kRunsPerSecond +
         absl::ToDoubleSeconds(advanced_time_);
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
  if (FindOpenProject(track) != nullptr) {
    return *track;
  }
  if (std::ranges::any_of(projects_, [track](const auto& project) {
        return project->HadTrack(track);
      })) {
    ADD_FAILURE() << "MediaTrack " << track_id << " is a deleted track";
  } else if (std::ranges::any_of(projects_, [track](const auto& project) {
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

FakeProject& FakeReaper::GetProjectOf(const FakeTrack& track) {
  FakeProject* project = FindOpenProject(&track);
  return project != nullptr ? *project : GetProject();
}

FakeProject* FakeReaper::FindOpenProject(const FakeTrack* track) {
  auto it = std::ranges::find_if(
      open_projects_,
      [track](const FakeProject* project) { return project->HasTrack(track); });
  return it != open_projects_.end() ? *it : nullptr;
}

//------------------------------------------------------------------------------
// Checks
//------------------------------------------------------------------------------

void FakeReaper::OnPreventUIRefresh(int count) {
  batch_depth_ += count;
  if (batch_depth_ < 0) {
    ADD_FAILURE() << "PreventUIRefresh(" << count
                  << ") ended a scope that wasn't started";
    batch_depth_ = 0;
  }
  if (batch_depth_ == 0) {
    EndBatch();
  }
}

void FakeReaper::EndBatch() {
  if (changed_in_batch_) {
    changed_in_batch_ = false;
    ++change_count_;
  }
}

void FakeReaper::OnBatchedChange(const FakeTrack* track) {
  changed_tracks_.insert(track);
  if (batch_depth_ > 0) {
    changed_in_batch_ = true;
  } else {
    ++change_count_;
  }
}

void FakeReaper::CheckEntryPoint() {
  if (batch_depth_ != 0) {
    ADD_FAILURE() << "PreventUIRefresh() is unbalanced by " << batch_depth_
                  << " at the end of an entry point";
    batch_depth_ = 0;
    EndBatch();
  }
  if (change_count_ > 1 && changed_tracks_.size() > 1) {
    ADD_FAILURE() << "Mute, solo, rec arm, or selection changed on "
                  << changed_tracks_.size() << " tracks, in " << change_count_
                  << " separate changes, in one entry point. Change several "
                     "tracks in one TrackBatch, for one UI refresh and one "
                     "undo point.";
  }
  change_count_ = 0;
  changed_tracks_.clear();
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
