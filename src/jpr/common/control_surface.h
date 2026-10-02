// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/common/reaper_profiler.h"
#include "jpr/common/runner.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// ControlSurfaceListener
//
// Receives the events a control surface's own logic needs from its
// ControlSurface. The ControlSurface keeps the rest of the common library (such
// as TrackCache and ContinuousUndo) up to date itself, before passing each
// event on.
//==============================================================================

class ControlSurfaceListener {
 public:
  ControlSurfaceListener(const ControlSurfaceListener&) = delete;
  ControlSurfaceListener& operator=(const ControlSurfaceListener&) = delete;
  virtual ~ControlSurfaceListener() = default;

  // Called on every run (about 30 times a second), after TrackCache is up to
  // date. `time` is when the run started, which everything in the run uses.
  virtual void OnRun(const RunTime& time) {}

  // Returns the config string REAPER saves in its preferences for the surface.
  virtual std::string GetConfig() const { return {}; }

 protected:
  ControlSurfaceListener() = default;
};

//==============================================================================
// ControlSurface
//
// The REAPER control surface, which the user adds in REAPER's preferences.
//
// It keeps the rest of the common library up to date with REAPER, and passes
// the events a surface needs on to its ControlSurfaceListener:
// - TrackCache is refreshed at the start of the next run after the track list
//   changes. Changes to track visibility are picked up within a second.
// - TrackCache follows selection, automation mode, and last touched track
//   changes.
// - ContinuousUndo is updated at the start of every run.
// - Each run reads REAPER's clock once, and passes that time to everything in
//   the run.
// - A ReaperProfiler profiles its runs, from once the listener is created
//   until the surface is destroyed.
//
// Only one instance may exist at a time. If the user adds the surface again,
// the new one is refused with an error, and the first is left untouched.
//==============================================================================

class ControlSurface final : private IReaperControlSurface {
 public:
  // A type of control surface, which the user can add in REAPER's
  // preferences.
  struct Type {
    // REAPER's ID for the type: only A-Z and 0-9. This must remain valid for
    // as long as the extension is loaded.
    const char* type_string;

    // The name shown in REAPER's preferences. This must remain valid for as
    // long as the extension is loaded.
    const char* description;

    // Creates the listener for a new control surface, from the config string
    // REAPER saved for it. This must not return null.
    std::unique_ptr<ControlSurfaceListener> (*create_listener)(
        std::string_view config);
  };

  // Registers the control surface type with REAPER. Only one type may be
  // registered. Returns false (and logs an error) if registration fails.
  static bool Register(reaper_plugin_info_t& plugin_info, const Type& type);

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

  explicit ControlSurface(std::string_view config);

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

  // The registered type, which every instance is, and its registration with
  // REAPER.
  static Type s_type_;
  static reaper_csurf_reg_t s_reg_;

  // The one instance that exists, if any.
  static ControlSurface* s_instance_;

  // The profiler is declared first, so its hooks are in place for the MIDI
  // ports the listener opens, and it outlives the listener.
  ReaperProfiler profiler_;
  std::unique_ptr<ControlSurfaceListener> listener_;
  std::string config_string_;  // Holds the result of GetConfigString().

  // Set when REAPER reports that the track list changed, so the next run
  // refreshes TrackCache.
  bool track_list_changed_ = false;

  // When track visibility was last polled, in seconds of REAPER's clock. This
  // defaults to the distant past so that the first run always polls.
  double last_visibility_time_ = -std::numeric_limits<double>::infinity();
};

}  // namespace jpr
