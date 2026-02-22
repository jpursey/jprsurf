// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>

#include "absl/types/span.h"
#include "control_input.h"
#include "control_output.h"
#include "gb/base/flags.h"
#include "gb/config/config.h"
#include "midi_port.h"
#include "reaper_plugin.h"
#include "runner.h"

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
  // This is a pure binary string (not serialized to readable text). This is
  // used for fast conversion from GUID and lookup.
  using GuidKey = std::array<std::byte, sizeof(GUID)>;

  enum class TrackFlag {
    kSelected,
    kSolo,
    kMute,
    kRecArm,
  };
  using TrackFlags = gb::Flags<TrackFlag>;

  struct TrackState {
    GUID guid;
    MediaTrack* track_id;
    std::string name;
    TrackFlags flags;
    double volume = 0.0;
    double pan = 0.0;
  };

  // This holds the cached track state and control mappings for a single track.
  struct TrackView {
    const GUID& GetGuid() const;

    // State is optional as a track view may exist even if there is no track for
    // it currently.
    std::optional<TrackState> state;

    // TODO: Add control mappings that can are synchronized with the state.
    std::unique_ptr<ControlPressInput> select_input;
    std::unique_ptr<ControlDValueOutput> select_output;
  };

  // This holds a set of consecutive track views, that are treated as a
  // contiguous set.
  struct TrackListView {
    // This is the GUID of the parent track for the view, if there is one.
    std::optional<GUID> parent_track;

    // This is the index of the first view relative to the parent track, or the
    // top level track if there is no parent.
    int index = 0;

    // All tracks in the view, in sequential order. This may contain tracks
    // that do not exist (for instance, if there are less actual tracks that
    // there are views, or if it hasn't been initialized yet).
    std::vector<TrackView> track_views;
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
  GuidKey GuidToKey(const GUID& guid);
  void ConnectDevices();
  void InitViews();
  TrackView* GetTrackView(MediaTrack* track_id);
  TrackState* GetTrackState(MediaTrack* track_id);
  void RebuildTrackView(TrackView& track_view, const GUID& track_guid,
                        MediaTrack* track_id, int track_index);
  void RefreshTrackListView(TrackListView& track_list_view);
  void RebuildTracks();
  int GetChildTrackIndex(MediaTrack* track_id);
  void EnsureTrackIsInView(TrackListView& track_list_view,
                           MediaTrack* track_id);
  void OnTrackViewSelectPressed(TrackView& track_view);

  // State
  std::string type_string_;
  gb::Config config_;
  std::string config_string_;
  Runner runner_;
  std::unique_ptr<MidiIn> xtouch_in_;
  std::unique_ptr<MidiOut> xtouch_out_;

  // Cached track state, updated whenever MediaTrack* pointers may be
  // invalidated (on a reset or track list change event).
  absl::flat_hash_map<GuidKey, MediaTrack*> track_guid_to_id_;

  // View state between the control surface and the actual REAPER tracks.
  TrackListView track_list_view_;
  TrackView master_track_view_;
};

}  // namespace jpr