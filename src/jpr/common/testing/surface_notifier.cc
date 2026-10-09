// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/surface_notifier.h"

#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

namespace {

// The key IsKeyDown() is asked about during a volume or pan change (VK_SHIFT).
constexpr int kShiftKey = 0x10;

}  // namespace

SurfaceNotifier::SurfaceNotifier(FakeReaper* reaper) : reaper_(reaper) {}

//------------------------------------------------------------------------------
// Hooked functions
//------------------------------------------------------------------------------

void SurfaceNotifier::OnPreventUIRefresh(decltype(PreventUIRefresh) original,
                                         int count) {
  original(count);
  if (count >= 0 || reaper_->IsUIRefreshPrevented()) {
    return;
  }

  // Taken first, as a call on the surface may change more.
  const auto rec_arm = std::exchange(batch_rec_arm_, {});
  const auto selected = std::exchange(batch_selected_, {});
  IReaperControlSurface* surface = GetWrappedSurface();
  if (surface == nullptr) {
    GetUnsent().clear();
    return;
  }
  if (rec_arm.empty() && selected.empty() && GetUnsent().empty()) {
    return;
  }

  // After a rec arm, every track's state, with a track whose rec arm changed
  // sent as it changes outside a batch.
  const bool track_list_changed = !rec_arm.empty();
  if (track_list_changed) {
    surface->SetTrackListChange();
    SendMasterSolo(*surface);
  }
  for (FakeTrack* track : GetTracks()) {
    if (rec_arm.contains(track)) {
      SendTitleRecArmAndSelected(*surface, track);
      SendRecArmChange(*surface, track);
    } else {
      if (track_list_changed) {
        SendState(*surface, track);
      }
      SendRefresh(*surface, track);
    }
    if (selected.contains(track)) {
      surface->SetSurfaceSelected(ToMediaTrack(track), track->selected);
    }
  }
}

int SurfaceNotifier::OnSetMuteOrSolo(decltype(SetTrackUIMute) original,
                                     MediaTrack* track, int value,
                                     int group_flags) {
  const std::vector<TrackFlags> before = GetFlags();
  const int result = original(track, value, group_flags);
  AddUnsent(before);
  if (IReaperControlSurface* surface = GetWrappedSurface()) {
    SendMasterSolo(*surface);
  }
  return result;
}

int SurfaceNotifier::OnSetTrackUIRecArm(decltype(SetTrackUIRecArm) original,
                                        MediaTrack* track, int rec_arm,
                                        int group_flags) {
  const std::vector<TrackFlags> before = GetFlags();
  const int result = original(track, rec_arm, group_flags);
  std::vector<FakeTrack*> changed = GetChanges(before);
  IReaperControlSurface* surface = GetWrappedSurface();
  if (changed.empty() || surface == nullptr) {
    return result;
  }

  // The original changed a track, so `track` is one of the fake's. A change
  // that ganging or grouping could take to other tracks is sent as a change to
  // every track, whatever it changed.
  FakeTrack* fake_track = ToFakeTrack(track);
  if (IsGanged(*fake_track, group_flags) ||
      (IsGrouped(group_flags) && fake_track->group != 0)) {
    changed = GetTracks();
    changed.erase(changed.begin());
  }
  if (reaper_->IsUIRefreshPrevented()) {
    batch_rec_arm_.insert(changed.begin(), changed.end());
    surface->SetTrackListChange();
    SendMasterSolo(*surface);
    return result;
  }
  SendRecArmChange(*surface, fake_track);
  surface->SetTrackListChange();
  for (FakeTrack* each_track : GetTracks()) {
    SendState(*surface, each_track);
  }
  SendMasterSolo(*surface);
  for (FakeTrack* changed_track : changed) {
    SendRecArmChange(*surface, changed_track);
  }
  return result;
}

void SurfaceNotifier::OnSetOnlyTrackSelected(
    decltype(SetOnlyTrackSelected) original, MediaTrack* track) {
  const std::vector<TrackFlags> before = GetFlags();
  original(track);
  SendSelectionChanges(GetChanges(before));
}

