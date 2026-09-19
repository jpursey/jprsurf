// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/control_surface.h"

#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "gb/config/text_config.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/track_cache.h"
#include "jpr/common/undo.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/reaper_property.h"
#include "jpr/scene/timeline_property.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

const GUID kEmptyGuid = {};
constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

// The name and X-Touch button for each SurfaceMode, indexed by the mode.
struct ModeInfo {
  std::string_view name;
  std::string_view button;
};
constexpr ModeInfo kModeInfo[kSurfaceModeCount] = {
    {"track", DeviceXTouch::kAssignTrack},
    {"send_receive", DeviceXTouch::kAssignSend},
};

// The X-Touch strip that shows the Send/Receive mode track itself.
constexpr int kInfoStrip = 7;

// The modifier property that is on while the Send/Receive mode button is held.
constexpr std::string_view kSendHold = "mod_send_hold";

// The modifier property that is on while a select button is held as the anchor
// for a range of tracks.
constexpr std::string_view kSelectAnchor = "mod_select_anchor";

// The Send/Receive mode button only acts when released if it was pressed for
// less than this. Holding it longer only shows which tracks have routes. This
// matches the long press duration of controls.
constexpr absl::Duration kSendHoldDuration = absl::Milliseconds(350);

// Returns true if Send/Receive mode can show this track: it exists, is on the
// surface, and has sends or receives.
bool CanShowRoutes(const Track* track) {
  return track != nullptr && track->Exists() &&
         track->IsVisible(TrackCache::Get().GetSurfaceFilter()) &&
         (!track->GetSends().empty() || !track->GetReceives().empty());
}

// Adds the mappings for the channel strip controls that show a track the same
// way in every mode: mute, solo, record arm, pan, volume, name, color, and
// meter. The select button and the bottom scribble line are left to the caller,
// as their meaning depends on the mode.
void AddTrackStripMappings(View* view, std::string_view device_prefix,
                           int strip) {
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiMute,
                   absl::StrCat(device_prefix, DeviceXTouch::Mute(strip)));
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiSolo,
                   absl::StrCat(device_prefix, DeviceXTouch::Solo(strip)));
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiRecArm,
                   absl::StrCat(device_prefix, DeviceXTouch::Rec(strip)));
  view->AddMapping(
      ViewMapping::kReadWriteControl, TrackProperties::kUiPan,
      absl::StrCat(device_prefix, DeviceXTouch::Pot(strip)),
      {.write = {
           .mode = 1,
           .mode_overrides = {
               {std::string(TrackProperties::kTrackExists), {{false, 8}}},
               {std::string(TrackProperties::kTrackIsFolder), {{true, 5}}}}}});
  view->AddMapping(ViewMapping::kReadControl, TrackProperties::kUiPan,
                   absl::StrCat(device_prefix, DeviceXTouch::PotButton(strip)),
                   {.read = {.property_min = 0.0, .property_max = 0.0}});
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiVolume,
                   absl::StrCat(device_prefix, DeviceXTouch::Fader(strip)));
  view->AddMapping(
      ViewMapping::kWriteControl, TrackProperties::kName,
      absl::StrCat(device_prefix, DeviceXTouch::Scribble(strip, 0)));
  view->AddMapping(
      ViewMapping::kWriteControl, TrackProperties::kColor,
      absl::StrCat(device_prefix, DeviceXTouch::ScribbleColor(strip)));
  view->AddMapping(ViewMapping::kWriteControl, TrackProperties::kMeter,
                   absl::StrCat(device_prefix, DeviceXTouch::Meter(strip)));
}

// Adds a property (anchor_<action>_<strip_index>) that anchors the view's track
// for a ranged track action while the control is held, and maps it to the
// control. Pressing the same control on another strip then acts on the range
// from the anchor (see Track). The anchor is released when the control is
// released, or when the view releases it (see View::SetAnchor()). The modifier,
// if any, is on while the anchor is held.
void AddTrackAnchorMapping(Scene* scene, View* view, int strip_index,
                           TrackAnchor type, std::string_view action,
                           std::string_view control,
                           InputConfig::PressBehavior press_behavior =
                               InputConfig::PressBehavior::kNormal,
                           Modifiers modifier = 0) {
  const std::string name = absl::StrCat("anchor_", action, "_", strip_index);
  scene->AddProperty(std::make_unique<CallbackToggleProperty>(
      name, [view, type, modifier](bool pressed) {
        Anchor<Track>& anchor = TrackCache::Get().GetAnchor(type);
        if (!pressed) {
          view->ReleaseAnchor(&anchor);
          return;
        }
        // An empty strip has no place in a range.
        Track* track = view->GetTrack();
        if (!track->Exists()) {
          return;
        }
        view->SetAnchor(anchor.Hold(track, modifier));
      }));
  view->AddMapping(
      ViewMapping::kReadControl, name, control,
      {.read = {.press_behavior = press_behavior, .press_release = true}});
}

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

