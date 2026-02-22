// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "control_surface.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "child_track_iterator.h"
#include "control_input_midi.h"
#include "control_output_midi.h"
#include "gb/config/text_config.h"
#include "midi_port.h"
#include "reaper_plugin_functions.h"

namespace jpr {

namespace {

const GUID kEmptyGuid = {};
constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

}  // namespace

#if 0
#define LOG_REAPER() LOG(INFO) << "REAPER: "
#else
#define LOG_REAPER() VLOG(1) << "REAPER: "
#endif

const GUID& ControlSurface::TrackView::GetGuid() const {
  return state.has_value() ? state->guid : kEmptyGuid;
}

reaper_csurf_reg_t* ControlSurface::GetControlSurfaceReg() {
  static reaper_csurf_reg_t reg = {kTypeString, kDescString,
                                   ControlSurface::Create,
                                   ControlSurface::ShowConfig};
  return &reg;
}

IReaperControlSurface* ControlSurface::Create(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  LOG_REAPER() << "Create(type_string="
               << (type_string ? type_string : "(null)") << ", config_string=\""
               << (config_string ? config_string : "") << "\")";
  return new ControlSurface(type_string != nullptr ? type_string : "",
                            config_string != nullptr ? config_string : "");
}

HWND ControlSurface::ShowConfig(const char* type_string, HWND parent,
                                const char* init_config_string) {
  LOG_REAPER() << "ShowConfig(type_string="
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
  LOG_REAPER() << "GetConfigString() -> \"" << config_string_ << "\"";
  return config_string_.c_str();
}

void ControlSurface::Run() { runner_.Run(); }

void ControlSurface::SetTrackListChange() {
  LOG_REAPER() << "SetTrackListChange";
  RebuildTracks();
}

void ControlSurface::SetSurfaceVolume(MediaTrack* track_id, double volume) {
  LOG_REAPER() << "SetSurfaceVolume(track_id=" << track_id
               << ", volume=" << volume << ")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->volume != volume) {
    LOG(INFO) << "Track \"" << track->name << "\": Volume " << track->volume
              << " -> " << volume;
    track->volume = volume;
  }
}

void ControlSurface::SetSurfacePan(MediaTrack* track_id, double pan) {
  LOG_REAPER() << "SetSurfacePan(track_id=" << track_id << ", pan=" << pan
               << ")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->pan != pan) {
    LOG(INFO) << "Track \"" << track->name << "\": Pan " << track->pan << " -> "
              << pan;
    track->pan = pan;
  }
}

void ControlSurface::SetSurfaceMute(MediaTrack* track_id, bool mute) {
  LOG_REAPER() << "SetSurfaceMute(track_id=" << track_id << ", mute=" << mute
               << ")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->flags.IsSet(TrackFlag::kMute) != mute) {
    LOG(INFO) << "Track \"" << track->name << "\": Mute "
              << track->flags.IsSet(TrackFlag::kMute) << " -> " << mute;
    if (mute) {
      track->flags.Set(TrackFlag::kMute);
    } else {
      track->flags.Clear(TrackFlag::kMute);
    }
  }
}

void ControlSurface::SetSurfaceSelected(MediaTrack* track_id, bool selected) {
  LOG_REAPER() << "SetSurfaceSelected(track_id=" << track_id
               << ", selected=" << selected << ")";
  TrackView* track_view = GetTrackView(track_id);
  if (track_view == nullptr) {
    return;
  }
  auto& track = track_view->state;
  if (!track.has_value() ||
      track->flags.IsSet(TrackFlag::kSelected) == selected) {
    return;
  }

  LOG(INFO) << "Track \"" << track->name << "\": Selected "
            << track->flags.IsSet(TrackFlag::kSelected) << " -> " << selected;
  if (selected) {
    track->flags.Set(TrackFlag::kSelected);
  } else {
    track->flags.Clear(TrackFlag::kSelected);
  }
  track_view->select_output->SetValue(selected ? 1 : 0);
}

