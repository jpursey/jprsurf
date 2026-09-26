// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/control_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"
#include "jpr/common/track_cache.h"
#include "jpr/common/undo.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

// How often the performance summary of Run() is logged.
constexpr absl::Duration kLogInterval = absl::Seconds(5);

// How often track visibility is polled. REAPER gives control surfaces no
// notification when a track is shown or hidden, so the only way to see it is to
// query every track in the project, which is far too much to do on every run.
// Hiding a track is a deliberate, infrequent action taken in the Track Manager,
// so a delay of up to this long before the surface follows is not noticeable.
constexpr absl::Duration kVisibilityInterval = absl::Seconds(1);

}  // namespace

ControlSurface::Type ControlSurface::s_type_ = {};
reaper_csurf_reg_t ControlSurface::s_reg_ = {};
ControlSurface* ControlSurface::s_instance_ = nullptr;

#define VLOG_REAPER() VLOG(1) << "REAPER: "

//------------------------------------------------------------------------------
// Registration
//------------------------------------------------------------------------------

bool ControlSurface::Register(reaper_plugin_info_t& plugin_info,
                              const Type& type) {
  if (s_reg_.type_string != nullptr) {
    LOG(ERROR) << "Control surface type " << type.type_string
               << " not registered, as " << s_reg_.type_string
               << " is already registered.";
    return false;
  }
  s_type_ = type;
  s_reg_ = {s_type_.type_string, s_type_.description, ControlSurface::Create,
            ControlSurface::ShowConfig};
  if (plugin_info.Register("csurf", &s_reg_) == 0) {
    LOG(ERROR) << "Failed to register control surface type "
               << s_type_.type_string << ".";
    s_reg_ = {};
    return false;
  }
  return true;
}

IReaperControlSurface* ControlSurface::Create(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  VLOG_REAPER() << "Create(type_string="
                << (type_string ? type_string : "(null)")
                << ", config_string=\"" << (config_string ? config_string : "")
                << "\")";

  // Refuse before creating the listener, which may claim resources (such as
  // MIDI ports) that the existing instance is using.
  if (s_instance_ != nullptr) {
    const std::string message = absl::StrCat(
        s_type_.description,
        " is already running, and only one can be added. If it is listed more "
        "than once in Preferences > Control/OSC/web, remove the extras.");
    LOG(ERROR) << message;
    ShowConsoleMsg(absl::StrCat(message, "\n").c_str());
    return nullptr;
  }

  return new ControlSurface(
      s_type_.create_listener(absl::NullSafeStringView(config_string)));
}

HWND ControlSurface::ShowConfig(const char* type_string, HWND parent,
                                const char* init_config_string) {
  VLOG_REAPER() << "ShowConfig(type_string="
                << (type_string ? type_string : "(null)")
                << ", parent=" << parent << ", init_config_string=\""
                << (init_config_string ? init_config_string : "") << "\")";
  return nullptr;
}

ControlSurface::ControlSurface(std::unique_ptr<ControlSurfaceListener> listener)
    : listener_(std::move(listener)) {
  CHECK(listener_ != nullptr)
      << "No listener created for control surface type " << s_type_.type_string;
  CHECK(s_instance_ == nullptr) << "Only one ControlSurface may exist";
  s_instance_ = this;
  LOG(INFO) << "ControlSurface created";
}

ControlSurface::~ControlSurface() {
  // Destroy the listener explicitly, so it is gone before this is logged, and
  // before another instance can be created.
  listener_.reset();
  s_instance_ = nullptr;
  LOG(INFO) << "ControlSurface destroyed";
}

//------------------------------------------------------------------------------
// Reaper callbacks
//------------------------------------------------------------------------------

const char* ControlSurface::GetTypeString() { return s_type_.type_string; }

const char* ControlSurface::GetDescString() { return s_type_.description; }

const char* ControlSurface::GetConfigString() {
  config_string_ = listener_->GetConfig();
  VLOG_REAPER() << "GetConfigString() -> \"" << config_string_ << "\"";
  return config_string_.c_str();
}