ControlSurface::~ControlSurface() {
  // Clear the surface, so it doesn't keep showing the last state after REAPER
  // exits or the surface is removed. Deactivating the scene releases every
  // mapping's output writer, which clears each control on its next run. There
  // are no more calls to Run(), so run the devices to clear their controls, and
  // then MIDI output to send it.
  if (scene_ != nullptr) {
    scene_->Deactivate();
    device_runner_.Run();
    midi_out_runner_.Run();

    // The MIDI ports are destroyed right after this, and MIDI still being sent
    // when a port is destroyed is lost (the X-Touch Extender, whose port is
    // destroyed first, did not clear without this). REAPER has no way to flush
    // a port, so give them time to finish sending.
    absl::SleepFor(absl::Milliseconds(100));
  }
  LOG(INFO) << "ControlSurface destroyed";
}

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

// How often track visibility is polled. REAPER gives control surfaces no
// notification when a track is shown or hidden, so the only way to see it is to
// query every track in the project, which is far too much to do on every run.
// Hiding a track is a deliberate, infrequent action taken in the Track Manager,
// so a delay of up to this long before the surface follows is not noticeable.
constexpr absl::Duration kVisibilityInterval = absl::Seconds(1);

void ControlSurface::Run() {
  absl::Time start = absl::Now();

  if (track_list_changed_) {
    TrackCache::Get().Refresh();
    RefreshTrackViews();
    track_list_changed_ = false;
    // Refresh() re-read visibility for every track, so the poll can wait.
    last_visibility_time_ = start;
    // The refresh is the first work done this run, so this is its duration.
    LOG(INFO) << "Refreshed TrackCache with "
              << TrackCache::Get().GetTrackCount() << " tracks in "
              << absl::ToInt64Microseconds(absl::Now() - start) << "us";
    mode_buttons_changed_ = true;

    // If the track shown in Send/Receive mode was deleted, there is nothing
    // left to show.
    if (mode_ == SurfaceMode::kSendReceive &&
        !send_receive_mode_view_->GetTrack()->Exists()) {
      LOG(INFO) << "Send/Receive track was deleted";
      EnterTrackMode();
    }
  } else if (last_visibility_time_ + kVisibilityInterval < start) {
    last_visibility_time_ = start;
    if (TrackCache::Get().RefreshVisibility()) {
      RefreshTrackViews();
      mode_buttons_changed_ = true;
    }
  }
  if (mode_buttons_changed_) {
    UpdateModeButtons();
  }

  device_runner_.Run();
  midi_in_runner_.Run();
  scene_runner_.Run();

  // Mode button presses are only recorded while the scene runs, so a requested
  // mode change is applied once it has finished.
  ApplyRequestedMode();

  // The Send/Receive mode button acts when it is released, which is also only
  // recorded while the scene runs. This is after any mode change, so a track
  // picked while it was held is already applied.
  ApplySendRelease(start);

  // Create undo points for continuous changes made this run, or earlier, once
  // they have stopped.
  ContinuousUndo::Get().Update(start);

  midi_out_runner_.Run();

  absl::Time end = absl::Now();
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

  // Mode availability depends on which track is selected.
  mode_buttons_changed_ = true;
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
  if (track_list_view_ == nullptr) {
    return;
  }
  Track* track = TrackCache::Get().GetTrack(track_id);
  if (mode_ == SurfaceMode::kSendReceive) {
    // Follow the touched track, unless it is already shown, or it couldn't be
    // shown by entering Send/Receive mode from Track mode: it doesn't exist, is
    // the master track, or isn't on the surface.
    if (track != nullptr && track != send_receive_mode_view_->GetTrack() &&
        track->Exists() && track != TrackCache::Get().GetMasterTrack() &&
        track->IsVisible(TrackCache::Get().GetSurfaceFilter())) {
      SetSendReceiveTrack(track);
    }
  } else {
    EnsureTrackIsVisible(track);
  }
  TrackCache::Get().SetLastTouchedTrack(track);
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

    // Timecode display
    root_view->AddMapping(ViewMapping::kWriteControl, kTimelinePosition,
                          absl::StrCat("XTouch/", DeviceXTouch::kTimecode));
    root_view->AddMapping(ViewMapping::kWriteControl, kRulerFrames,
                          absl::StrCat("XTouch/", DeviceXTouch::kSmpteLed));
    root_view->AddMapping(ViewMapping::kWriteControl, kRulerBeats,
                          absl::StrCat("XTouch/", DeviceXTouch::kBeatsLed));
    root_view->AddMapping(
        ViewMapping::kReadControl, kRulerMode,
        absl::StrCat("XTouch/", DeviceXTouch::kShowTimeBeats));
    root_view->AddMapping(ViewMapping::kWriteControl, kStateAnyTrackSolo,
                          absl::StrCat("XTouch/", DeviceXTouch::kSoloLed));

    // Utility buttons
    const std::string undo = absl::StrCat("XTouch/", DeviceXTouch::kUndo);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdUndo, undo);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdRedo, undo,
                          {.read = {.required_modifiers = kModShift}});
    root_view->AddMapping(ViewMapping::kWriteControl, kStateCanRedo, undo);
    const std::string save = absl::StrCat("XTouch/", DeviceXTouch::kSave);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdSaveProject, save);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdSaveNewProjectVersion,
                          save, {.read = {.required_modifiers = kModShift}});
    root_view->AddMapping(ViewMapping::kWriteControl, kStateProjectDirty, save,
                          {.write = {.mode = 1}});  // Blinking
    const std::string cancel = absl::StrCat("XTouch/", DeviceXTouch::kCancel);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdUnselectAllItems,
                          cancel);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdRemoveTimeSelection,
                          cancel, {.read = {.required_modifiers = kModShift}});
    const std::string enter = absl::StrCat("XTouch/", DeviceXTouch::kEnter);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertMidiItem, enter);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertEmptyItem, enter,
                          {.read = {.required_modifiers = kModShift}});

    // Misc buttons (above transport)
    root_view->AddMapping(ViewMapping::kReadWriteControl, kModMarker,
                          absl::StrCat("XTouch/", DeviceXTouch::kMarker));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kModNudge,
                          absl::StrCat("XTouch/", DeviceXTouch::kNudge));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRepeat,
                          absl::StrCat("XTouch/", DeviceXTouch::kCycle));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdMetronome,
                          absl::StrCat("XTouch/", DeviceXTouch::kClick));
    const std::string solo = absl::StrCat("XTouch/", DeviceXTouch::kSolo);
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdSoloInFront,
                          solo);
    root_view->AddMapping(
        ViewMapping::kReadControl, kCmdSoloDefeat, solo,
        {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});

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
  InitModeButtons(has_xtouch);
  root_view->Enable();

  // Add the Track mode view, which holds everything that is specific to Track
  // mode.
  track_mode_view_ = root_view->AddChildView("TrackMode");

  // On while a track's select button is held as the anchor for a range.
  const Modifiers select_anchor_modifier =
      scene_->AddModifierProperty(kSelectAnchor);
  CHECK(select_anchor_modifier != 0);

  // Add TrackList view with 8 track views, which will correspond to the 8
  // tracks on the X-Touch.
  track_list_view_ = track_mode_view_->AddChildView("TrackList");
  int child_view_index = 0;
  for (int d = 0; d < 2; ++d) {
    if ((d == 0 && !has_xtouch_ext) || (d == 1 && !has_xtouch)) {
      continue;
    }
    std::string device_prefix = (d == 0) ? "XTouchExt/" : "XTouch/";
    for (int i = 0; i < 8; ++i) {
      View* track_view = track_list_view_->AddChildView(
          absl::StrCat("Track", ++child_view_index));
      // Select selects the track, and double press navigates into it. While
      // Send is held, pressing select instead picks the track for Send/Receive
      // mode. This uses required modifiers rather than a condition, so holding
      // Send never resets a pending press.
      const std::string select =
          absl::StrCat(device_prefix, DeviceXTouch::Select(i));
      track_view->AddMapping(ViewMapping::kReadControl,
                             TrackProperties::kUiSelected, select);
      track_view->AddMapping(
          ViewMapping::kReadControl, View::kParentTrackChild, select,
          {.read = {.press_behavior =
                        InputConfig::PressBehavior::kDoublePress}});

      // Holding select (a long press) selects the track and anchors it, so
      // pressing another track's select selects the range between them. The
      // select anchor turns on a modifier, which puts the other select
      // buttons in a group with no double press, so the range is selected as
      // soon as they are pressed.
      track_view->AddMapping(
          ViewMapping::kReadControl, TrackProperties::kUiSelected, select,
          {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});
      AddTrackAnchorMapping(scene_.get(), track_view, child_view_index,
                            TrackAnchor::kSelect, "select", select,
                            InputConfig::PressBehavior::kLongPress,
                            select_anchor_modifier);
      track_view->AddMapping(
          ViewMapping::kReadControl, TrackProperties::kUiSelected, select,
          {.read = {.required_modifiers = select_anchor_modifier}});
      const std::string pick_name =
          absl::StrCat("pick_send_receive_track_", child_view_index);
      scene_->AddProperty(std::make_unique<CallbackActionProperty>(
          pick_name, [this, track_view] {
            requested_mode_ = SurfaceMode::kSendReceive;
            requested_send_receive_track_ = track_view->GetTrack();
            // Send was held to pick a track, so releasing it does nothing, even
            // if the picked track has no routes.
            send_press_mode_.reset();
          }));
      track_view->AddMapping(
          ViewMapping::kReadControl, pick_name, select,
          {.read = {.required_modifiers = send_hold_modifier_}});

      // The select light shows whether the track is selected, or while Send is
      // held, whether it has routes to show in Send/Receive mode.
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kUiSelected, select,
          {.condition = ViewMapping::Condition{
               .property = std::string(kSendHold), .value = false}});
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kTrackHasRoutes, select,
          {.condition = ViewMapping::Condition{
               .property = std::string(kSendHold), .value = true}});
      AddTrackStripMappings(track_view, device_prefix, i);

      // Holding mute, solo, or record arm anchors the track, so pressing the
      // same button on another track sets the range between them to the held
      // track's value.
      AddTrackAnchorMapping(scene_.get(), track_view, child_view_index,
                            TrackAnchor::kMute, "mute",
                            absl::StrCat(device_prefix, DeviceXTouch::Mute(i)));
      AddTrackAnchorMapping(scene_.get(), track_view, child_view_index,
                            TrackAnchor::kSolo, "solo",
                            absl::StrCat(device_prefix, DeviceXTouch::Solo(i)));
      AddTrackAnchorMapping(scene_.get(), track_view, child_view_index,
                            TrackAnchor::kRecArm, "rec_arm",
                            absl::StrCat(device_prefix, DeviceXTouch::Rec(i)));
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kUiVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 1)));
      track_view->Enable();
    }
  }
  track_list_view_->SetChildContext(View::ChildContextType::kTrack);
  if (has_xtouch) {
    // Global navigates up one level, or all the way to the root when held. It
    // is lit while there is a level to go up to.
    const std::string global = absl::StrCat("XTouch/", DeviceXTouch::kGlobal);
    track_list_view_->AddMapping(ViewMapping::kReadControl, View::kTrackParent,
                                 global);
    track_list_view_->AddMapping(
        ViewMapping::kReadControl, View::kTrackRoot, global,
        {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});
    track_list_view_->AddMapping(ViewMapping::kWriteControl,
                                 TrackProperties::kTrackHasParent, global);
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
  track_mode_view_->Enable();

  // Add the Send/Receive mode view, which starts disabled as the surface starts
  // in Track mode.
  send_receive_mode_view_ = root_view->AddChildView("SendReceiveMode");

  // Add a route view for each channel strip, which will show consecutive sends
  // or receives of the Send/Receive mode track. Each route view's track is the
  // track at the other end of its route.
  int route_view_index = 0;
  for (int d = 0; d < 2; ++d) {
    if ((d == 0 && !has_xtouch_ext) || (d == 1 && !has_xtouch)) {
      continue;
    }
    std::string device_prefix = (d == 0) ? "XTouchExt/" : "XTouch/";
    for (int i = 0; i < 8; ++i) {
      // The Info strip on the X-Touch (d == 1) shows the track, not a route.
      if (d == 1 && i == kInfoStrip) {
        continue;
      }
      View* route_view = send_receive_mode_view_->AddChildView(
          absl::StrCat("Route", ++route_view_index));
      // Select navigates across the route to the track at its other end.
      route_view->AddMapping(
          ViewMapping::kReadControl, View::kParentRouteOtherTrack,
          absl::StrCat(device_prefix, DeviceXTouch::Select(i)));
      route_view->AddMapping(
          ViewMapping::kReadWriteControl, RouteProperties::kMute,
          absl::StrCat(device_prefix, DeviceXTouch::Mute(i)));
      route_view->AddMapping(
          ViewMapping::kReadWriteControl, RouteProperties::kPan,
          absl::StrCat(device_prefix, DeviceXTouch::Pot(i)),
          {.write = {.mode = 1,
                     .mode_overrides = {{std::string(RouteProperties::kExists),
                                         {{false, 8}}}}}});
      route_view->AddMapping(
          ViewMapping::kReadControl, RouteProperties::kPan,
          absl::StrCat(device_prefix, DeviceXTouch::PotButton(i)),
          {.read = {.property_min = 0.0, .property_max = 0.0}});
      route_view->AddMapping(
          ViewMapping::kReadWriteControl, RouteProperties::kVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Fader(i)));
      route_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kName,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 0)));
      route_view->AddMapping(
          ViewMapping::kWriteControl, RouteProperties::kVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 1)));
      route_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kColor,
          absl::StrCat(device_prefix, DeviceXTouch::ScribbleColor(i)));
      route_view->Enable();
    }
  }
  if (has_xtouch) {
    send_receive_mode_view_->AddMapping(
        ViewMapping::kReadControl, View::kChildDec,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelLeft));
    send_receive_mode_view_->AddMapping(
        ViewMapping::kReadControl, View::kChildInc,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelRight));
    send_receive_mode_view_->AddMapping(
        ViewMapping::kReadControl, View::kBankDec,
        absl::StrCat("XTouch/", DeviceXTouch::kBankLeft));
    send_receive_mode_view_->AddMapping(
        ViewMapping::kReadControl, View::kBankInc,
        absl::StrCat("XTouch/", DeviceXTouch::kBankRight));

    // The Info strip shows the Send/Receive mode track itself, the same as in
    // Track mode, except the bottom scribble line shows whether its sends or
    // receives are shown.
    AddTrackStripMappings(send_receive_mode_view_, "XTouch/", kInfoStrip);
    send_receive_mode_view_->AddMapping(
        ViewMapping::kWriteControl, View::kChildRouteTypeName,
        absl::StrCat("XTouch/", DeviceXTouch::Scribble(kInfoStrip, 1)));
  }
  // Bank left/right pages through all the route strips at once.
  send_receive_mode_view_->SetBankSize(
      send_receive_mode_view_->GetChildViewCount());

  // Finally activate the scene, which will start it running and activate all
  // enabled views.
  scene_->Activate(scene_runner_);
}