void ControlSurface::SetSurfaceSolo(MediaTrack* track_id, bool solo) {
  LOG_REAPER() << "SetSurfaceSolo(track_id=" << track_id << ", solo=" << solo
               << ")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->flags.IsSet(TrackFlag::kSolo) != solo) {
    LOG(INFO) << "Track \"" << track->name << "\": Solo "
              << track->flags.IsSet(TrackFlag::kSolo) << " -> " << solo;
    if (solo) {
      track->flags.Set(TrackFlag::kSolo);
    } else {
      track->flags.Clear(TrackFlag::kSolo);
    }
  }
}

void ControlSurface::SetSurfaceRecArm(MediaTrack* track_id, bool rec_arm) {
  LOG_REAPER() << "SetSurfaceRecArm(track_id=" << track_id
               << ", rec_arm=" << rec_arm << ")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->flags.IsSet(TrackFlag::kRecArm) != rec_arm) {
    LOG(INFO) << "Track \"" << track->name << "\": RecArm "
              << track->flags.IsSet(TrackFlag::kRecArm) << " -> " << rec_arm;
    if (rec_arm) {
      track->flags.Set(TrackFlag::kRecArm);
    } else {
      track->flags.Clear(TrackFlag::kRecArm);
    }
  }
}

void ControlSurface::SetPlayState(bool play, bool pause, bool rec) {
  LOG_REAPER() << "SetPlayState(play=" << play << ", pause=" << pause
               << ", rec=" << rec << ")";
}

void ControlSurface::SetRepeatState(bool rep) {
  LOG_REAPER() << "SetRepeatState(rep=" << rep << ")";
}

void ControlSurface::SetTrackTitle(MediaTrack* track_id, const char* title) {
  LOG_REAPER() << "SetTrackTitle(track_id=" << track_id << ", title=\""
               << (title != nullptr ? title : "(null)") << "\")";
  if (TrackState* track = GetTrackState(track_id);
      track != nullptr && track->name != title) {
    LOG(INFO) << "Track \"" << track->name << "\": Title \"" << track->name
              << "\" -> \"" << title << "\"";
    track->name = title;
  }
}

bool ControlSurface::GetTouchState(MediaTrack* track_id, int is_pan) {
  LOG_REAPER() << "GetTouchState(track_id=" << track_id << ", is_pan=" << is_pan
               << ")";
  return false;
}

void ControlSurface::SetAutoMode(int mode) {
  LOG_REAPER() << "SetAutoMode(mode=" << mode << ")";
}

void ControlSurface::ResetCachedVolPanStates() {
  LOG_REAPER() << "ResetCachedVolPanStates";
}

void ControlSurface::OnTrackSelection(MediaTrack* track_id) {
  LOG_REAPER() << "OnTrackSelection(track_id=" << track_id << ")";

  int flags = 0;
  const char* track_name =
      GetTrackInfo(reinterpret_cast<INT_PTR>(track_id), &flags);
  if (flags & 2) {
    LOG(INFO) << "Track \"" << track_name << "\" selected";
    EnsureTrackIsInView(track_list_view_, track_id);
  }
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
  LOG_REAPER() << "Extended(call=" << absl::Hex(call, absl::kZeroPad8)
               << "): Unsupported call";
  return 0;
}

void ControlSurface::OnReset() { LOG_REAPER() << "OnReset"; }

void ControlSurface::OnSetInputMonitor(MediaTrack* track_id, int rec_monitor) {
  LOG_REAPER() << "OnSetInputMonitor(track_id=" << track_id
               << ", rec_monitor=" << rec_monitor << ")";
}

void ControlSurface::OnSetMetronome(bool enabled) {
  LOG_REAPER() << "OnSetMetronome(enabled=" << enabled << ")";
}

void ControlSurface::OnSetAutoRecArm(bool auto_rec_arm) {
  LOG_REAPER() << "OnSetAutoRecArm(auto_rec_arm=" << auto_rec_arm << ")";
}

void ControlSurface::OnSetRecMode(int rec_mode) {
  LOG_REAPER() << "OnSetRecMode(rec_mode=" << rec_mode << ")";
}

