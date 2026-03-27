// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/control_surface.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gb/config/text_config.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/track_cache.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/reaper_property.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

const GUID kEmptyGuid = {};
constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

}  // namespace

#define LOG_REAPER() LOG(INFO) << "REAPER: "
#define VLOG_REAPER() VLOG(1) << "REAPER: "

reaper_csurf_reg_t* ControlSurface::GetControlSurfaceReg() {
  static reaper_csurf_reg_t reg = {kTypeString, kDescString,
                                   ControlSurface::Create,
                                   ControlSurface::ShowConfig};
  return &reg;
}

IReaperControlSurface* ControlSurface::Create(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  VLOG_REAPER() << "Create(type_string="
                << (type_string ? type_string : "(null)")
                << ", config_string=\"" << (config_string ? config_string : "")
                << "\")";
  return new ControlSurface(type_string != nullptr ? type_string : "",
                            config_string != nullptr ? config_string : "");
}

HWND ControlSurface::ShowConfig(const char* type_string, HWND parent,
                                const char* init_config_string) {
  VLOG_REAPER() << "ShowConfig(type_string="
                << (type_string ? type_string : "(null)")
                << ", parent=" << parent << ", init_config_string=\""
                << (init_config_string ? init_config_string : "") << "\")";
  return 0;
}

ControlSurface::ControlSurface(std::string type_string,
                               std::string config_string)
    : type_string_(std::move(type_string)) {
  if (!config_string.empty()) {
    auto config = gb::ReadConfigFromText(config_string);
    if (!config.ok()) {
      LOG(ERROR) << "Failed to parse control surface config: "
                 << config.status();
      LOG(ERROR) << config_string;
    }
  }
  LOG(INFO) << "ControlSurface created";

  ConnectDevices();
  InitViews();
}

ControlSurface::~ControlSurface() { LOG(INFO) << "ControlSurface destroyed"; }

//------------------------------------------------------------------------------
// Reaper callbacks
//------------------------------------------------------------------------------

const char* ControlSurface::GetTypeString() { return "JPRSurf"; }

const char* ControlSurface::GetDescString() { return kDescString; }

const char* ControlSurface::GetConfigString() {
  config_string_ = gb::WriteConfigToText(config_, gb::kCompactTextConfig);
  VLOG_REAPER() << "GetConfigString() -> \"" << config_string_ << "\"";
  return config_string_.c_str();
}

constexpr absl::Duration kLogInterval = absl::Seconds(5);

void ControlSurface::Run() {
  absl::Time start = absl::Now();

  if (track_list_changed_) {
    LOG(INFO) << "Refreshing TrackCache!";
    TrackCache::Get().Refresh();
    if (master_track_view_ != nullptr) {
      master_track_view_->SetTrackContext(TrackCache::Get().GetMasterTrack());
    }
    track_list_view_->RefreshChildContext();
    track_list_changed_ = false;
  }

  device_runner_.Run();
  midi_in_runner_.Run();
  scene_runner_.Run();
  midi_out_runner_.Run();

  absl::Time end = absl::Now();
  elapsed_run_time_ += end - start;
  ++run_count_;
  if (last_log_time_ + kLogInterval < end) {
    LOG(INFO) << "Run() " << run_count_ << " times, avg/run: "
              << std::ceil(absl::ToDoubleMicroseconds(elapsed_run_time_) /
                           run_count_)
              << "us, avg/sec: "
              << std::ceil(absl::ToDoubleMicroseconds(elapsed_run_time_) /
                           absl::ToDoubleSeconds(kLogInterval))
              << "us";
    last_log_time_ = end;
    elapsed_run_time_ = absl::ZeroDuration();
    run_count_ = 0;
  }
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
}

void ControlSurface::ResetCachedVolPanStates() {
  VLOG_REAPER() << "ResetCachedVolPanStates";
}

