// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/surface_notifier.h"

#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

namespace {

// The key IsKeyDown() is asked about during a volume or pan change (VK_SHIFT).
constexpr int kShiftKey = 0x10;

// CSURF_EXT_SETPAN_EX's pan mode for REAPER 4 and later's balance, which is
// what every track in the traces had.
constexpr int kBalancePanMode = 3;

// Actions that call back.
constexpr int kUndoCommand = 40029;
constexpr int kRedoCommand = 40030;
constexpr int kRepeatCommand = 1068;

// The automation mode actions, which set the selected tracks' modes from trim
// (40400) to latch (40404), in the order of their I_AUTOMODE values.
constexpr int kFirstAutoModeCommand = 40400;
constexpr int kLastAutoModeCommand = 40404;
static_assert(kLastAutoModeCommand - kFirstAutoModeCommand ==
              static_cast<int>(AutoMode::kLatch));

}  // namespace

SurfaceNotifier::SurfaceNotifier(FakeReaper* reaper) : reaper_(reaper) {}

//------------------------------------------------------------------------------
// Hooked functions
//------------------------------------------------------------------------------

void SurfaceNotifier::OnPreventUIRefresh(decltype(PreventUIRefresh) original,
                                         int count) {
  original(count);
  if (count < 0) {
    SendIfNoBatch();
  }
}

int SurfaceNotifier::OnSetMuteOrSolo(decltype(SetTrackUIMute) original,
                                     MediaTrack* track, int value,
                                     int group_flags) {
  const std::vector<TrackFlags> before = GetFlags();
  const int result = original(track, value, group_flags);
  AddChanges(before, pending_mute_solo_);
  if (IReaperControlSurface* surface = reaper_->GetSurface()) {
    SendMasterSolo(*surface);
  }
  SendIfNoBatch();
  return result;
}

int SurfaceNotifier::OnSetTrackUIRecArm(decltype(SetTrackUIRecArm) original,
                                        MediaTrack* track, int rec_arm,
                                        int group_flags) {
  const int result = original(track, rec_arm, group_flags);
  if (IReaperControlSurface* surface = reaper_->GetSurface()) {
    surface->SetTrackListChange();
    SendMasterSolo(*surface);
  }
  pending_track_list_ = true;
  SendIfNoBatch();
  return result;
}

void SurfaceNotifier::OnSetOnlyTrackSelected(
    decltype(SetOnlyTrackSelected) original, MediaTrack* track) {
  const std::vector<TrackFlags> before = GetFlags();
  original(track);
  AddChanges(before, pending_selected_);
  SendIfNoBatch();
}

void SurfaceNotifier::OnSetTrackSelected(decltype(SetTrackSelected) original,
                                         MediaTrack* track, bool selected) {
  const std::vector<TrackFlags> before = GetFlags();
  original(track, selected);
  AddChanges(before, pending_selected_);
  SendIfNoBatch();
}

double SurfaceNotifier::OnVolumeOrPanChange(
    decltype(CSurf_OnVolumeChangeEx) original, MediaTrack* track, double value,
    bool relative, bool allow_gang) {
  const double result = original(track, value, relative, allow_gang);
  if (IReaperControlSurface* surface = reaper_->GetSurface()) {
    surface->IsKeyDown(kShiftKey);
    surface->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, track, nullptr, nullptr);
  }
  return result;
}

void SurfaceNotifier::OnSetGlobalAutomationOverride(
    decltype(SetGlobalAutomationOverride) original, int mode) {
  original(mode);
  if (IReaperControlSurface* surface = reaper_->GetSurface()) {
    SendAutomationChange(*surface);
  }
}

void SurfaceNotifier::OnMainOnCommand(decltype(Main_OnCommand) original,
                                      int command, int flag) {
  original(command, flag);
  IReaperControlSurface* surface = reaper_->GetSurface();
  if (surface == nullptr) {
    return;
  }
  if (command == kUndoCommand || command == kRedoCommand) {
    SendUndo(*surface);
  } else if (command >= kFirstAutoModeCommand &&
             command <= kLastAutoModeCommand) {
    surface->SetAutoMode(command - kFirstAutoModeCommand);
    SendAutomationChange(*surface);
  }
}

//------------------------------------------------------------------------------
// Track changes
//------------------------------------------------------------------------------

std::vector<FakeTrack*> SurfaceNotifier::GetTracks() {
  FakeProject& project = reaper_->GetProject();
  std::vector<FakeTrack*> tracks;
  tracks.reserve(project.GetTrackCount() + 1);
  tracks.push_back(project.GetMasterTrack());
  for (int i = 0; i < project.GetTrackCount(); ++i) {
    tracks.push_back(project.GetTrack(i));
  }
  return tracks;
}

std::vector<SurfaceNotifier::TrackFlags> SurfaceNotifier::GetFlags() {
  const std::vector<FakeTrack*> tracks = GetTracks();
  std::vector<TrackFlags> flags;
  flags.reserve(tracks.size());
  for (const FakeTrack* track : tracks) {
    flags.push_back({track->mute, track->solo, track->selected});
  }
  return flags;
}

