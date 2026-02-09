// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "control_surface.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace jpr {

namespace {

constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

}  // namespace

reaper_csurf_reg_t* ControlSurface::GetControlSurfaceReg() {
  static reaper_csurf_reg_t reg = {kTypeString, kDescString,
                                   ControlSurface::Create,
                                   ControlSurface::ShowConfig};
  return &reg;
}

IReaperControlSurface* ControlSurface::Create(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  LOG(INFO) << "REAPER: Create(type_string="
            << (type_string ? type_string : "(null)") << ", config_string=\""
            << (config_string ? config_string : "") << "\")";
  return new ControlSurface(type_string != nullptr ? type_string : "",
                            config_string != nullptr ? config_string : "");
}

HWND ControlSurface::ShowConfig(const char* type_string, HWND parent,
                                const char* init_config_string) {
  LOG(INFO) << "REAPER: ShowConfig(type_string="
            << (type_string ? type_string : "(null)") << ", parent=" << parent
            << ", init_config_string=\""
            << (init_config_string ? init_config_string : "") << "\")";
  return 0;
}

ControlSurface::ControlSurface(std::string type_string,
                               std::string config_string)
    : type_string_(std::move(type_string)),
      config_string_(std::move(config_string)) {
  LOG(INFO) << "ControlSurface created";
}

ControlSurface::~ControlSurface() { LOG(INFO) << "ControlSurface destroyed"; }

const char* ControlSurface::GetTypeString() { return "JPRSurf"; }

const char* ControlSurface::GetDescString() { return kDescString; }

const char* ControlSurface::GetConfigString() {
  LOG(INFO) << "REAPER: GetConfigString() -> \"" << config_string_ << "\"";
  return config_string_.c_str();
}

void ControlSurface::Run() {
  // TODO: Called ~30x/sec, poll and sync control surface state here
}

void ControlSurface::SetTrackListChange() {
  LOG(INFO) << "REAPER: SetTrackListChange";
}

void ControlSurface::SetSurfaceVolume(MediaTrack* track_id, double volume) {
  LOG(INFO) << "REAPER: SetSurfaceVolume(track_id=" << track_id
            << ", volume=" << volume << ")";
}

void ControlSurface::SetSurfacePan(MediaTrack* track_id, double pan) {
  LOG(INFO) << "REAPER: SetSurfacePan(track_id=" << track_id << ", pan=" << pan
            << ")";
}

void ControlSurface::SetSurfaceMute(MediaTrack* track_id, bool mute) {
  LOG(INFO) << "REAPER: SetSurfaceMute(track_id=" << track_id
            << ", mute=" << mute << ")";
}

void ControlSurface::SetSurfaceSelected(MediaTrack* track_id, bool selected) {
  LOG(INFO) << "REAPER: SetSurfaceSelected(track_id=" << track_id
            << ", selected=" << selected << ")";
}

void ControlSurface::SetSurfaceSolo(MediaTrack* track_id, bool solo) {
  LOG(INFO) << "REAPER: SetSurfaceSolo(track_id=" << track_id
            << ", solo=" << solo << ")";
}

void ControlSurface::SetSurfaceRecArm(MediaTrack* track_id, bool rec_arm) {
  LOG(INFO) << "REAPER: SetSurfaceRecArm(track_id=" << track_id
            << ", rec_arm=" << rec_arm << ")";
}

void ControlSurface::SetPlayState(bool play, bool pause, bool rec) {
  LOG(INFO) << "REAPER: SetPlayState(play=" << play << ", pause=" << pause
            << ", rec=" << rec << ")";
}

void ControlSurface::SetRepeatState(bool rep) {
  LOG(INFO) << "REAPER: SetRepeatState(rep=" << rep << ")";
}

void ControlSurface::SetTrackTitle(MediaTrack* track_id, const char* title) {
  LOG(INFO) << "REAPER: SetTrackTitle(track_id=" << track_id << ", title=\""
            << (title != nullptr ? title : "(null)") << "\")";
}