void ControlSurface::OnTrackSelection(MediaTrack* track_id) {
  VLOG_REAPER() << "OnTrackSelection(track_id=" << track_id << ")";

  // TODO: Ensure track is in view
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

#define JPR_GET_PARAM_VALUE(Type, name, index)               \
  if (!CheckParamValue(call, index, param##index)) return 0; \
  auto name = *static_cast<Type*>(param##index);

}  // namespace

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
      JPR_GET_PARAM_VALUE(int, flags, 2);
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
  if (track_list_view_ != nullptr) {
    Track* track = TrackCache::Get().GetTrack(track_id);
    EnsureTrackIsVisible(TrackCache::Get().GetTrack(track_id));
    TrackCache::Get().SetLastTouchedTrack(TrackCache::Get().GetTrack(track_id));
  }
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

void ControlSurface::ConnectDevices() {
  for (auto& port : MidiIn::GetPorts()) {
    LOG(INFO) << "MIDI Input Port: " << port->GetName() << " (index "
              << port->GetIndex() << ")";
    if (port->GetName() == "X-Touch") {
      xtouch_in_ = std::move(port);
      if (!xtouch_in_->Open(midi_in_runner_)) {
        LOG(ERROR) << "Failed to open MIDI input port for X-Touch";
        xtouch_in_.reset();
      }
    } else if (port->GetName() == "X-Touch-Ext") {
      xtouch_ext_in_ = std::move(port);
      if (!xtouch_ext_in_->Open(midi_in_runner_)) {
        LOG(ERROR) << "Failed to open MIDI input port for X-Touch Extender";
        xtouch_ext_in_.reset();
      }
    }
  }
  for (auto& port : MidiOut::GetPorts()) {
    LOG(INFO) << "MIDI Output Port: " << port->GetName() << " (index "
              << port->GetIndex() << ")";
    if (port->GetName() == "X-Touch") {
      xtouch_out_ = std::move(port);
      if (!xtouch_out_->Open(midi_out_runner_)) {
        LOG(ERROR) << "Failed to open MIDI output port for X-Touch";
        xtouch_out_.reset();
      }
    } else if (port->GetName() == "X-Touch-Ext") {
      xtouch_ext_out_ = std::move(port);
      if (!xtouch_ext_out_->Open(midi_out_runner_)) {
        LOG(ERROR) << "Failed to open MIDI output port for X-Touch Extender";
        xtouch_ext_out_.reset();
      }
    }
  }
}

void ControlSurface::InitViews() {
  // Note: For now we are just hard-coding views to my development setup, which
  // is an X-Touch and X-Touch Extender, with the X-Touch extender to the left
  // of the X-Touch.
  scene_ = std::make_unique<Scene>("Scene");
  bool has_xtouch = (xtouch_in_ != nullptr && xtouch_out_ != nullptr);
  bool has_xtouch_ext =
      (xtouch_ext_in_ != nullptr && xtouch_ext_out_ != nullptr);
  constexpr std::string_view kModMarker = "mod_marker";
  constexpr std::string_view kModNudge = "mod_nudge";
  Modifiers mod_marker = 0;
  Modifiers mod_nudge = 0;
  if (has_xtouch) {
    scene_->AddDevice("XTouch", std::make_unique<DeviceXTouch>(
                                    DeviceXTouch::Type::kFull, device_runner_,
                                    xtouch_in_.get(), xtouch_out_.get()));
    mod_marker = scene_->AddModifierProperty(kModMarker);
    mod_nudge = scene_->AddModifierProperty(kModNudge);
  }
  if (has_xtouch_ext) {
    scene_->AddDevice("XTouchExt",
                      std::make_unique<DeviceXTouch>(
                          DeviceXTouch::Type::kExtender, device_runner_,
                          xtouch_ext_in_.get(), xtouch_ext_out_.get()));
  }

  // Add global mappings
  auto* root_view = scene_->GetRootView();
  if (has_xtouch) {
    // Master fader
    master_track_view_ = root_view->AddChildView("MasterFader");
    master_track_view_->SetTrackContext();
    master_track_view_->AddMapping(
        ViewMapping::kReadWriteControl, TrackProperties::kVolume,
        absl::StrCat("XTouch/", DeviceXTouch::kMasterFader));
    master_track_view_->Enable();

    // Modifiers
    root_view->AddMapping(ViewMapping::kReadWriteControl,
                          ModifierProperty::kShift,
                          absl::StrCat("XTouch/", DeviceXTouch::kShift),
                          {.read = {.press_release = true}});
    root_view->AddMapping(ViewMapping::kReadWriteControl,
                          ModifierProperty::kCtrl,
                          absl::StrCat("XTouch/", DeviceXTouch::kControl),
                          {.read = {.press_release = true}});
    root_view->AddMapping(ViewMapping::kReadWriteControl,
                          ModifierProperty::kAlt,
                          absl::StrCat("XTouch/", DeviceXTouch::kAlt),
                          {.read = {.press_release = true}});
    root_view->AddMapping(ViewMapping::kReadWriteControl,
                          ModifierProperty::kOpt,
                          absl::StrCat("XTouch/", DeviceXTouch::kOption),
                          {.read = {.press_release = true}});

    // Misc buttons (above transport)
    root_view->AddMapping(ViewMapping::kReadWriteControl, kModMarker,
                          absl::StrCat("XTouch/", DeviceXTouch::kMarker));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kModNudge,
                          absl::StrCat("XTouch/", DeviceXTouch::kNudge));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRepeat,
                          absl::StrCat("XTouch/", DeviceXTouch::kCycle));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdMetronome,
                          absl::StrCat("XTouch/", DeviceXTouch::kClick));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdSoloInFront,
                          absl::StrCat("XTouch/", DeviceXTouch::kSolo));

    // Transport controls
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoPrevMeasure,
                          absl::StrCat("XTouch/", DeviceXTouch::kRewind));
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoPrevBeat,
                          absl::StrCat("XTouch/", DeviceXTouch::kRewind),
                          {.read = {.required_modifiers = mod_nudge}});
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoPrevMarker,
                          absl::StrCat("XTouch/", DeviceXTouch::kRewind),
                          {.read = {.required_modifiers = mod_marker}});
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoNextMeasure,
                          absl::StrCat("XTouch/", DeviceXTouch::kForward));
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoNextBeat,
                          absl::StrCat("XTouch/", DeviceXTouch::kForward),
                          {.read = {.required_modifiers = mod_nudge}});
    root_view->AddMapping(ViewMapping::kReadControl, kCmdGoNextMarker,
                          absl::StrCat("XTouch/", DeviceXTouch::kForward),
                          {.read = {.required_modifiers = mod_marker}});
    root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportStop,
                          absl::StrCat("XTouch/", DeviceXTouch::kStop));
    root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportPlayPause,
                          absl::StrCat("XTouch/", DeviceXTouch::kPlay));
    root_view->AddMapping(ViewMapping::kWriteControl, kCmdTransportPlay,
                          absl::StrCat("XTouch/", DeviceXTouch::kPlay));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRecord,
                          absl::StrCat("XTouch/", DeviceXTouch::kRecord));
  }
  root_view->Enable();

  // Add TrackList view with 8 track views, which will correspond to the 8
  // tracks on the X-Touch.
  track_list_view_ = root_view->AddChildView("TrackList");
  int child_view_index = 0;
  for (int d = 0; d < 2; ++d) {
    if ((d == 0 && !has_xtouch_ext) || (d == 1 && !has_xtouch)) {
      continue;
    }
    std::string device_prefix = (d == 0) ? "XTouchExt/" : "XTouch/";
    for (int i = 0; i < 8; ++i) {
      View* track_view = track_list_view_->AddChildView(
          absl::StrCat("Track", ++child_view_index));
      // Sets the context to a track context, so we can map REAPER tracks to the
      // controls.
      track_view->SetTrackContext();

      // Add all the per-track controls
      track_view->AddMapping(
          ViewMapping::kReadWriteControl, TrackProperties::kUiSelected,
          absl::StrCat(device_prefix, DeviceXTouch::Select(i)));
      track_view->AddMapping(
          ViewMapping::kReadControl, View::kParentTrackChild,
          absl::StrCat(device_prefix, DeviceXTouch::Select(i)),
          {.read = {.press_behavior =
                        InputConfig::PressBehavior::kDoublePress}});
      track_view->AddMapping(
          ViewMapping::kReadControl, View::kParentTrackParent,
          absl::StrCat(device_prefix, DeviceXTouch::Select(i)),
          {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});
      track_view->AddMapping(
          ViewMapping::kReadWriteControl, TrackProperties::kUiMute,
          absl::StrCat(device_prefix, DeviceXTouch::Mute(i)));
      track_view->AddMapping(
          ViewMapping::kReadWriteControl, TrackProperties::kUiSolo,
          absl::StrCat(device_prefix, DeviceXTouch::Solo(i)));
      track_view->AddMapping(ViewMapping::kReadWriteControl,
                             TrackProperties::kUiRecArm,
                             absl::StrCat(device_prefix, DeviceXTouch::Rec(i)));
      track_view->AddMapping(ViewMapping::kReadWriteControl,
                             TrackProperties::kUiPan,
                             absl::StrCat(device_prefix, DeviceXTouch::Pot(i)),
                             {.write = {.mode = 1}});
      track_view->AddMapping(
          ViewMapping::kReadControl, TrackProperties::kUiPan,
          absl::StrCat(device_prefix, DeviceXTouch::PotButton(i)),
          {.read = {.property_min = 0.0, .property_max = 0.0}});
      track_view->AddMapping(
          ViewMapping::kReadWriteControl, TrackProperties::kUiVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Fader(i)));
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kName,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 0)));
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kUiVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 1)));
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kColor,
          absl::StrCat(device_prefix, DeviceXTouch::ScribbleColor(i)));
      track_view->Enable();
    }
  }
  track_list_view_->SetChildContext(View::ContextType::kTrack);
  if (has_xtouch) {
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kTrackRoot,
        absl::StrCat("XTouch/", DeviceXTouch::kGlobal));
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kChildDec,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelLeft));
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kChildInc,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelRight));
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kBankDec,
        absl::StrCat("XTouch/", DeviceXTouch::kBankLeft));
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kBankInc,
        absl::StrCat("XTouch/", DeviceXTouch::kBankRight));
  }
  track_list_view_->Enable();

  // Finally activate the scene, which will start it running and activate all
  // enabled views.
  scene_->Activate(scene_runner_);
}

void ControlSurface::EnsureTrackIsVisible(Track* track) {
  // This can happen if we get events for tracks before the TrachCache has been
  // refreshed which happens only when Run() is called.
  if (track == nullptr) {
    return;
  }
  const int num_tracks_in_view = track_list_view_->GetChildContextCount();
  const int track_index = track->GetIndex();
  const int last_child_context_index =
      std::max(0, track_index - num_tracks_in_view + 1);
  Track* parent_track = track->GetParentTrack();

  // If the track is already in the current view, we only need to make sure it
  // is in view, or do a minimum scroll to get it in view.
  if (track_list_view_->GetTrackContext() == parent_track) {
    int first_index = track_list_view_->GetChildContextIndex();
    if (track_index < first_index) {
      track_list_view_->SetChildContextIndex(track_index);
    } else if (track_index >= first_index + num_tracks_in_view) {
      track_list_view_->SetChildContextIndex(last_child_context_index);
    }
  } else if (parent_track == nullptr) {
    // We are switching to a top level track.
    track_list_view_->ClearContext(last_child_context_index);
  } else {
    // We are switching to a nested track.
    track_list_view_->SetTrackContext(parent_track, last_child_context_index);
  }
}

}  // namespace jpr