void SurfaceNotifier::OnSetTrackSelected(decltype(SetTrackSelected) original,
                                         MediaTrack* track, bool selected) {
  const std::vector<TrackFlags> before = GetFlags();
  original(track, selected);
  SendSelectionChanges(GetChanges(before));
}

double SurfaceNotifier::OnVolumeOrPanChange(
    decltype(CSurf_OnVolumeChangeEx) original, MediaTrack* track, double value,
    bool relative, bool allow_gang) {
  const double result = original(track, value, relative, allow_gang);
  if (IReaperControlSurface* surface = GetWrappedSurface()) {
    surface->IsKeyDown(kShiftKey);
    surface->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, track, nullptr, nullptr);
  }
  return result;
}

bool SurfaceNotifier::OnSetTrackSendUIVol(decltype(SetTrackSendUIVol) original,
                                          MediaTrack* track, int index,
                                          double volume, int end_edit) {
  const bool result = original(track, index, volume, end_edit);
  if (result && end_edit != kEndEdit) {
    SendRouteChange(track, index, /*pan=*/false);
  }
  return result;
}

bool SurfaceNotifier::OnSetTrackSendUIPan(decltype(SetTrackSendUIPan) original,
                                          MediaTrack* track, int index,
                                          double pan, int end_edit) {
  const bool result = original(track, index, pan, end_edit);
  if (result && end_edit != kEndEdit) {
    SendRouteChange(track, index, /*pan=*/true);
  }
  return result;
}

void SurfaceNotifier::OnSetGlobalAutomationOverride(
    decltype(SetGlobalAutomationOverride) original, int mode) {
  original(mode);
  if (IReaperControlSurface* surface = GetWrappedSurface()) {
    SendAutomationChange(*surface);
  }
}

void SurfaceNotifier::OnMainOnCommand(decltype(Main_OnCommand) original,
                                      int command, int flag) {
  const bool auto_mode_action =
      command >= kFirstAutoModeAction && command <= kLastAutoModeAction;
  std::vector<TrackFlags> before;
  if (auto_mode_action) {
    before = GetFlags();
  }
  const int undo_count = reaper_->GetProject().GetUndoCount();
  original(command, flag);
  IReaperControlSurface* surface = GetWrappedSurface();
  if (surface == nullptr) {
    return;
  }
  if (command == kUndoAction || command == kRedoAction) {
    if (reaper_->GetProject().GetUndoCount() != undo_count) {
      SendUndo(*surface);
    }
  } else if (auto_mode_action) {
    surface->SetAutoMode(command - kFirstAutoModeAction);
    if (!GetChanges(before).empty()) {
      SendAutomationChange(*surface);
    }
  }
}

//------------------------------------------------------------------------------
// Changes made in REAPER's own UI
//------------------------------------------------------------------------------

void SurfaceNotifier::ClickTrack(FakeTrack* track) {
  if (CheckOutsideBatch() &&
      CheckTraced(!IsMaster(track), "a click on the master")) {
    ClickToSelect(track, /*only=*/true);
  }
}

void SurfaceNotifier::CtrlClickTrack(FakeTrack* track) {
  if (CheckOutsideBatch() &&
      CheckTraced(!IsMaster(track), "a Ctrl+click on the master") &&
      CheckTraced(!track->selected, "a Ctrl+click that unselects a track")) {
    ClickToSelect(track, /*only=*/false);
  }
}

void SurfaceNotifier::ClickMute(FakeTrack* track) {
  // REAPER may mute every selected track, when the one clicked is selected.
  const int selected_count = static_cast<int>(
      reaper_->GetProject().GetSelectedTracks(/*include_master=*/true).size());
  if (!CheckOutsideBatch() ||
      !CheckTraced(!IsMaster(track), "a click on the master's mute") ||
      !CheckTraced(!track->selected || selected_count == 1,
                   "a click on the mute of one of several selected tracks")) {
    return;
  }
  track->mute = !track->mute;
  if (TestControlSurface* surface = reaper_->GetSurface()) {
    surface->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, ToMediaTrack(track),
                      nullptr, nullptr);
    SendMuteAndSolo(*surface, track);
  }
}