//------------------------------------------------------------------------------
// Surface modes
//------------------------------------------------------------------------------

void ControlSurface::InitModeButtons(bool has_xtouch) {
  View* root_view = scene_->GetRootView();

  // On while the Send/Receive mode button is held. This is added even without
  // an X-Touch, as Track mode mappings refer to it.
  send_hold_modifier_ = scene_->AddModifierProperty(kSendHold);
  CHECK(send_hold_modifier_ != 0);

  for (int i = 0; i < kSurfaceModeCount; ++i) {
    const SurfaceMode mode = static_cast<SurfaceMode>(i);
    const ModeInfo& info = kModeInfo[i];
    const std::string available_name =
        absl::StrCat("mode_", info.name, "_available");
    const std::string active_name = absl::StrCat("mode_", info.name, "_active");
    const std::string select_name = absl::StrCat("mode_", info.name, "_select");

    ModeButton& button = mode_buttons_[i];
    button.available = scene_->AddProperty(
        std::make_unique<ToggleValueProperty>(available_name));
    button.active =
        scene_->AddProperty(std::make_unique<ToggleValueProperty>(active_name));
    CHECK(button.available != nullptr && button.active != nullptr);
    scene_->AddProperty(
        std::make_unique<CallbackActionProperty>(select_name, [this, mode] {
          // The Send/Receive mode button acts when it is released, so it can be
          // held to pick a track instead (see ApplySendRelease()).
          if (mode == SurfaceMode::kSendReceive) {
            send_press_mode_ = mode_;
            send_press_time_ = absl::Now();
          } else {
            requested_mode_ = mode;
          }
        }));

    if (!has_xtouch) {
      continue;
    }
    const std::string control = absl::StrCat("XTouch/", info.button);
    root_view->AddMapping(
        ViewMapping::kWriteControl, available_name, control,
        {.write = {.mode_overrides = {{active_name, {{true, 1}}}}}});
    root_view->AddMapping(ViewMapping::kReadControl, select_name, control);
    if (mode == SurfaceMode::kSendReceive) {
      root_view->AddMapping(ViewMapping::kReadControl, kSendHold, control,
                            {.read = {.press_release = true}});
    }
  }
}

