// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "gb/base/function_hook.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// SurfaceNotifier
//
// Makes the calls REAPER makes on the open control surface from inside its own
// functions, as traces showed (see "Seen in traces" in
// docs/testing_and_profiling.md), for as long as it exists. A test of a whole
// surface creates one before the plugin loads, so its hooks are under the
// profiler's and the trace's, as REAPER's own functions are. A test of
// ControlSurface itself makes the calls it needs by hand, and has none.
//
// It hooks these functions over the fake, and calls back:
// - SetTrackUIMute() and SetTrackUISolo(): the master's solo (whether any track
//   is soloed), then the mute and solo of each track whose mute or solo
//   changed.
// - SetTrackUIRecArm(): SetTrackListChange() and the master's solo, then the
//   same again, and every track's state.
// - SetOnlyTrackSelected() and SetTrackSelected(): the selection of each track
//   whose selection changed.
// - CSurf_OnVolumeChangeEx() and CSurf_OnPanChangeEx(): IsKeyDown(VK_SHIFT),
//   then the track as the last touched.
// - SetGlobalAutomationOverride(): every track's volume, pan, and selection.
// - Main_OnCommand(), after the action's handler: for the automation mode
//   actions (40400-40404), SetAutoMode() with the mode, then as the override
//   does. For Edit: Undo and Edit: Redo (40029, 40030), everything (see
//   SendUndo()). Redo is taken to call back as Undo does, which no trace has
//   shown.
//
// The track setters make their calls in two parts: the master's solo (and for
// rec arm, the track list change) in the call, and the rest when the
// outermost PreventUIRefresh() scope ends, or in the call when there is none.
// Everything else is in the call.
//
// Tracks are sent master first, then in order, from the current project. It
// makes every call the traces showed, not just those ControlSurface acts on,
// except those about state the fake doesn't hold: the mixer's scroll, and
// input monitoring.
//
// Calls are made on FakeReaper::GetSurface(), so they are part of the entry
// point that called the function (see "Checks" in FakeReaper), and are only
// made while a surface is open.
//
// Brittleness: it knows only what traces showed. A function JPRSurf starts
// calling that makes REAPER call back needs a new trace and a new hook here,
// or tests silently get no callback.
//==============================================================================

class SurfaceNotifier final {
 public:
  // Hooks `reaper`'s functions, which must already be loaded as the REAPER
  // API. `reaper` must outlive this. Hooks installed over these must be
  // removed before this is destroyed (see gb::FunctionHook), so a plugin that
  // hooks the API must be unloaded first.
  explicit SurfaceNotifier(FakeReaper* reaper);
  SurfaceNotifier(const SurfaceNotifier&) = delete;
  SurfaceNotifier& operator=(const SurfaceNotifier&) = delete;
  ~SurfaceNotifier() = default;

 private:
  // A gb::FunctionHook's Hook, which passes each call to `kHandler`.
  template <auto kHandler>
  class Hook final {
   public:
    explicit Hook(SurfaceNotifier* notifier) : notifier_(notifier) {}

    template <typename Function, typename... Args>
    auto Call(Function original, Args... args) {
      return (notifier_->*kHandler)(original, args...);
    }

   private:
    SurfaceNotifier* const notifier_;
  };

  //----------------------------------------------------------------------------
  // Hooked functions, each calling the original first
  //----------------------------------------------------------------------------

  void OnPreventUIRefresh(decltype(PreventUIRefresh) original, int count);
  int OnSetMuteOrSolo(decltype(SetTrackUIMute) original, MediaTrack* track,
                      int value, int group_flags);
  int OnSetTrackUIRecArm(decltype(SetTrackUIRecArm) original, MediaTrack* track,
                         int rec_arm, int group_flags);
  void OnSetOnlyTrackSelected(decltype(SetOnlyTrackSelected) original,
                              MediaTrack* track);
  void OnSetTrackSelected(decltype(SetTrackSelected) original,
                          MediaTrack* track, bool selected);
  double OnVolumeOrPanChange(decltype(CSurf_OnVolumeChangeEx) original,
                             MediaTrack* track, double value, bool relative,
                             bool allow_gang);
  void OnSetGlobalAutomationOverride(
      decltype(SetGlobalAutomationOverride) original, int mode);
  void OnMainOnCommand(decltype(Main_OnCommand) original, int command,
                       int flag);