void SurfaceNotifier::ClickToSelect(FakeTrack* clicked, bool only) {
  std::vector<FakeTrack*> changed;
  for (FakeTrack* track : GetTracks()) {
    const bool selected = track == clicked || (!only && track->selected);
    if (track->selected != selected) {
      track->selected = selected;
      changed.push_back(track);
    }
  }
  TestControlSurface* surface = reaper_->GetSurface();
  if (surface == nullptr) {
    return;
  }
  for (FakeTrack* track : changed) {
    surface->SetSurfaceSelected(ToMediaTrack(track), track->selected);
  }
  surface->OnTrackSelection(ToMediaTrack(clicked));
  surface->Extended(CSURF_EXT_SETLASTTOUCHEDTRACK, ToMediaTrack(clicked),
                    nullptr, nullptr);
}

bool SurfaceNotifier::IsMaster(const FakeTrack* track) {
  return track == reaper_->GetProject().GetMasterTrack();
}

bool SurfaceNotifier::CheckOutsideBatch() {
  if (reaper_->IsUIRefreshPrevented()) {
    ADD_FAILURE() << "A change in REAPER's UI was made inside a "
                     "PreventUIRefresh() scope, which REAPER's UI can't do";
    return false;
  }
  return true;
}

bool SurfaceNotifier::CheckTraced(bool traced, std::string_view change) {
  if (!traced) {
    ADD_FAILURE() << "No trace has shown what REAPER calls for " << change;
  }
  return traced;
}