void ControlSurface::OnSetSendVolume(MediaTrack* track_id, int send_idx,
                                     double volume) {
  LOG_REAPER() << "OnSetSendVolume(track_id=" << track_id
               << ", send_idx=" << send_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetSendPan(MediaTrack* track_id, int send_idx,
                                  double pan) {
  LOG_REAPER() << "OnSetSendPan(track_id=" << track_id
               << ", send_idx=" << send_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxEnabled(MediaTrack* track_id, int fx_idx,
                                    bool enabled) {
  LOG_REAPER() << "OnSetFxEnabled(track_id=" << track_id
               << ", fx_idx=" << fx_idx << ", enabled=" << enabled << ")";
}

void ControlSurface::OnSetFxParam(MediaTrack* track_id, int fx_idx,
                                  int param_idx, double normalized_value) {
  LOG_REAPER() << "OnSetFxParam(track_id=" << track_id << ", fx_idx=" << fx_idx
               << ", param_idx=" << param_idx
               << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetFxParamRecFx(MediaTrack* track_id, int fx_idx,
                                       int param_idx, double normalized_value) {
  LOG_REAPER() << "OnSetFxParamRecFx(track_id=" << track_id
               << ", fx_idx=" << fx_idx << ", param_idx=" << param_idx
               << ", normalized_value=" << normalized_value << ")";
}

void ControlSurface::OnSetBpmAndPlayRate(std::optional<double> bpm,
                                         std::optional<double> play_rate) {
  LOG_REAPER() << "OnSetBpmAndPlayRate(bpm="
               << (bpm.has_value() ? absl::StrCat(bpm.value()) : "(null)")
               << ", play_rate="
               << (play_rate.has_value() ? absl::StrCat(play_rate.value())
                                         : "(null)")
               << ")";
}

void ControlSurface::OnClearLastTouchedFx() {
  LOG_REAPER() << "OnClearLastTouchedFx";
}

void ControlSurface::OnSetLastTouchedFx(MediaTrack* track_id,
                                        std::optional<int> media_item_idx,
                                        int fx_idx) {
  LOG_REAPER() << "OnSetLastTouchedFx(track_id=" << track_id
               << ", media_item_idx="
               << (media_item_idx.has_value()
                       ? absl::StrCat(media_item_idx.value())
                       : "(null)")
               << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnClearFocusedFx() { LOG_REAPER() << "OnClearFocusedFx"; }

void ControlSurface::OnSetFocusedFx(MediaTrack* track_id,
                                    std::optional<int> media_item_idx,
                                    int fx_idx) {
  LOG_REAPER() << "OnSetFocusedFx(track_id=" << track_id << ", media_item_idx="
               << (media_item_idx.has_value()
                       ? absl::StrCat(media_item_idx.value())
                       : "(null)")
               << ", fx_idx=" << fx_idx << ")";
}

void ControlSurface::OnSetLastTouchedTrack(MediaTrack* track_id) {
  LOG_REAPER() << "OnSetLastTouchedTrack(track_id=" << track_id << ")";
}

void ControlSurface::OnSetMixerScroll(MediaTrack* track_id) {
  LOG_REAPER() << "OnSetMixerScroll(track_id=" << track_id << ")";
}

void ControlSurface::OnSetPanEx(MediaTrack* track_id,
                                absl::Span<const double> pan, int mode) {
  LOG_REAPER() << "OnSetPanEx(track_id=" << track_id << ", pan=["
               << absl::StrJoin(pan, ", ") << "], mode=" << mode << ")";
}

void ControlSurface::OnSetRecvVolume(MediaTrack* track_id, int rec_idx,
                                     double volume) {
  LOG_REAPER() << "OnSetRecvVolume(track_id=" << track_id
               << ", rec_idx=" << rec_idx << ", volume=" << volume << ")";
}

void ControlSurface::OnSetRecvPan(MediaTrack* track_id, int rec_idx,
                                  double pan) {
  LOG_REAPER() << "OnSetRecvPan(track_id=" << track_id
               << ", rec_idx=" << rec_idx << ", pan=" << pan << ")";
}

void ControlSurface::OnSetFxOpen(MediaTrack* track_id, int fx_idx, bool open) {
  LOG_REAPER() << "OnSetFxOpen(track_id=" << track_id << ", fx_idx=" << fx_idx
               << ", open=" << open << ")";
}

void ControlSurface::OnSetFxChange(MediaTrack* track_id, int flags) {
  LOG_REAPER() << "OnSetFxChange(track_id=" << track_id
               << ", flags=" << absl::Hex(flags, absl::kZeroPad8) << ")";
}

void ControlSurface::OnSetProjectMarkerChange() {
  LOG_REAPER() << "OnSetProjectMarkerChange";
}

void ControlSurface::OnTrackFxPresetChanged(MediaTrack* track_id, int fx_idx) {
  LOG_REAPER() << "OnTrackFxPresetChanged(track_id=" << track_id
               << ", fx_idx=" << fx_idx << ")";
}

bool ControlSurface::OnSupportsExtendedTouch() {
  LOG_REAPER() << "OnSupportsExtendedTouch() -> false";
  return false;
}

void ControlSurface::OnMidiDeviceRemap(bool is_out, int old_idx, int new_idx) {
  LOG_REAPER() << "OnMidiDeviceRemap(is_out=" << is_out
               << ", old_idx=" << old_idx << ", new_idx=" << new_idx << ")";
}

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------

ControlSurface::GuidKey ControlSurface::GuidToKey(const GUID& guid) {
  GuidKey key;
  static_assert(sizeof(GuidKey) == sizeof(GUID));
  std::memcpy(key.data(), &guid, sizeof(GUID));
  return key;
}

void ControlSurface::ConnectDevices() {
  for (auto& port : MidiIn::GetPorts()) {
    LOG(INFO) << "MIDI Input Port: " << port->GetName() << " (index "
              << port->GetIndex() << ")";
    if (port->GetName() == "X-Touch") {
      xtouch_in_ = std::move(port);
      if (!xtouch_in_->Open(runner_)) {
        LOG(ERROR) << "Failed to open MIDI input port for X-Touch";
        xtouch_in_.reset();
      }
    }
  }
  for (auto& port : MidiOut::GetPorts()) {
    LOG(INFO) << "MIDI Output Port: " << port->GetName() << " (index "
              << port->GetIndex() << ")";
    if (port->GetName() == "X-Touch") {
      xtouch_out_ = std::move(port);
      if (!xtouch_out_->Open(runner_)) {
        LOG(ERROR) << "Failed to open MIDI output port for X-Touch";
        xtouch_out_.reset();
      }
    }
  }
}

void ControlSurface::InitViews() {
  // Note: For now we are just hard-coding views to my development setup, which
  // is an X-Touch and X-Touch Extender, with the X-Touch extender to the left
  // of the X-Touch.

  // TODO: Just starting with the main X-Touch for now, which has 8 tracks.
  track_list_view_.track_views.resize(8);
  if (xtouch_in_ == nullptr) {
    LOG(WARNING)
        << "X-Touch MIDI input port not found, no tracks will be shown";
  }
  uint8_t note = 0x18;  // First select button on the X-Touch.
  int view_index = 0;
  for (TrackView& track_view : track_list_view_.track_views) {
    track_view.select_input = std::make_unique<ControlPressInputMidiMsg>(
        xtouch_in_.get(), MidiNoteOn(0, note, 0x7F));
    track_view.select_input->SetListener(
        [this, view_index](ControlPressInput* input) {
          OnTrackViewSelectPressed(track_list_view_.track_views[view_index]);
        });
    track_view.select_output = std::make_unique<ControlDValueOutputMidiNote>(
        xtouch_out_.get(), /*channel=*/0, note, /*use_note_off=*/false);
    ++note;
    ++view_index;
  }
}

ControlSurface::TrackView* ControlSurface::GetTrackView(MediaTrack* track_id) {
  if (master_track_view_.state.has_value() &&
      master_track_view_.state->track_id == track_id) {
    return &master_track_view_;
  }
  for (TrackView& track_view : track_list_view_.track_views) {
    if (track_view.state.has_value() &&
        track_view.state->track_id == track_id) {
      return &track_view;
    }
  }
  return nullptr;
}

ControlSurface::TrackState* ControlSurface::GetTrackState(
    MediaTrack* track_id) {
  if (master_track_view_.state.has_value() &&
      master_track_view_.state->track_id == track_id) {
    return &master_track_view_.state.value();
  }
  for (auto& track_view : track_list_view_.track_views) {
    if (track_view.state.has_value() &&
        track_view.state->track_id == track_id) {
      return &track_view.state.value();
    }
  }
  return nullptr;
}

void ControlSurface::RebuildTrackView(TrackView& track_view,
                                      const GUID& track_guid,
                                      MediaTrack* track_id, int track_index) {
  int flags = 0;
  track_view.state = TrackState{.guid = track_guid,
                                .track_id = track_id,
                                .name = GetTrackInfo(track_index, &flags)};
  if (track_view.state->name.empty()) {
    track_view.state->name = absl::StrCat("Track ", track_index + 1);
  }

  bool selected = ((flags & 2) != 0);
  if (selected) {
    track_view.state->flags.Set(TrackFlag::kSelected);
  }
  if (track_view.select_output != nullptr) {
    track_view.select_output->SetValue(selected ? 1 : 0);
  }

  bool mute = false;
  GetTrackUIMute(track_id, &mute);
  if (mute) {
    track_view.state->flags.Set(TrackFlag::kMute);
  }

  if ((flags & 16) != 0) {
    track_view.state->flags.Set(TrackFlag::kSolo);
  }

  if ((flags & 64) != 0) {
    track_view.state->flags.Set(TrackFlag::kRecArm);
  }

  GetTrackUIVolPan(track_id, &track_view.state->volume, &track_view.state->pan);
}

void ControlSurface::RefreshTrackListView(TrackListView& track_list_view) {
  auto& track_views = track_list_view.track_views;
  const int track_view_count = track_views.size();
  LOG(INFO) << "RefreshTrackListView: view count = " << track_view_count;

  // Find the first track index relative to the parent track, if specified. If
  // the parent track is not found, reset the parent track and index to show all
  // tracks from the top.
  MediaTrack* parent_track_id = nullptr;
  if (track_list_view.parent_track.has_value()) {
    if (auto it = track_guid_to_id_.find(
            GuidToKey(track_list_view.parent_track.value()));
        it == track_guid_to_id_.end()) {
      track_list_view.parent_track.reset();
      track_list_view.index = 0;
    } else {
      parent_track_id = it->second;
    }
  }

  // Get all tracks in the project until the view is full.
  int view_index = 0;
  for (ChildTrackIterator child(parent_track_id);
       !child.AtEnd() && view_index < track_view_count; child.Next()) {
    if (child.GetChildIndex() >= track_list_view.index) {
      TrackView& track_view = track_views[view_index];
      const GUID& track_guid = *GetTrackGUID(child.GetTrackId());
      if (track_guid != track_view.GetGuid() ||
          track_view.state->track_id != child.GetTrackId()) {
        LOG(INFO) << "Track view at index " << view_index
                  << " changed, resetting to track index " << child.GetIndex();
        RebuildTrackView(track_view, track_guid, child.GetTrackId(),
                         child.GetIndex());
      } else {
        LOG(INFO) << "Track view at index " << view_index
                  << " unchanged with track index " << child.GetIndex();
      }
      ++view_index;
    }
  }

  // Any remaining views need to be reset, as they are no longer showing valid
  // tracks.
  for (; view_index < track_view_count; ++view_index) {
    TrackView& track_view = track_views[view_index];
    if (track_view.state.has_value()) {
      LOG(INFO) << "Track view at index " << view_index
                << " changed, resetting to empty";
      track_view.state.reset();
      track_view.select_output->SetValue(0);
    } else {
      LOG(INFO) << "Track view at index " << view_index
                << " unchanged as empty";
    }
  }
}

void ControlSurface::RebuildTracks() {
  const int track_count = CountTracks(nullptr);
  LOG(INFO) << "RebuildTracks: Track count = " << track_count;

  // All MediaTrack* IDs may no longer be valid, so we need to rebuild the
  // entire map of MediaTrack* to GUID.
  track_guid_to_id_.clear();
  for (int i = 0; i < track_count; ++i) {
    MediaTrack* track_id = GetTrack(nullptr, i);
    if (track_id == nullptr) {
      LOG(ERROR) << "GetTrack returned null for index " << i;
    } else {
      track_guid_to_id_[GuidToKey(*GetTrackGUID(track_id))] = track_id;
    }
  }
  MediaTrack* master_track_id = GetMasterTrack(nullptr);
  const GUID& master_guid = *GetTrackGUID(master_track_id);
  track_guid_to_id_[GuidToKey(master_guid)] = master_track_id;

  // Update the master track state, if needed
  const GUID& old_master_guid = master_track_view_.GetGuid();
  if (master_guid != old_master_guid ||
      master_track_view_.state->track_id != master_track_id) {
    LOG(INFO) << "Master track changed, resetting";
    RebuildTrackView(master_track_view_, master_guid, master_track_id, -1);
  }

  // Refresh all the track list views.
  RefreshTrackListView(track_list_view_);
}

int ControlSurface::GetChildTrackIndex(MediaTrack* child_track_id) {
  MediaTrack* parent_track_id = GetParentTrack(child_track_id);
  for (ChildTrackIterator child(parent_track_id); !child.AtEnd();
       child.Next()) {
    if (child.GetTrackId() == child_track_id) {
      return child.GetChildIndex();
    }
  }
  // It will only get here if the child track ID does not exist in the track
  // list at all, which would be a coding error.
  LOG(ERROR) << "Failed to find child track index for track_id="
             << child_track_id;
  return 0;
}

void ControlSurface::EnsureTrackIsInView(TrackListView& track_list_view,
                                         MediaTrack* track_id) {
  // First see if the track is already in view, and if so, nothing to do.
  if (GetTrackState(track_id) != nullptr) {
    return;
  }

  // First make sure the view parent is the track's parent.
  MediaTrack* parent_track_id = GetParentTrack(track_id);
  const GUID& parent_track_guid =
      parent_track_id != nullptr ? *GetTrackGUID(parent_track_id) : kEmptyGuid;
  const GUID& view_parent_guid = track_list_view.parent_track.has_value()
                                     ? track_list_view.parent_track.value()
                                     : kEmptyGuid;
  int view_count = track_list_view.track_views.size();
  int track_index = GetChildTrackIndex(track_id);
  if (parent_track_guid != view_parent_guid) {
    // The track's parent is different from the view's parent, so update the
    // parent and set the selected track as far to the right as possible.
    if (parent_track_id == nullptr) {
      track_list_view.parent_track = std::nullopt;
    } else {
      track_list_view.parent_track = parent_track_guid;
    }
    track_list_view.index = std::max(0, track_index - view_count + 1);
    RefreshTrackListView(track_list_view);
    return;
  }

  // The track's parent is the same as the view's parent, so just need to make
  // sure the track is within the view index range, and if not, update the index
  // to show the track.
  if (track_index < track_list_view.index) {
    track_list_view.index = track_index;
    RefreshTrackListView(track_list_view);
  } else if (track_index >= track_list_view.index + view_count) {
    track_list_view.index = track_index - view_count + 1;
    RefreshTrackListView(track_list_view);
  }
}

void ControlSurface::OnTrackViewSelectPressed(TrackView& track_view) {
  if (!track_view.state.has_value()) {
    return;
  }
  auto& state = *track_view.state;
  if (state.flags.IsSet(TrackFlag::kSelected)) {
    state.flags.Clear(TrackFlag::kSelected);
  } else {
    state.flags.Set(TrackFlag::kSelected);
  }
  const bool selected = state.flags.IsSet(TrackFlag::kSelected);
  if (track_view.select_output) {
    track_view.select_output->SetValue(selected ? 1 : 0);
  }
  SetTrackSelected(state.track_id, selected);
  LOG(INFO) << "OnTrackViewSelectPressed: track \"" << state.name << "\" ("
            << state.track_id << ") -> " << (selected ? "true" : "false");
}

}  // namespace jpr