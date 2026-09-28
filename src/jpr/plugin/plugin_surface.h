// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
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
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
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
  std::string GetConfig() const override;

  // Implementation
  void ConnectDevices();
  void InitViews();

  // Surface modes
  //
  // Each mode has a button that is lit when the mode is available, and blinks
  // when it is the current mode. Each mode's view has a condition on the mode's
  // active toggle, so entering a mode only sets the toggles, and can happen at
  // any time, including while the scene runs. The scene switches the views
  // when it next runs, before any mappings sync.
  void InitModeButtons(bool has_xtouch);
  bool IsModeAvailable(SurfaceMode mode) const;
  void UpdateModeButtons();

  // Switches the surface to the given mode. The Send/Receive mode track must
  // not be null.
  void EnterTrackMode();
  void EnterSendReceiveMode(Track* track);

  // Enters Send/Receive mode for `track`, if it can be shown: it exists, is on
  // the surface, and has sends or receives.
  void TryEnterSendReceiveMode(Track* track);

  // Completes a mode change from `old_mode`.
  void FinishModeChange(SurfaceMode old_mode);

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

  // The track whose routes Send/Receive mode shows, and which the track list
  // reveals. It follows the last touched track, and entering Send/Receive mode
  // and route navigation also change it.
  TrackReference* current_track_ = nullptr;

  View* track_mode_view_ = nullptr;  // Parent of all Track mode views.
  // The Send/Receive mode view, which is bound to the current track, and lists
  // its routes.
  View* send_receive_mode_view_ = nullptr;

  // Surface mode state.
  struct ModeButton {
    ToggleValueProperty* available = nullptr;  // Lights the button.

    // Makes the light blink, and is the condition for the mode's view.
    ToggleValueProperty* active = nullptr;
  };
  SurfaceMode mode_ = SurfaceMode::kTrack;
  ModeButton mode_buttons_[kSurfaceModeCount];

  // The modifier that is on while the Send/Receive mode button is held, so it
  // can be held to pick a track.
  Modifiers send_hold_modifier_ = 0;

  // Set when mode availability may have changed, so the mode buttons are
  // updated on the next run. Availability depends on the track selection and
  // routing, which only change when REAPER notifies the surface, so there is no
  // need to check them every run.
  bool mode_buttons_changed_ = true;
};

}  // namespace jpr