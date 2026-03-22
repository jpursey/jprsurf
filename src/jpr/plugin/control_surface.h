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
  View* track_list_view_ = nullptr;
  bool track_list_changed_ = false;

  // Performance monitoring
  absl::Time last_log_time_;
  absl::Duration elapsed_run_time_;
  int run_count_ = 0;
};

}  // namespace jpr