// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "gb/config/config.h"
#include "jpr/common/control_surface.h"
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

class PluginSurface final : private ControlSurfaceListener {
 public:
  // Registers JPRSurf as a control surface type with REAPER. Returns false (and
  // logs an error) if registration fails.
  static bool Register(reaper_plugin_info_t& plugin_info);

  PluginSurface(const PluginSurface&) = delete;
  PluginSurface& operator=(const PluginSurface&) = delete;
  ~PluginSurface() override;

 private:
  // Creates the listener for a new JPRSurf control surface.
  static std::unique_ptr<ControlSurfaceListener> Create(
      std::string_view config);

  explicit PluginSurface(std::string_view config);

  // ControlSurfaceListener overrides
  void OnRun(absl::Time now) override;
  void OnTracksChanged() override;
  void OnSelectionChanged() override;
  void OnLastTouchedTrackChanged(Track* track) override;
  std::string GetConfig() const override;

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
  gb::Config config_;
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
};

}  // namespace jpr