bool ControlSurface::IsModeAvailable(SurfaceMode mode) const {
  switch (mode) {
    case SurfaceMode::kTrack:
      return true;
    case SurfaceMode::kSendReceive:
      return CanShowRoutes(TrackCache::Get().GetOnlySelectedTrack());
  }
  return false;
}

void ControlSurface::UpdateModeButtons() {
  mode_buttons_changed_ = false;
  for (int i = 0; i < kSurfaceModeCount; ++i) {
    const SurfaceMode mode = static_cast<SurfaceMode>(i);
    const bool active = (mode == mode_);
    // The current mode stays lit even if it is no longer available, so it is
    // always clear which mode the surface is in.
    mode_buttons_[i].available->SetBool(active || IsModeAvailable(mode));
    mode_buttons_[i].active->SetBool(active);
  }
}

void ControlSurface::ApplyRequestedMode() {
  if (!requested_mode_.has_value()) {
    return;
  }
  const SurfaceMode mode = *requested_mode_;
  requested_mode_.reset();
  Track* picked_track = std::exchange(requested_send_receive_track_, nullptr);

  // Requesting the current mode does nothing.
  if (mode == mode_) {
    return;
  }
  switch (mode) {
    case SurfaceMode::kTrack:
      EnterTrackMode();
      break;
    case SurfaceMode::kSendReceive:
      // A track picked by holding Send and pressing its select button, or
      // otherwise the selected track.
      TryEnterSendReceiveMode(picked_track != nullptr
                                  ? picked_track
                                  : TrackCache::Get().GetOnlySelectedTrack());
      break;
  }
}