  //----------------------------------------------------------------------------
  // Track changes
  //----------------------------------------------------------------------------

  // Returns the current project's tracks, master first.
  std::vector<FakeTrack*> GetTracks();

  // What a track setter can change on each track.
  struct TrackFlags {
    bool mute = false;
    bool solo = false;
    bool selected = false;

    bool operator==(const TrackFlags&) const = default;
  };

  // Returns GetTracks()' flags, to compare with after a change.
  std::vector<TrackFlags> GetFlags();

  // Adds each track whose flags differ from `before` to `changed`.
  void AddChanges(const std::vector<TrackFlags>& before,
                  absl::flat_hash_set<const FakeTrack*>& changed);

  // Sends what the track setters left for the end of the batch, unless a
  // PreventUIRefresh() scope is open in the fake.
  void SendIfNoBatch();
  void SendPending();

  //----------------------------------------------------------------------------
  // Calls on the surface
  //----------------------------------------------------------------------------

  // Sends the master's solo, as whether any track is soloed.
  void SendMasterSolo(IReaperControlSurface& surface);

  // Sends the master's mute, volume, and pan, as undo does.
  void SendMasterMuteVolumeAndPan(IReaperControlSurface& surface);

  // Sends a track's volume and pan, each way REAPER does.
  void SendVolumeAndPan(IReaperControlSurface& surface, FakeTrack* track);

  // Sends a track's whole state, as after a track list change.
  void SendState(IReaperControlSurface& surface, FakeTrack* track);

  // Sends every track's volume, pan, and selection, as after a change to
  // automation modes.
  void SendAutomationChange(IReaperControlSurface& surface);

  // Sends what Edit: Undo did (see "Seen in traces").
  void SendUndo(IReaperControlSurface& surface);

  FakeReaper* const reaper_;

  // What the track setters left to send when the PreventUIRefresh() scopes
  // end.
  absl::flat_hash_set<const FakeTrack*> pending_mute_solo_;
  absl::flat_hash_set<const FakeTrack*> pending_selected_;
  bool pending_track_list_ = false;

  gb::FunctionHook<&PreventUIRefresh,
                   Hook<&SurfaceNotifier::OnPreventUIRefresh>>
      prevent_ui_refresh_hook_{this};
  gb::FunctionHook<&SetTrackUIMute, Hook<&SurfaceNotifier::OnSetMuteOrSolo>>
      set_track_ui_mute_hook_{this};
  gb::FunctionHook<&SetTrackUISolo, Hook<&SurfaceNotifier::OnSetMuteOrSolo>>
      set_track_ui_solo_hook_{this};
  gb::FunctionHook<&SetTrackUIRecArm,
                   Hook<&SurfaceNotifier::OnSetTrackUIRecArm>>
      set_track_ui_rec_arm_hook_{this};
  gb::FunctionHook<&SetOnlyTrackSelected,
                   Hook<&SurfaceNotifier::OnSetOnlyTrackSelected>>
      set_only_track_selected_hook_{this};
  gb::FunctionHook<&SetTrackSelected,
                   Hook<&SurfaceNotifier::OnSetTrackSelected>>
      set_track_selected_hook_{this};
  gb::FunctionHook<&CSurf_OnVolumeChangeEx,
                   Hook<&SurfaceNotifier::OnVolumeOrPanChange>>
      volume_change_hook_{this};
  gb::FunctionHook<&CSurf_OnPanChangeEx,
                   Hook<&SurfaceNotifier::OnVolumeOrPanChange>>
      pan_change_hook_{this};
  gb::FunctionHook<&SetGlobalAutomationOverride,
                   Hook<&SurfaceNotifier::OnSetGlobalAutomationOverride>>
      set_global_automation_override_hook_{this};
  gb::FunctionHook<&Main_OnCommand, Hook<&SurfaceNotifier::OnMainOnCommand>>
      main_on_command_hook_{this};
};

}  // namespace jpr