void ControlSurface::Run() {
  const absl::Time start = absl::Now();

  if (track_list_changed_) {
    track_list_changed_ = false;
    TrackCache::Get().Refresh();

    // Refresh() re-read visibility for every track, so the poll can wait.
    last_visibility_time_ = start;
    listener_->OnTracksChanged();

    // The refresh is the first work done this run, so this is its duration,
    // including the listener's response to it.
    LOG(INFO) << "Refreshed TrackCache with "
              << TrackCache::Get().GetTrackCount() << " tracks in "
              << absl::ToInt64Microseconds(absl::Now() - start) << "us";
  } else if (last_visibility_time_ + kVisibilityInterval < start) {
    last_visibility_time_ = start;
    if (TrackCache::Get().RefreshVisibility()) {
      listener_->OnTracksChanged();
    }
  }

  listener_->OnRun(start);

  // Create undo points for continuous changes made this run, or earlier, once
  // they have stopped.
  ContinuousUndo::Get().Update(start);

  LogRunTime(start, absl::Now());
}

void ControlSurface::SetTrackListChange() {
  VLOG_REAPER() << "SetTrackListChange";
  track_list_changed_ = true;
}

void ControlSurface::SetSurfaceVolume(MediaTrack* track_id, double volume) {
  VLOG_REAPER() << "SetSurfaceVolume(track_id=" << track_id
                << ", volume=" << volume << ")";
}

void ControlSurface::SetSurfacePan(MediaTrack* track_id, double pan) {
  VLOG_REAPER() << "SetSurfacePan(track_id=" << track_id << ", pan=" << pan
                << ")";
}

void ControlSurface::SetSurfaceMute(MediaTrack* track_id, bool mute) {
  VLOG_REAPER() << "SetSurfaceMute(track_id=" << track_id << ", mute=" << mute
                << ")";
}

void ControlSurface::SetSurfaceSelected(MediaTrack* track_id, bool selected) {
  VLOG_REAPER() << "SetSurfaceSelected(track_id=" << track_id
                << ", selected=" << selected << ")";
  TrackCache::Get().OnSelectionChanged();
  listener_->OnSelectionChanged();
}

void ControlSurface::SetSurfaceSolo(MediaTrack* track_id, bool solo) {
  VLOG_REAPER() << "SetSurfaceSolo(track_id=" << track_id << ", solo=" << solo
                << ")";
}

void ControlSurface::SetSurfaceRecArm(MediaTrack* track_id, bool rec_arm) {
  VLOG_REAPER() << "SetSurfaceRecArm(track_id=" << track_id
                << ", rec_arm=" << rec_arm << ")";
}

void ControlSurface::SetPlayState(bool play, bool pause, bool rec) {
  VLOG_REAPER() << "SetPlayState(play=" << play << ", pause=" << pause
                << ", rec=" << rec << ")";
}

void ControlSurface::SetRepeatState(bool rep) {
  VLOG_REAPER() << "SetRepeatState(rep=" << rep << ")";
}

void ControlSurface::SetTrackTitle(MediaTrack* track_id, const char* title) {
  VLOG_REAPER() << "SetTrackTitle(track_id=" << track_id << ", title=\""
                << (title != nullptr ? title : "(null)") << "\")";
}

bool ControlSurface::GetTouchState(MediaTrack* track_id, int is_pan) {
  VLOG_REAPER() << "GetTouchState(track_id=" << track_id
                << ", is_pan=" << is_pan << ")";
  return false;
}

void ControlSurface::SetAutoMode(int mode) {
  VLOG_REAPER() << "SetAutoMode(mode=" << mode << ")";
  TrackCache::Get().OnAutoModeChanged();
}

void ControlSurface::ResetCachedVolPanStates() {
  VLOG_REAPER() << "ResetCachedVolPanStates";
}

void ControlSurface::OnTrackSelection(MediaTrack* track_id) {
  VLOG_REAPER() << "OnTrackSelection(track_id=" << track_id << ")";
}

bool ControlSurface::IsKeyDown(int key) {
  // Too spammy to log, REAPER calls this continuously.
  return false;
}

namespace {

bool CheckParamValue(int call, int index, void* param) {
  if (param == nullptr) {
    LOG(ERROR) << "REAPER: Extended(call=" << absl::Hex(call, absl::kZeroPad8)
               << "): Aborted because param" << index << " is null";
    return false;
  }
  return true;
}

}  // namespace