IReaperControlSurface* SurfaceNotifier::GetWrappedSurface() {
  TestControlSurface* surface = reaper_->GetSurface();
  return surface != nullptr ? surface->GetWrapped() : nullptr;
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

SurfaceNotifier::TrackFlags SurfaceNotifier::GetFlags(const FakeTrack& track) {
  return {track.mute, track.solo, track.rec_arm, track.selected,
          track.auto_mode};
}

std::vector<SurfaceNotifier::TrackFlags> SurfaceNotifier::GetFlags() {
  const std::vector<FakeTrack*> tracks = GetTracks();
  std::vector<TrackFlags> flags;
  flags.reserve(tracks.size());
  for (const FakeTrack* track : tracks) {
    flags.push_back(GetFlags(*track));
  }
  return flags;
}

std::vector<FakeTrack*> SurfaceNotifier::GetChanges(
    const std::vector<TrackFlags>& before) {
  const std::vector<FakeTrack*> tracks = GetTracks();
  std::vector<FakeTrack*> changed;
  for (int i = 0; i < static_cast<int>(before.size()); ++i) {
    if (before[i] != GetFlags(*tracks[i])) {
      changed.push_back(tracks[i]);
    }
  }
  return changed;
}

void SurfaceNotifier::SendSelectionChanges(
    const std::vector<FakeTrack*>& changed) {
  if (reaper_->IsUIRefreshPrevented()) {
    batch_selected_.insert(changed.begin(), changed.end());
    return;
  }
  if (IReaperControlSurface* surface = GetWrappedSurface()) {
    for (FakeTrack* track : changed) {
      surface->SetSurfaceSelected(ToMediaTrack(track), track->selected);
    }
  }
}

//------------------------------------------------------------------------------
// What isn't sent yet
//------------------------------------------------------------------------------

absl::flat_hash_map<const FakeTrack*, SurfaceNotifier::Unsent>&
SurfaceNotifier::GetUnsent() {
  // REAPER sends them before each run.
  if (unsent_run_ != reaper_->GetRuns()) {
    unsent_.clear();
    unsent_run_ = reaper_->GetRuns();
  }
  return unsent_;
}

void SurfaceNotifier::AddUnsent(const std::vector<TrackFlags>& before) {
  const std::vector<FakeTrack*> tracks = GetTracks();
  for (int i = 0; i < static_cast<int>(before.size()); ++i) {
    const FakeTrack* track = tracks[i];
    if (before[i].mute != track->mute || before[i].solo != track->solo) {
      // Only at the first change, as what was last sent is before it.
      GetUnsent().try_emplace(track, Unsent{before[i].mute, before[i].solo});
    }
  }
}

void SurfaceNotifier::SendRefresh(IReaperControlSurface& surface,
                                  FakeTrack* track) {
  auto& unsent = GetUnsent();
  const auto it = unsent.find(track);
  if (it == unsent.end()) {
    return;
  }
  const Unsent sent = it->second;
  unsent.erase(it);
  if (sent.mute != track->mute || sent.solo != track->solo) {
    SendMuteAndSolo(surface, track);
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
  SendMuteAndSolo(surface, master);
  SendVolumeAndPan(surface, master);
}

void SurfaceNotifier::SendMuteAndSolo(IReaperControlSurface& surface,
                                      FakeTrack* track) {
  surface.SetSurfaceMute(ToMediaTrack(track), track->mute);
  if (!IsMaster(track)) {
    surface.SetSurfaceSolo(ToMediaTrack(track), track->solo);
  }
  GetUnsent().erase(track);
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
  SendVolumeAndPan(surface, track);
  if (!GetUnsent().contains(track)) {
    SendMuteAndSolo(surface, track);
  }
  SendTitleRecArmAndSelected(surface, track);
}

void SurfaceNotifier::SendTitleRecArmAndSelected(IReaperControlSurface& surface,
                                                 FakeTrack* track) {
  surface.SetTrackTitle(ToMediaTrack(track), track->name.c_str());
  surface.SetSurfaceRecArm(ToMediaTrack(track), track->rec_arm);
  surface.SetSurfaceSelected(ToMediaTrack(track), track->selected);
}

void SurfaceNotifier::SendRecArmChange(IReaperControlSurface& surface,
                                       FakeTrack* track) {
  surface.SetSurfaceRecArm(ToMediaTrack(track), track->rec_arm);
  SendMuteAndSolo(surface, track);
  SendVolumeAndPan(surface, track);
}

void SurfaceNotifier::SendAutomationChange(IReaperControlSurface& surface) {
  for (FakeTrack* track : GetTracks()) {
    SendVolumeAndPan(surface, track);
    surface.SetSurfaceSelected(ToMediaTrack(track), track->selected);
  }
}

void SurfaceNotifier::SendRouteChange(MediaTrack* track, int index, bool pan) {
  // The original found the route, so `track` is one of the fake's. A track in
  // another project has none in the current one.
  IReaperControlSurface* surface = GetWrappedSurface();
  const FakeProject& project = reaper_->GetProject();
  const FakeRoute* route =
      project.GetTrackSendUiRoute(ToFakeTrack(track), index);
  if (surface == nullptr || route == nullptr) {
    return;
  }
  int send_index = project.GetTrackSendUiIndex(route);
  double value = pan ? route->pan : route->volume;
  surface->Extended(pan ? CSURF_EXT_SETSENDPAN : CSURF_EXT_SETSENDVOLUME,
                    ToMediaTrack(route->source), &send_index, &value);
  if (route->destination != nullptr) {
    int receive_index = project.GetReceiveIndex(route);
    surface->Extended(pan ? CSURF_EXT_SETRECVPAN : CSURF_EXT_SETRECVVOLUME,
                      ToMediaTrack(route->destination), &receive_index, &value);
  }
}

void SurfaceNotifier::SendUndo(IReaperControlSurface& surface) {
  // The master, and the project.
  SendMasterMuteVolumeAndPan(surface);
  surface.SetRepeatState(reaper_->GetToggleState(kRepeatAction) == 1);
  double bpm = FakeProject::kBeatsPerMinute;
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
    SendRecArmChange(surface, project.GetTrack(i));
  }
}

}  // namespace jpr