bool ControlSurface::GetTouchState(MediaTrack* track_id, int is_pan) {
  LOG(INFO) << "REAPER: GetTouchState(track_id=" << track_id
            << ", is_pan=" << is_pan << ")";
  return false;
}

void ControlSurface::SetAutoMode(int mode) {
  LOG(INFO) << "REAPER: SetAutoMode(mode=" << mode << ")";
}

void ControlSurface::ResetCachedVolPanStates() {
  LOG(INFO) << "REAPER: ResetCachedVolPanStates";
}

void ControlSurface::OnTrackSelection(MediaTrack* track_id) {
  LOG(INFO) << "REAPER: OnTrackSelection(track_id=" << track_id << ")";
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
      JPR_GET_PARAM_VALUE(int, enabled, 1);
      OnSetMetronome(enabled != 0);
      return 0;
    }
    case CSURF_EXT_SETAUTORECARM: {
      JPR_GET_PARAM_VALUE(int, enabled, 1);
      OnSetAutoRecArm(enabled != 0);
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
      auto enabled = param3 != nullptr;
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
      JPR_GET_PARAM_VALUE(int, open, 3);
      OnSetFxOpen(track_id, fx_idx, open != 0);
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
  LOG(INFO) << "REAPER: Extended(call=" << absl::Hex(call, absl::kZeroPad8)
            << "): Unsupported call";
  return 0;
}

void ControlSurface::OnReset() { LOG(INFO) << "REAPER: OnReset"; }

void ControlSurface::OnSetInputMonitor(MediaTrack* track_id, int rec_monitor) {
  LOG(INFO) << "REAPER: OnSetInputMonitor(track_id=" << track_id
            << ", rec_monitor=" << rec_monitor << ")";
}

void ControlSurface::OnSetMetronome(bool enabled) {
  LOG(INFO) << "REAPER: OnSetMetronome(enabled=" << enabled << ")";
}

void ControlSurface::OnSetAutoRecArm(bool auto_rec_arm) {
  LOG(INFO) << "REAPER: OnSetAutoRecArm(auto_rec_arm=" << auto_rec_arm << ")";
}

void ControlSurface::OnSetRecMode(int rec_mode) {
  LOG(INFO) << "REAPER: OnSetRecMode(rec_mode=" << rec_mode << ")";
}