#define JPR_GET_PARAM_VALUE(Type, name, index)       \
  if (!CheckParamValue(call, index, param##index)) { \
    return 0;                                        \
  }                                                  \
  auto name = *static_cast<Type*>(param##index);

int ControlSurface::Extended(int call, void* param1, void* param2,
                             void* param3) {
  switch (call) {
    case CSURF_EXT_RESET: {
      OnReset();
      return 0;
    }
    case CSURF_EXT_SETINPUTMONITOR: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, rec_monitor, 2);
      OnSetInputMonitor(track_id, rec_monitor);
      return 0;
    }
    case CSURF_EXT_SETMETRONOME: {
      bool enabled = (param1 != nullptr);
      OnSetMetronome(enabled);
      return 0;
    }
    case CSURF_EXT_SETAUTORECARM: {
      bool enabled = (param1 != nullptr);
      OnSetAutoRecArm(enabled);
      return 0;
    }
    case CSURF_EXT_SETRECMODE: {
      JPR_GET_PARAM_VALUE(int, rec_mode, 1);
      OnSetRecMode(rec_mode);
      return 0;
    }
    case CSURF_EXT_SETSENDVOLUME: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, send_idx, 2);
      JPR_GET_PARAM_VALUE(double, volume, 3);
      OnSetSendVolume(track_id, send_idx, volume);
      return 0;
    }
    case CSURF_EXT_SETSENDPAN: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, send_idx, 2);
      JPR_GET_PARAM_VALUE(double, pan, 3);
      OnSetSendPan(track_id, send_idx, pan);
      return 0;
    }
    case CSURF_EXT_SETFXENABLED: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, fx_idx, 2);
      auto enabled = (param3 != nullptr);
      OnSetFxEnabled(track_id, fx_idx, enabled);
      return 0;
    }
    case CSURF_EXT_SETFXPARAM: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, param_info, 2);
      auto fx_idx = param_info >> 16;
      auto param_idx = param_info & 0xFFFF;
      JPR_GET_PARAM_VALUE(double, normalized_value, 3);
      OnSetFxParam(track_id, fx_idx, param_idx, normalized_value);
      return 0;
    }
    case CSURF_EXT_SETFXPARAM_RECFX: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, param_info, 2);
      auto fx_idx = param_info >> 16;
      auto param_idx = param_info & 0xFFFF;
      JPR_GET_PARAM_VALUE(double, normalized_value, 3);
      OnSetFxParamRecFx(track_id, fx_idx, param_idx, normalized_value);
      return 0;
    }
    case CSURF_EXT_SETBPMANDPLAYRATE: {
      auto* bpm = static_cast<double*>(param1);
      auto* play_rate = static_cast<double*>(param2);
      OnSetBpmAndPlayRate(
          bpm ? std::make_optional(*bpm) : std::nullopt,
          play_rate ? std::make_optional(*play_rate) : std::nullopt);
      return 0;
    }
    case CSURF_EXT_SETLASTTOUCHEDFX: {
      if (param2 == nullptr && param3 == nullptr) {
        OnClearLastTouchedFx();
        return 0;
      }
      auto* track_id = static_cast<MediaTrack*>(param1);
      auto* media_item_idx = static_cast<int*>(param2);
      JPR_GET_PARAM_VALUE(int, fx_idx, 3);
      OnSetLastTouchedFx(track_id,
                         media_item_idx != nullptr
                             ? std::make_optional(*media_item_idx)
                             : std::nullopt,
                         fx_idx);
      return 0;
    }
    case CSURF_EXT_SETFOCUSEDFX: {
      if (param2 == nullptr && param3 == nullptr) {
        OnClearFocusedFx();
        return 0;
      }
      auto* track_id = static_cast<MediaTrack*>(param1);
      auto* media_item_idx = static_cast<int*>(param2);
      JPR_GET_PARAM_VALUE(int, fx_idx, 3);
      OnSetFocusedFx(track_id,
                     media_item_idx != nullptr
                         ? std::make_optional(*media_item_idx)
                         : std::nullopt,
                     fx_idx);
      return 0;
    }
    case CSURF_EXT_SETLASTTOUCHEDTRACK: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      OnSetLastTouchedTrack(track_id);
      return 0;
    }
    case CSURF_EXT_SETMIXERSCROLL: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      OnSetMixerScroll(track_id);
      return 0;
    }
    case CSURF_EXT_SETPAN_EX: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      auto* pan = static_cast<double*>(param2);
      JPR_GET_PARAM_VALUE(int, mode, 3);
      int pan_count = (mode == 5 || mode == 6) ? 2 : 1;
      OnSetPanEx(track_id, absl::MakeConstSpan(pan, pan_count), mode);
      return 0;
    }
    case CSURF_EXT_SETRECVVOLUME: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, rec_idx, 2);
      JPR_GET_PARAM_VALUE(double, volume, 3);
      OnSetRecvVolume(track_id, rec_idx, volume);
      return 0;
    }
    case CSURF_EXT_SETRECVPAN: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, rec_idx, 2);
      JPR_GET_PARAM_VALUE(double, pan, 3);
      OnSetRecvPan(track_id, rec_idx, pan);
      return 0;
    }
    case CSURF_EXT_SETFXOPEN: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, fx_idx, 2);
      bool open = (param3 != nullptr);
      OnSetFxOpen(track_id, fx_idx, open);
      return 0;
    }
    case CSURF_EXT_SETFXCHANGE: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      int flags = reinterpret_cast<intptr_t>(param2);
      OnSetFxChange(track_id, flags);
      return 0;
    }
    case CSURF_EXT_SETPROJECTMARKERCHANGE: {
      OnSetProjectMarkerChange();
      return 0;
    }
    case CSURF_EXT_TRACKFX_PRESET_CHANGED: {
      auto* track_id = static_cast<MediaTrack*>(param1);
      JPR_GET_PARAM_VALUE(int, fx_idx, 2);
      OnTrackFxPresetChanged(track_id, fx_idx);
      return 0;
    }
    case CSURF_EXT_SUPPORTS_EXTENDED_TOUCH: {
      return OnSupportsExtendedTouch() ? 1 : 0;
    }
    case CSURF_EXT_MIDI_DEVICE_REMAP: {
      JPR_GET_PARAM_VALUE(int, is_out, 1);
      JPR_GET_PARAM_VALUE(int, old_idx, 2);
      JPR_GET_PARAM_VALUE(int, new_idx, 3);
      OnMidiDeviceRemap(is_out != 0, old_idx, new_idx);
      return 0;
    }
  }
  VLOG_REAPER() << "Extended(call=" << absl::Hex(call, absl::kZeroPad8)
                << "): Unsupported call";
  return 0;
}