void ControlSurface::TryEnterSendReceiveMode(Track* track) {
  // This is checked here rather than using the button light, as the selection
  // may have changed since the light was last updated.
  if (CanShowRoutes(track)) {
    EnterSendReceiveMode(track);
  }
}

void ControlSurface::ApplySendRelease(absl::Time now) {
  // A press shorter than a frame never turns on the hold modifier, but the
  // press is still recorded, so it is handled as released too.
  if (!send_press_mode_.has_value() || AreModifiersOn(send_hold_modifier_)) {
    return;
  }
  const SurfaceMode press_mode = *send_press_mode_;
  send_press_mode_.reset();

  // Releasing Send does nothing if it was held rather than pressed (to see
  // which tracks have routes), or if the mode changed while it was held.
  if (now - send_press_time_ >= kSendHoldDuration || press_mode != mode_) {
    return;
  }
  switch (mode_) {
    case SurfaceMode::kTrack:
      TryEnterSendReceiveMode(TrackCache::Get().GetOnlySelectedTrack());
      break;
    case SurfaceMode::kSendReceive:
      send_receive_mode_view_->ToggleChildRouteType();
      break;
  }
}

void ControlSurface::EnterTrackMode() {
  const absl::Time start = absl::Now();
  const SurfaceMode old_mode = mode_;
  Track* track =
      (mode_ == SurfaceMode::kSendReceive ? send_receive_mode_view_->GetTrack()
                                          : nullptr);
  mode_ = SurfaceMode::kTrack;
  send_receive_mode_view_->Disable();
  send_receive_mode_view_->SetTrack(nullptr);
  track_mode_view_->Enable();

  // Show the track that was shown in Send/Receive mode among its siblings. This
  // does nothing if it was deleted or is hidden, leaving the track list where
  // it was.
  if (track != nullptr) {
    EnsureTrackIsVisible(track);
  }
  FinishModeChange(old_mode, start);
}

