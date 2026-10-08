// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "gb/base/function_hook.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// SurfaceNotifier
//
// Makes the calls REAPER makes on the open control surface from inside its own
// functions, as the contract tests in surface_notifier_contract_test.cc show
// (see also "Seen in traces" in docs/testing_and_profiling.md), for as long as
// it exists, and those for the changes made in REAPER's own UI that tests make
// (see below). A test of a whole surface creates one before the plugin loads,
// so its hooks are under the profiler's and the trace's, as REAPER's own
// functions are. A test of ControlSurface itself makes the calls it needs by
// hand, and has none.
//
// It hooks these functions over the fake, and calls back:
// - SetTrackUIMute() and SetTrackUISolo(): the master's solo (whether any track
//   is soloed), even if nothing changed. A track's own mute and solo are sent
//   at the next refresh (see below).
// - SetTrackUIRecArm(), if a track's rec arm changed: outside a batch, the
//   track's change (its rec arm, mute, solo, volume, and pan), then
//   SetTrackListChange() and every track's state, then the master's solo and
//   the change of each track whose rec arm changed. In a batch,
//   SetTrackListChange() and the master's solo, and the rest at the refresh.
//   With selection ganging (group flags without &2), a selected track's
//   change is sent as a change to every track. REAPER also changes the other
//   selected tracks then, which the fake doesn't.
// - SetOnlyTrackSelected() and SetTrackSelected(): the selection of each track
//   whose selection changed, outside a batch, and otherwise at the refresh.
// - CSurf_OnVolumeChangeEx() and CSurf_OnPanChangeEx(): IsKeyDown(VK_SHIFT),
//   then the track as the last touched.
// - SetTrackSendUIVol() and SetTrackSendUIPan(), unless they end an edit
//   (isend 1): the new value as the source track's send, and for a send to a
//   track, as the destination's receive. A route's mute sends nothing.
// - SetGlobalAutomationOverride(): every track's volume, pan, and selection,
//   even if nothing changed.
// - Main_OnCommand(), after the action's handler: for the automation mode
//   actions (40400-40404), SetAutoMode() with the mode, then as the override
//   does if any track's mode changed. For Edit: Undo and Edit: Redo (40029,
//   40030), everything (see SendUndo()).
//
// The refresh is when the outermost PreventUIRefresh() scope ends. Outside a
// batch, REAPER sends a track's mute and solo before the next run, which a
// contract test can't span, so it isn't made: they are forgotten when the
// surface next runs. Until they are sent, every track's state leaves them out.
// They are only sent if they differ from what was last sent.
//
// Tracks are sent master first, then in order, from the current project. It
// makes every call the contract tests show, not just those ControlSurface acts
// on, except those about state the fake doesn't hold: the mixer's scroll, and
// input monitoring. REAPER also calls IsKeyDown(VK_SHIFT) before each run,
// which isn't made either.
//
// Calls from inside a function are made on the surface the open
// TestControlSurface wraps (see TestControlSurface::GetWrapped()), so they are
// part of the entry point that called the function (see "Checks" in
// FakeReaper). Calls for a change in REAPER's UI are made on the
// TestControlSurface, so each is an entry point of its own. Calls are only
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

  //----------------------------------------------------------------------------
  // Changes made in REAPER's own UI
  //
  // Each changes the current project as the user does in REAPER, and makes the
  // calls REAPER makes on the surface for it, as traces showed. REAPER makes
  // them between runs, so a test does too, outside any PreventUIRefresh()
  // scope. A change no trace has shown, such as one to the master, fails the
  // test, and changes nothing. A change to what JPRSurf polls, such as a
  // volume or a route, needs none of these: a test sets the fake. Setting the
  // global automation override in REAPER calls what
  // SetGlobalAutomationOverride() does.
  //----------------------------------------------------------------------------

  // Clicks `track` in the track panel, which selects only it: the
  // selection of each track that changed, then OnTrackSelection() and the
  // track as the last touched.
  void ClickTrack(FakeTrack* track);

  // Ctrl+clicks `track`, which adds it to the selection, and calls back
  // as a click does. Traces only showed a track being added, so `track` must
  // not be selected.
  void CtrlClickTrack(FakeTrack* track);

  // Clicks `track`'s mute button, which toggles its mute: the track as the
  // last touched, then its mute and solo. Traces only showed a track that is
  // the only one selected, or isn't selected.
  void ClickMute(FakeTrack* track);

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
  bool OnSetTrackSendUIVol(decltype(SetTrackSendUIVol) original,
                           MediaTrack* track, int index, double volume,
                           int end_edit);
  bool OnSetTrackSendUIPan(decltype(SetTrackSendUIPan) original,
                           MediaTrack* track, int index, double pan,
                           int end_edit);
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
    bool rec_arm = false;
    bool selected = false;
    int auto_mode = 0;

    bool operator==(const TrackFlags&) const = default;
  };

  // Returns GetTracks()' flags, to compare with after a change, or one track's.
  std::vector<TrackFlags> GetFlags();
  static TrackFlags GetFlags(const FakeTrack& track);

  // Returns the tracks whose flags differ from `before`, in order. None of
  // the hooked functions adds or removes tracks.
  std::vector<FakeTrack*> GetChanges(const std::vector<TrackFlags>& before);

  // Sends the selection of each of `changed`, or leaves it for the refresh in
  // a batch.
  void SendSelectionChanges(const std::vector<FakeTrack*>& changed);

  // Selects `clicked`, and only it if `only` is true, as a click on it
  // in REAPER does, and calls back.
  void ClickToSelect(FakeTrack* clicked, bool only);

  // Returns true if `track` is the current project's master.
  bool IsMaster(const FakeTrack* track);

  // Each returns true if a change in REAPER's UI can be made, and otherwise
  // fails the test: outside any PreventUIRefresh() scope, and only if
  // `traced`, for the `change` no trace has shown.
  bool CheckOutsideBatch();
  bool CheckTraced(bool traced, std::string_view change);

  // Returns the surface to make calls from inside a function on, or null if
  // none is open.
  IReaperControlSurface* GetWrappedSurface();

  //----------------------------------------------------------------------------
  // What isn't sent yet
  //----------------------------------------------------------------------------

  // The mute and solo last sent for a track whose mute or solo changed since.
  struct Unsent {
    bool mute = false;
    bool solo = false;
  };

  // Returns the tracks whose mute and solo aren't sent yet, and what was last
  // sent for each. Those of an earlier run are forgotten, as REAPER sent them
  // before it.
  absl::flat_hash_map<const FakeTrack*, Unsent>& GetUnsent();

  // Records each track whose mute or solo differs from `before`, and what
  // was last sent for it.
  void AddUnsent(const std::vector<TrackFlags>& before);

  // Sends `track`'s mute and solo at the refresh (see the class comment), if
  // they aren't sent yet, and differ from what was.
  void SendRefresh(IReaperControlSurface& surface, FakeTrack* track);

  //----------------------------------------------------------------------------
  // Calls on the surface
  //----------------------------------------------------------------------------

  // Sends the master's solo, as whether any track is soloed.
  void SendMasterSolo(IReaperControlSurface& surface);

  // Sends the master's mute, volume, and pan, as undo does.
  void SendMasterMuteVolumeAndPan(IReaperControlSurface& surface);

  // Sends a track's mute, and solo unless it is the master, which are then
  // sent.
  void SendMuteAndSolo(IReaperControlSurface& surface, FakeTrack* track);

  // Sends a track's volume and pan, each way REAPER does.
  void SendVolumeAndPan(IReaperControlSurface& surface, FakeTrack* track);

  // Sends a track's whole state, as after a track list change, without its
  // mute and solo if they aren't sent yet.
  void SendState(IReaperControlSurface& surface, FakeTrack* track);

  // Sends a track's title, rec arm, and selection, the end of its state.
  void SendTitleRecArmAndSelected(IReaperControlSurface& surface,
                                  FakeTrack* track);

  // Sends a change to a track's rec arm: its rec arm, mute, solo, volume, and
  // pan.
  void SendRecArmChange(IReaperControlSurface& surface, FakeTrack* track);

  // Sends every track's volume, pan, and selection, as after a change to
  // automation modes.
  void SendAutomationChange(IReaperControlSurface& surface);

  // Sends the new value of `track`'s route at `index`, as the *TrackSendUI*
  // functions index them, from each end.
  void SendRouteChange(MediaTrack* track, int index, bool pan);

  // Sends what Edit: Undo did (see "Seen in traces").
  void SendUndo(IReaperControlSurface& surface);

  FakeReaper* const reaper_;

  // See GetUnsent(), which forgets them when the run isn't `unsent_run_`.
  absl::flat_hash_map<const FakeTrack*, Unsent> unsent_;
  int64_t unsent_run_ = 0;

  // The tracks whose rec arm or selection changed in the batch.
  absl::flat_hash_set<const FakeTrack*> batch_rec_arm_;
  absl::flat_hash_set<const FakeTrack*> batch_selected_;

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
  gb::FunctionHook<&SetTrackSendUIVol,
                   Hook<&SurfaceNotifier::OnSetTrackSendUIVol>>
      set_track_send_ui_vol_hook_{this};
  gb::FunctionHook<&SetTrackSendUIPan,
                   Hook<&SurfaceNotifier::OnSetTrackSendUIPan>>
      set_track_send_ui_pan_hook_{this};
  gb::FunctionHook<&SetGlobalAutomationOverride,
                   Hook<&SurfaceNotifier::OnSetGlobalAutomationOverride>>
      set_global_automation_override_hook_{this};
  gb::FunctionHook<&Main_OnCommand, Hook<&SurfaceNotifier::OnMainOnCommand>>
      main_on_command_hook_{this};
};

}  // namespace jpr