void ControlSurface::OnReset() { VLOG_REAPER() << "OnReset"; }

void ControlSurface::OnSetInputMonitor(MediaTrack* track_id, int rec_monitor) {
  VLOG_REAPER() << "OnSetInputMonitor(track_id=" << track_id
                << ", rec_monitor=" << rec_monitor << ")";
}

void ControlSurface::OnSetMetronome(bool enabled) {
  VLOG_REAPER() << "OnSetMetronome(enabled=" << enabled << ")";
}

void ControlSurface::OnSetAutoRecArm(bool auto_rec_arm) {
  VLOG_REAPER() << "OnSetAutoRecArm(auto_rec_arm=" << auto_rec_arm << ")";
}

void ControlSurface::OnSetRecMode(int rec_mode) {
  VLOG_REAPER() << "OnSetRecMode(rec_mode=" << rec_mode << ")";
}

void ControlSurface::OnSetSendVolume(MediaTrack* track_id, int send_idx,
                                     double volume) {
  VLOG_REAPER() << "OnSetSendVolume(track_id=" << track_id
                << ", send_idx=" << send_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetSendPan(MediaTrack* track_id, int send_idx,
                                  double pan) {
  VLOG_REAPER() << "OnSetSendPan(track_id=" << track_id
                << ", send_idx=" << send_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxEnabled(MediaTrack* track_id, int fx_idx,
                                    bool enabled) {
  VLOG_REAPER() << "OnSetFxEnabled(track_id=" << track_id
                << ", fx_idx=" << fx_idx << ", enabled=" << enabled << ")";
}

void ControlSurface::OnSetFxParam(MediaTrack* track_id, int fx_idx,
                                  int param_idx, double normalized_value) {
  VLOG_REAPER() << "OnSetFxParam(track_id=" << track_id << ", fx_idx=" << fx_idx
                << ", param_idx=" << param_idx
                << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetFxParamRecFx(MediaTrack* track_id, int fx_idx,
                                       int param_idx, double normalized_value) {
  VLOG_REAPER() << "OnSetFxParamRecFx(track_id=" << track_id
                << ", fx_idx=" << fx_idx << ", param_idx=" << param_idx
                << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetBpmAndPlayRate(std::optional<double> bpm,
                                         std::optional<double> play_rate) {
  VLOG_REAPER() << "OnSetBpmAndPlayRate(bpm="
                << (bpm.has_value() ? absl::StrCat(bpm.value()) : "(null)")
                << ", play_rate="
                << (play_rate.has_value() ? absl::StrCat(play_rate.value())
                                          : "(null)")
                << ")";
}

void ControlSurface::OnClearLastTouchedFx() {
  VLOG_REAPER() << "OnClearLastTouchedFx";
}

void ControlSurface::OnSetLastTouchedFx(MediaTrack* track_id,
                                        std::optional<int> media_item_idx,
                                        int fx_idx) {
  VLOG_REAPER() << "OnSetLastTouchedFx(track_id=" << track_id
                << ", media_item_idx="
                << (media_item_idx.has_value()
                        ? absl::StrCat(media_item_idx.value())
                        : "(null)")
                << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnClearFocusedFx() { VLOG_REAPER() << "OnClearFocusedFx"; }

void ControlSurface::OnSetFocusedFx(MediaTrack* track_id,
                                    std::optional<int> media_item_idx,
                                    int fx_idx) {
  VLOG_REAPER() << "OnSetFocusedFx(track_id=" << track_id << ", media_item_idx="
                << (media_item_idx.has_value()
                        ? absl::StrCat(media_item_idx.value())
                        : "(null)")
                << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnSetLastTouchedTrack(MediaTrack* track_id) {
  VLOG_REAPER() << "OnSetLastTouchedTrack(track_id=" << track_id << ")";
  Track* track = TrackCache::Get().GetTrack(track_id);
  TrackCache::Get().SetLastTouchedTrack(track);
  listener_->OnLastTouchedTrackChanged(track);
}

void ControlSurface::OnSetMixerScroll(MediaTrack* track_id) {
  VLOG_REAPER() << "OnSetMixerScroll(track_id=" << track_id << ")";
}

void ControlSurface::OnSetPanEx(MediaTrack* track_id,
                                absl::Span<const double> pan, int mode) {
  VLOG_REAPER() << "OnSetPanEx(track_id=" << track_id << ", pan=["
                << absl::StrJoin(pan, ", ") << "], mode=" << mode << ")";
}

void ControlSurface::OnSetRecvVolume(MediaTrack* track_id, int rec_idx,
                                     double volume) {
  VLOG_REAPER() << "OnSetRecvVolume(track_id=" << track_id
                << ", rec_idx=" << rec_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetRecvPan(MediaTrack* track_id, int rec_idx,
                                  double pan) {
  VLOG_REAPER() << "OnSetRecvPan(track_id=" << track_id
                << ", rec_idx=" << rec_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxOpen(MediaTrack* track_id, int fx_idx, bool open) {
  VLOG_REAPER() << "OnSetFxOpen(track_id=" << track_id << ", fx_idx=" << fx_idx
                << ", open=" << open << ")";
}

void ControlSurface::OnSetFxChange(MediaTrack* track_id, int flags) {
  VLOG_REAPER() << "OnSetFxChange(track_id=" << track_id
                << ", flags=" << absl::Hex(flags, absl::kZeroPad8) << ")";
}

void ControlSurface::OnSetProjectMarkerChange() {
  VLOG_REAPER() << "OnSetProjectMarkerChange";
}

void ControlSurface::OnTrackFxPresetChanged(MediaTrack* track_id, int fx_idx) {
  VLOG_REAPER() << "OnTrackFxPresetChanged(track_id=" << track_id
                << ", fx_idx=" << fx_idx << ")";
}

bool ControlSurface::OnSupportsExtendedTouch() {
  VLOG_REAPER() << "OnSupportsExtendedTouch() -> false";
  return false;
}

void ControlSurface::OnMidiDeviceRemap(bool is_out, int old_idx, int new_idx) {
  VLOG_REAPER() << "OnMidiDeviceRemap(is_out=" << is_out
                << ", old_idx=" << old_idx << ", new_idx=" << new_idx << ")";
}

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------

void ControlSurface::LogRunTime(absl::Time start, absl::Time end) {
  absl::Duration run_time = end - start;
  max_run_time_ = std::max(max_run_time_, run_time);
  elapsed_run_time_ += run_time;
  ++run_count_;
  if (last_log_time_ + kLogInterval < end) {
    LOG(INFO) << "Run() " << run_count_
              << " times, max: " << absl::ToInt64Microseconds(max_run_time_)
              << "us, avg: "
              << std::ceil(absl::ToDoubleMicroseconds(elapsed_run_time_) /
                           run_count_)
              << "us, avg/sec: "
              << std::ceil(absl::ToDoubleMicroseconds(elapsed_run_time_) /
                           absl::ToDoubleSeconds(kLogInterval))
              << "us";
    last_log_time_ = end;
    elapsed_run_time_ = absl::ZeroDuration();
    max_run_time_ = absl::ZeroDuration();
    run_count_ = 0;
  }
}

#undef JPR_GET_PARAM_VALUE
#undef VLOG_REAPER

}  // namespace jpr