void ControlSurface::EnterSendReceiveMode(Track* track) {
  DCHECK(track != nullptr);
  const absl::Time start = absl::Now();
  const SurfaceMode old_mode = mode_;
  mode_ = SurfaceMode::kSendReceive;
  SetSendReceiveTrack(track);
  track_mode_view_->Disable();
  send_receive_mode_view_->Enable();
  FinishModeChange(old_mode, start);
}

void ControlSurface::SetSendReceiveTrack(Track* track) {
  const bool show_receives =
      track->GetSends().empty() && !track->GetReceives().empty();
  send_receive_mode_view_->SetChildContext(
      show_receives ? View::ChildContextType::kReceives
                    : View::ChildContextType::kSends);
  send_receive_mode_view_->SetTrack(track);
}

void ControlSurface::FinishModeChange(SurfaceMode old_mode, absl::Time start) {
  UpdateModeButtons();
  LOG(INFO) << "Surface mode changed from "
            << kModeInfo[static_cast<int>(old_mode)].name << " to "
            << kModeInfo[static_cast<int>(mode_)].name << " in "
            << absl::ToInt64Microseconds(absl::Now() - start) << "us";
}

void ControlSurface::RefreshTrackViews() {
  Track* master_track = TrackCache::Get().GetMasterTrack();
  if (master_track_view_ != nullptr) {
    master_track_view_->SetTrack(master_track);
  }

  // The track list view is parented to a track whose children fill the strips.
  // If that track was deleted there is nothing left to show, so return to the
  // master track. A track that is merely hidden on the surface is deliberately
  // left in place: its strips go blank, but they come back as soon as it is
  // shown again, and the user can navigate up explicitly if they want to.
  if (!track_list_view_->GetTrack()->Exists()) {
    track_list_view_->SetTrack(master_track, 0);
  }

  // Hiding tracks can shorten the child list out from under the current bank,
  // which would otherwise leave the strips blank.
  track_list_view_->SetChildContextIndex(
      std::clamp(track_list_view_->GetChildContextIndex(), 0,
                 track_list_view_->GetMaxChildContextIndex()));
  track_list_view_->RefreshChildContext();

  // Routes are only added or removed when the track list changes, which can
  // also shorten the route list out from under the current bank.
  send_receive_mode_view_->SetChildContextIndex(
      std::clamp(send_receive_mode_view_->GetChildContextIndex(), 0,
                 send_receive_mode_view_->GetMaxChildContextIndex()));
  send_receive_mode_view_->RefreshChildContext();
}

