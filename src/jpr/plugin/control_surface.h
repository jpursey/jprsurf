// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>

#include "absl/types/span.h"
#include "gb/base/flags.h"
#include "gb/config/config.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/common/track.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/scene/scene.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

class ToggleValueProperty;

// The top level modes of the control surface, which determine what the channel
// strips control. Values are contiguous starting at zero, and are used directly
// as indices into per-mode state.
enum class SurfaceMode {
  kTrack,        // Channel strips show tracks in the track hierarchy.
  kSendReceive,  // Channel strips show the sends or receives of one track.
};
inline constexpr int kSurfaceModeCount = 2;

class ControlSurface final : private IReaperControlSurface {
 public:
  // Returns the control surface registration struct used to register this
  // control surface with REAPER.
  static reaper_csurf_reg_t* GetControlSurfaceReg();

  ControlSurface(const ControlSurface&) = delete;
  ControlSurface& operator=(const ControlSurface&) = delete;
  ~ControlSurface() override;

 private:
  // Registration functions
  static IReaperControlSurface* Create(const char* type_string,
                                       const char* config_string,
                                       int* err_stats);
  static HWND ShowConfig(const char* type_string, HWND parent,
                         const char* init_config_string);

  // Construction
  ControlSurface(std::string type_string, std::string config_string);

  // IReaperControlSurface overrides
  const char* GetTypeString() override;
  const char* GetDescString() override;
  const char* GetConfigString() override;
  void Run() override;
  void SetTrackListChange() override;
  void SetSurfaceVolume(MediaTrack* track_id, double volume) override;
  void SetSurfacePan(MediaTrack* track_id, double pan) override;
  void SetSurfaceMute(MediaTrack* track_id, bool mute) override;
  void SetSurfaceSelected(MediaTrack* track_id, bool selected) override;
  void SetSurfaceSolo(MediaTrack* track_id, bool solo) override;
  void SetSurfaceRecArm(MediaTrack* track_id, bool rec_arm) override;
  void SetPlayState(bool play, bool pause, bool rec) override;
  void SetRepeatState(bool rep) override;
  void SetTrackTitle(MediaTrack* track_id, const char* title) override;
  bool GetTouchState(MediaTrack* track_id, int is_pan) override;
  void SetAutoMode(int mode) override;
  void ResetCachedVolPanStates() override;
  void OnTrackSelection(MediaTrack* track_id) override;
  bool IsKeyDown(int key) override;
  int Extended(int call, void* param1, void* param2, void* param3) override;

  // Extended calls
  void OnReset();
  void OnSetInputMonitor(MediaTrack* track_id, int rec_monitor);
  void OnSetMetronome(bool enabled);
  void OnSetAutoRecArm(bool auto_rec_arm);
  void OnSetRecMode(int rec_mode);
  void OnSetSendVolume(MediaTrack* track_id, int send_idx, double volume);
  void OnSetSendPan(MediaTrack* track_id, int send_idx, double pan);
  void OnSetFxEnabled(MediaTrack* track_id, int fx_idx, bool enabled);
  void OnSetFxParam(MediaTrack* track_id, int fx_idx, int param_idx,
                    double normalized_value);
  void OnSetFxParamRecFx(MediaTrack* track_id, int fx_idx, int param_idx,
                         double normalized_value);
  void OnSetBpmAndPlayRate(std::optional<double> bpm,
                           std::optional<double> play_rate);
  void OnClearLastTouchedFx();
  void OnSetLastTouchedFx(MediaTrack* track_id,
                          std::optional<int> media_item_idx, int fx_idx);
  void OnClearFocusedFx();
  void OnSetFocusedFx(MediaTrack* track_id, std::optional<int> media_item_idx,
                      int fx_idx);
  void OnSetLastTouchedTrack(MediaTrack* track_id);
  void OnSetMixerScroll(MediaTrack* track_id);
  void OnSetPanEx(MediaTrack* track_id, absl::Span<const double> pan, int mode);
  void OnSetRecvVolume(MediaTrack* track_id, int rec_idx, double volume);
  void OnSetRecvPan(MediaTrack* track_id, int rec_idx, double pan);
  void OnSetFxOpen(MediaTrack* track_id, int fx_idx, bool open);
  void OnSetFxChange(MediaTrack* track_id, int flags);
  void OnSetProjectMarkerChange();
  void OnTrackFxPresetChanged(MediaTrack* track_id, int fx_idx);
  bool OnSupportsExtendedTouch();
  void OnMidiDeviceRemap(bool is_out, int old_idx, int new_idx);