void SurfaceNotifier::AddChanges(
    const std::vector<TrackFlags>& before,
    absl::flat_hash_set<const FakeTrack*>& changed) {
  // None of the hooked setters adds or removes tracks.
  const std::vector<FakeTrack*> tracks = GetTracks();
  for (int i = 0; i < static_cast<int>(before.size()); ++i) {
    const FakeTrack* track = tracks[i];
    if (before[i] != TrackFlags{track->mute, track->solo, track->selected}) {
      changed.insert(track);
    }
  }
}

void SurfaceNotifier::SendIfNoBatch() {
  if (!reaper_->IsUIRefreshPrevented()) {
    SendPending();
  }
}

void SurfaceNotifier::SendPending() {
  // Taken first, as a call on the surface may change more.
  const auto mute_solo = std::exchange(pending_mute_solo_, {});
  const auto selected = std::exchange(pending_selected_, {});
  const bool track_list = std::exchange(pending_track_list_, false);
  IReaperControlSurface* surface = reaper_->GetSurface();
  if (surface == nullptr ||
      (mute_solo.empty() && selected.empty() && !track_list)) {
    return;
  }

  // No trace has shown the order when a batch changes more than one of these.
  const std::vector<FakeTrack*> tracks = GetTracks();
  for (FakeTrack* track : tracks) {
    if (mute_solo.contains(track)) {
      surface->SetSurfaceMute(ToMediaTrack(track), track->mute);
      surface->SetSurfaceSolo(ToMediaTrack(track), track->solo);
    }
    if (selected.contains(track)) {
      surface->SetSurfaceSelected(ToMediaTrack(track), track->selected);
    }
  }
  if (track_list) {
    surface->SetTrackListChange();
    SendMasterSolo(*surface);
    for (FakeTrack* track : tracks) {
      SendState(*surface, track);
    }
  }
}

//------------------------------------------------------------------------------
// Calls on the surface
//------------------------------------------------------------------------------

void SurfaceNotifier::SendMasterSolo(IReaperControlSurface& surface) {
  FakeProject& project = reaper_->GetProject();
  surface.SetSurfaceSolo(ToMediaTrack(project.GetMasterTrack()),
                         project.AnyTrackSolo());
}

void SurfaceNotifier::SendMasterMuteVolumeAndPan(
    IReaperControlSurface& surface) {
  FakeTrack* master = reaper_->GetProject().GetMasterTrack();
  surface.SetSurfaceMute(ToMediaTrack(master), master->mute);
  SendVolumeAndPan(surface, master);
}

void SurfaceNotifier::SendVolumeAndPan(IReaperControlSurface& surface,
                                       FakeTrack* track) {
  surface.SetSurfaceVolume(ToMediaTrack(track), track->volume);
  surface.SetSurfacePan(ToMediaTrack(track), track->pan);
  double pan = track->pan;
  int mode = kBalancePanMode;
  surface.Extended(CSURF_EXT_SETPAN_EX, ToMediaTrack(track), &pan, &mode);
}

void SurfaceNotifier::SendState(IReaperControlSurface& surface,
                                FakeTrack* track) {
  const bool is_master = track == reaper_->GetProject().GetMasterTrack();
  SendVolumeAndPan(surface, track);
  surface.SetSurfaceMute(ToMediaTrack(track), track->mute);
  if (!is_master) {
    surface.SetSurfaceSolo(ToMediaTrack(track), track->solo);
  }
  surface.SetTrackTitle(ToMediaTrack(track), track->name.c_str());
  surface.SetSurfaceRecArm(ToMediaTrack(track), track->rec_arm);
  surface.SetSurfaceSelected(ToMediaTrack(track), track->selected);
}

void SurfaceNotifier::SendAutomationChange(IReaperControlSurface& surface) {
  for (FakeTrack* track : GetTracks()) {
    SendVolumeAndPan(surface, track);
    surface.SetSurfaceSelected(ToMediaTrack(track), track->selected);
  }
}

void SurfaceNotifier::SendUndo(IReaperControlSurface& surface) {
  // The master, and the project.
  SendMasterMuteVolumeAndPan(surface);
  surface.SetRepeatState(reaper_->GetToggleState(kRepeatCommand) == 1);
  double bpm = FakeReaper::kBeatsPerMinute;
  surface.Extended(CSURF_EXT_SETBPMANDPLAYRATE, &bpm, nullptr, nullptr);

  // Every track's state, as a track list change.
  surface.SetTrackListChange();
  for (FakeTrack* track : GetTracks()) {
    SendState(surface, track);
  }

  // Then the master's, and each track's, again.
  SendMasterSolo(surface);
  SendMasterMuteVolumeAndPan(surface);
  FakeProject& project = reaper_->GetProject();
  for (int i = 0; i < project.GetTrackCount(); ++i) {
    FakeTrack* track = project.GetTrack(i);
    surface.SetSurfaceRecArm(ToMediaTrack(track), track->rec_arm);
    surface.SetSurfaceMute(ToMediaTrack(track), track->mute);
    surface.SetSurfaceSolo(ToMediaTrack(track), track->solo);
    SendVolumeAndPan(surface, track);
  }
}

}  // namespace jpr