void ControlSurface::EnsureTrackIsVisible(Track* track) {
  // This can happen if we get events for tracks before the TrachCache has been
  // refreshed which happens only when Run() is called.
  if (track == nullptr) {
    return;
  }

  // The track may be hidden in the mixer while still selectable in the arrange
  // view, in which case it has no strip to scroll to, so leave the bank put.
  // This also covers the master and stub tracks, which have no parent track and
  // so can never be made visible this way.
  const TrackFilter filter = TrackCache::Get().GetSurfaceFilter();
  const std::optional<int> track_index = track->GetIndex(filter);
  if (!track_index.has_value()) {
    return;
  }
  Track* parent_track = track->GetParentTrack();

  const int num_tracks_in_view = track_list_view_->GetChildViewCount();
  const int last_child_context_index =
      std::max(0, *track_index - num_tracks_in_view + 1);

  // If the track is already in the current view, we only need to make sure it
  // is in view, or do a minimum scroll to get it in view.
  if (track_list_view_->GetTrack() == parent_track) {
    int first_index = track_list_view_->GetChildContextIndex();
    if (*track_index < first_index) {
      track_list_view_->SetChildContextIndex(*track_index);
    } else if (*track_index >= first_index + num_tracks_in_view) {
      track_list_view_->SetChildContextIndex(last_child_context_index);
    }
  } else {
    // We are switching to a nested track.
    track_list_view_->SetTrack(parent_track, last_child_context_index);
  }
}

}  // namespace jpr