void ControlSurface::OnSetSendVolume(MediaTrack* track_id, int send_idx,
                                     double volume) {
  LOG(INFO) << "REAPER: OnSetSendVolume(track_id=" << track_id
            << ", send_idx=" << send_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetSendPan(MediaTrack* track_id, int send_idx,
                                  double pan) {
  LOG(INFO) << "REAPER: OnSetSendPan(track_id=" << track_id
            << ", send_idx=" << send_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxEnabled(MediaTrack* track_id, int fx_idx,
                                    bool enabled) {
  LOG(INFO) << "REAPER: OnSetFxEnabled(track_id=" << track_id
            << ", fx_idx=" << fx_idx << ", enabled=" << enabled << ")";
}

void ControlSurface::OnSetFxParam(MediaTrack* track_id, int fx_idx,
                                  int param_idx, double normalized_value) {
  LOG(INFO) << "REAPER: OnSetFxParam(track_id=" << track_id
            << ", fx_idx=" << fx_idx << ", param_idx=" << param_idx
            << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetFxParamRecFx(MediaTrack* track_id, int fx_idx,
                                       int param_idx, double normalized_value) {
  LOG(INFO) << "REAPER: OnSetFxParamRecFx(track_id=" << track_id
            << ", fx_idx=" << fx_idx << ", param_idx=" << param_idx
            << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetBpmAndPlayRate(std::optional<double> bpm,
                                         std::optional<double> play_rate) {
  LOG(INFO) << "REAPER: OnSetBpmAndPlayRate(bpm="
            << (bpm.has_value() ? absl::StrCat(bpm.value()) : "(null)")
            << ", play_rate="
            << (play_rate.has_value() ? absl::StrCat(play_rate.value())
                                      : "(null)")
            << ")";
}

void ControlSurface::OnClearLastTouchedFx() {
  LOG(INFO) << "REAPER: OnClearLastTouchedFx";
}

void ControlSurface::OnSetLastTouchedFx(MediaTrack* track_id,
                                        std::optional<int> media_item_idx,
                                        int fx_idx) {
  LOG(INFO) << "REAPER: OnSetLastTouchedFx(track_id=" << track_id
            << ", media_item_idx="
            << (media_item_idx.has_value()
                    ? absl::StrCat(media_item_idx.value())
                    : "(null)")
            << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnClearFocusedFx() {
  LOG(INFO) << "REAPER: OnClearFocusedFx";
}

void ControlSurface::OnSetFocusedFx(MediaTrack* track_id,
                                    std::optional<int> media_item_idx,
                                    int fx_idx) {
  LOG(INFO) << "REAPER: OnSetFocusedFx(track_id=" << track_id
            << ", media_item_idx="
            << (media_item_idx.has_value()
                    ? absl::StrCat(media_item_idx.value())
                    : "(null)")
            << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnSetLastTouchedTrack(MediaTrack* track_id) {
  LOG(INFO) << "REAPER: OnSetLastTouchedTrack(track_id=" << track_id << ")";
}

void ControlSurface::OnSetMixerScroll(MediaTrack* track_id) {
  LOG(INFO) << "REAPER: OnSetMixerScroll(track_id=" << track_id << ")";
}

void ControlSurface::OnSetPanEx(MediaTrack* track_id,
                                absl::Span<const double> pan, int mode) {
  LOG(INFO) << "REAPER: OnSetPanEx(track_id=" << track_id << ", pan=["
            << absl::StrJoin(pan, ", ") << "], mode=" << mode << ")";
}

void ControlSurface::OnSetRecvVolume(MediaTrack* track_id, int rec_idx,
                                     double volume) {
  LOG(INFO) << "REAPER: OnSetRecvVolume(track_id=" << track_id
            << ", rec_idx=" << rec_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetRecvPan(MediaTrack* track_id, int rec_idx,
                                  double pan) {
  LOG(INFO) << "REAPER: OnSetRecvPan(track_id=" << track_id
            << ", rec_idx=" << rec_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxOpen(MediaTrack* track_id, int fx_idx, bool open) {
  LOG(INFO) << "REAPER: OnSetFxOpen(track_id=" << track_id
            << ", fx_idx=" << fx_idx << ", open=" << open << ")";
}

void ControlSurface::OnSetFxChange(MediaTrack* track_id, int flags) {
  LOG(INFO) << "REAPER: OnSetFxChange(track_id=" << track_id
            << ", flags=" << absl::Hex(flags, absl::kZeroPad8) << ")";
}

void ControlSurface::OnSetProjectMarkerChange() {
  LOG(INFO) << "REAPER: OnSetProjectMarkerChange";
}

void ControlSurface::OnTrackFxPresetChanged(MediaTrack* track_id, int fx_idx) {
  LOG(INFO) << "REAPER: OnTrackFxPresetChanged(track_id=" << track_id
            << ", fx_idx=" << fx_idx << ")";
}

bool ControlSurface::OnSupportsExtendedTouch() {
  LOG(INFO) << "REAPER: OnSupportsExtendedTouch";
  return false;
}

void ControlSurface::OnMidiDeviceRemap(bool is_out, int old_idx, int new_idx) {
  LOG(INFO) << "REAPER: OnMidiDeviceRemap(is_out=" << is_out
            << ", old_idx=" << old_idx << ", new_idx=" << new_idx << ")";
}

}  // namespace jpr