  // Implementation
  void ConnectDevices();
  void InitViews();
  void EnsureTrackIsVisible(Track* track);

  // Re-points the views at the current track list. This is called whenever the
  // track list changes, or a track is shown or hidden on the surface. Only a
  // deleted parent track moves the track list view; a hidden one does not.
  void RefreshTrackViews();

  // Surface modes
  //
  // Each mode has a button that is lit when the mode is available, and blinks
  // when it is the current mode. Pressing a button only records the requested
  // mode, as presses are handled while the scene is running. The request is
  // applied by ApplyRequestedMode() once the scene has finished.
  void InitModeButtons(bool has_xtouch);
  bool IsModeAvailable(SurfaceMode mode) const;
  void UpdateModeButtons();
  void ApplyRequestedMode();

  // Switches the surface to the given mode. These must not be called while the
  // scene is running, as they enable and disable views. The Send/Receive mode
  // track must not be null.
  void EnterTrackMode();
  void EnterSendReceiveMode(Track* track);

  // Enters Send/Receive mode for `track`, if it can be shown: it exists, is on
  // the surface, and has sends or receives.
  void TryEnterSendReceiveMode(Track* track);

  // Handles the Send/Receive mode button once it is released (see
  // send_press_mode_), if it was pressed rather than held. This must not be
  // called while the scene is running.
  void ApplySendRelease(absl::Time now);

  // Shows the routes of `track` in Send/Receive mode: its receives if it has
  // only receives, and otherwise its sends (even if it has no routes at all).
  void SetSendReceiveTrack(Track* track);

  // Completes a mode change started at `start` from `old_mode`.
  void FinishModeChange(SurfaceMode old_mode, absl::Time start);

  // State
  std::string type_string_;
  gb::Config config_;
  std::string config_string_;
  Runner device_runner_;    // Resets device state, sends pending messages.
  Runner midi_in_runner_;   // Reads MIDI messages from the ports.
  Runner scene_runner_;     // Updates the scene.
  Runner midi_out_runner_;  // Sends MIDI messages to the ports.
  std::unique_ptr<MidiIn> xtouch_in_;
  std::unique_ptr<MidiOut> xtouch_out_;
  std::unique_ptr<MidiIn> xtouch_ext_in_;
  std::unique_ptr<MidiOut> xtouch_ext_out_;
  std::unique_ptr<Scene> scene_;
  View* track_mode_view_ = nullptr;  // Parent of all Track mode views.
  // The Send/Receive mode view. Its track is the track whose routes are shown
  // (which route navigation can change), and its child views show the routes.
  View* send_receive_mode_view_ = nullptr;
  View* master_track_view_ = nullptr;
  View* track_list_view_ = nullptr;
  bool track_list_changed_ = false;

  // Surface mode state.
  struct ModeButton {
    ToggleValueProperty* available = nullptr;  // Lights the button.
    ToggleValueProperty* active = nullptr;     // Makes the light blink.
  };
  SurfaceMode mode_ = SurfaceMode::kTrack;
  std::optional<SurfaceMode> requested_mode_;
  ModeButton mode_buttons_[kSurfaceModeCount];

  // A track picked for Send/Receive mode by pressing its select button while
  // holding the Send/Receive mode button. Applied with requested_mode_.
  Track* requested_send_receive_track_ = nullptr;

  // The Send/Receive mode button acts when it is released rather than pressed,
  // and only if it wasn't held, so it can be held to pick a track. This is the
  // modifier that is on while it is held, and the mode and time when it was
  // pressed, until the release is handled.
  Modifiers send_hold_modifier_ = 0;
  std::optional<SurfaceMode> send_press_mode_;
  absl::Time send_press_time_;

  // Set when mode availability may have changed, so the mode buttons are
  // updated on the next run. Availability depends on the track selection and
  // routing, which only change when REAPER notifies the surface, so there is no
  // need to check them every run.
  bool mode_buttons_changed_ = true;

  // When track visibility was last polled. This defaults to the epoch so that
  // the first run always polls.
  absl::Time last_visibility_time_;

  // Performance monitoring
  absl::Time last_log_time_;
  absl::Duration elapsed_run_time_;
  absl::Duration max_run_time_;
  int run_count_ = 0;
};

}  // namespace jpr