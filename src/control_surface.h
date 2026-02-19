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
#include "reaper_plugin.h"

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
  enum class TrackFlag {
    kSelected,
    kSolo,
    kMute,
    kRecArm,
  };
  using TrackFlags = gb::Flags<TrackFlag>;

  struct TrackState {
    MediaTrack* track_id;
    std::string name;
    TrackFlags flags;
    double volume = 0.0;
    double pan = 0.0;
  };

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
  TrackState* GetTrackState(MediaTrack* track_id);
  void RebuildTracks();

  // State
  std::string type_string_;
  gb::Config config_;
  std::string config_string_;

  // All tracks in order, not including the master track.
  std::vector<TrackState> tracks_;
  absl::flat_hash_map<MediaTrack*, int> track_indexes_;
  MediaTrack* master_track_ = nullptr;
};

}  // namespace jpr