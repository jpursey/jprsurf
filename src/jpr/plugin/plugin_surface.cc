// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin_surface.h"

#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/config/text_config.h"
#include "jpr/common/log_file.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/prefixed_name.h"
#include "jpr/common/reaper_api.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/state_properties.h"
#include "jpr/scene/track_anchor_property.h"
#include "jpr/scene/track_pick_property.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

// The top level mode of the surface, which determines what the channel strips
// control. Each mode has a view that is active while it is the mode. Its values
// are the modes below, whose names are kSurfaceModeNames, in order.
constexpr std::string_view kSurfaceMode = kUserName<"surface_mode">;
constexpr int kTrackMode = 0;        // Strips show tracks in the hierarchy.
constexpr int kSendReceiveMode = 1;  // Strips show one track's routes.
constexpr std::string_view kSurfaceModeNames[] = {"track", "send_receive"};

// The X-Touch strip that shows the Send/Receive mode track itself.
constexpr int kInfoStrip = 7;

// The track whose child tracks the track list shows. It starts as, and returns
// to, the master track if its track is deleted. A folder that is only hidden is
// kept: its strips go blank, and come back as soon as it is shown again.
constexpr std::string_view kFolder = kUserName<"folder">;

// The track whose routes Send/Receive mode shows, and which the track list
// reveals. It follows the last touched track, and picks and route navigation
// also change it.
constexpr std::string_view kCurrentTrack = kUserName<"current_track">;

// The name and color of the track at the other end of a route strip's route.
constexpr std::string_view kOtherTrackName =
    kPrefixedName<RouteProperties::kOtherTrack, ".name">;
constexpr std::string_view kOtherTrackColor =
    kPrefixedName<RouteProperties::kOtherTrack, ".color">;

// Whether the selected track has routes, so Send/Receive mode can show it.
constexpr std::string_view kSelectedTrackHasRoutes =
    kPrefixedName<Scene::kSelectedTrack, ".has_routes">;

// The modifier property that is on while the Send/Receive mode button is held.
constexpr std::string_view kSendHold = kModName<"send_hold">;

// The modifier property that is on while a select button is held as the anchor
// for a range of tracks.
constexpr std::string_view kSelectAnchor = kModName<"select_anchor">;

// The anchors each track strip adds for its track, for ranges of tracks.
constexpr std::string_view kAnchorSelect = kUserName<"anchor_select">;
constexpr std::string_view kAnchorMute = kUserName<"anchor_mute">;
constexpr std::string_view kAnchorSolo = kUserName<"anchor_solo">;
constexpr std::string_view kAnchorRecArm = kUserName<"anchor_rec_arm">;

// Picks the current track for Send/Receive mode: the selected track, or a track
// strip's track, which each track strip adds.
constexpr std::string_view kPickSelectedTrack =
    kUserName<"pick_selected_track">;
constexpr std::string_view kPickTrack = kUserName<"pick_track">;

// What Rewind and Forward move by, whose values are the steps in
// kTransportSteps, in order.
constexpr std::string_view kTransportStep = kUserName<"transport_step">;

// A step Rewind and Forward can move by, and the button that picks it, if any.
struct TransportStep {
  std::string_view name;
  std::string_view button;
  std::string_view prev_command;
  std::string_view next_command;
};
constexpr TransportStep kTransportSteps[] = {
    {"measure", {}, kCmdGoPrevMeasure, kCmdGoNextMeasure},
    {"beat", DeviceXTouch::kNudge, kCmdGoPrevBeat, kCmdGoNextBeat},
    {"marker", DeviceXTouch::kMarker, kCmdGoPrevMarker, kCmdGoNextMarker},
};

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

// Adds a track anchor with the name to the view, which anchors the view's track
// for a ranged track action while the control is held, and maps it to the
// control. Pressing the same control on another strip then acts on the range
// from the anchor (see TrackAnchorProperty).
void AddTrackAnchorMapping(View* view, std::string_view name,
                           TrackAnchorProperty::Config config,
                           std::string_view control,
                           InputConfig::PressBehavior press_behavior =
                               InputConfig::PressBehavior::kNormal) {
  view->AddUserProperty(std::make_unique<TrackAnchorProperty>(name, config));
  view->AddMapping(
      ViewMapping::kReadControl, name, control,
      {.read = {.press_behavior = press_behavior, .press_release = true}});
}

}  // namespace

//------------------------------------------------------------------------------
// Registration
//------------------------------------------------------------------------------

bool PluginSurface::Register(reaper_plugin_info_t& plugin_info) {
  std::filesystem::path profile_path = GetLogPath("jprsurf_profile.txt");
  if (profile_path.empty()) {
    LOG(ERROR) << "There is nowhere to write the profile, so it is off.";
  }
  return ControlSurface::Register(plugin_info,
                                  {.type_string = kTypeString,
                                   .description = kDescString,
                                   .create_listener = PluginSurface::Create,
                                   .profile_path = std::move(profile_path)});
}

std::unique_ptr<ControlSurfaceListener> PluginSurface::Create(
    std::string_view config) {
  // The listener is a private base, so convert the pointer here, where it is
  // accessible.
  return std::unique_ptr<ControlSurfaceListener>(new PluginSurface(config));
}

PluginSurface::PluginSurface(std::string_view config) {
  if (!config.empty()) {
    auto parsed_config = gb::ReadConfigFromText(std::string(config));
    if (!parsed_config.ok()) {
      LOG(ERROR) << "Failed to parse control surface config: "
                 << parsed_config.status();
      LOG(ERROR) << config;
    }
  }
  LOG(INFO) << "PluginSurface created";

  InitViews();
}

PluginSurface::~PluginSurface() {
  // Clear the surface, so it doesn't keep showing the last state after REAPER
  // exits or the surface is removed. Deactivating the scene releases every
  // mapping's output writer, which clears each control on its next run. There
  // are no more calls to OnRun(), so run the devices to clear their controls.
  // Destroying the MIDI ports sends it.
  if (scene_ != nullptr) {
    scene_->Deactivate();
    device_runner_.Run(RunTime::Now());
  }
  LOG(INFO) << "PluginSurface destroyed";
}

//------------------------------------------------------------------------------
// ControlSurfaceListener overrides
//------------------------------------------------------------------------------

void PluginSurface::OnRun(const RunTime& time) {
  device_runner_.Run(time);
  midi_ports_.RunInput(time);
  scene_runner_.Run(time);
  midi_ports_.RunOutput(time);
}

std::string PluginSurface::GetConfig() const {
  return gb::WriteConfigToText(config_, gb::kCompactTextConfig);
}

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------

void PluginSurface::InitViews() {
  // Note: For now we are just hard-coding views to my development setup, which
  // is an X-Touch and X-Touch Extender, with the X-Touch extender to the left
  // of the X-Touch.
  MidiIn* xtouch_in = midi_ports_.OpenInput("X-Touch");
  MidiOut* xtouch_out = midi_ports_.OpenOutput("X-Touch");
  MidiIn* xtouch_ext_in = midi_ports_.OpenInput("X-Touch-Ext");
  MidiOut* xtouch_ext_out = midi_ports_.OpenOutput("X-Touch-Ext");
  scene_ = std::make_unique<Scene>("Scene");
  bool has_xtouch = (xtouch_in != nullptr && xtouch_out != nullptr);
  bool has_xtouch_ext = (xtouch_ext_in != nullptr && xtouch_ext_out != nullptr);
  if (has_xtouch) {
    scene_->AddDevice("XTouch", std::make_unique<DeviceXTouch>(
                                    DeviceXTouch::Type::kFull, device_runner_,
                                    xtouch_in, xtouch_out));
  }
  if (has_xtouch_ext) {
    scene_->AddDevice("XTouchExt",
                      std::make_unique<DeviceXTouch>(
                          DeviceXTouch::Type::kExtender, device_runner_,
                          xtouch_ext_in, xtouch_ext_out));
  }

  const TrackReference* folder = scene_->AddTrackReference(
      kFolder, {.fallback = std::string(Scene::kMasterTrack)});
  CHECK(folder != nullptr);
  TrackReference* current_track = scene_->AddTrackReference(
      kCurrentTrack, {.follow = std::string(Scene::kLastTouchedTrack)});
  CHECK(current_track != nullptr);

  // Always on, for lights that are lit whenever their mapping is active.
  const std::string lit(
      scene_->AddConstProperty(ViewProperty::Type::kToggle, true)->GetName());

  // Add global mappings
  auto* root_view = scene_->GetRootView();
  if (has_xtouch) {
    // Master fader
    View* master_track_view = root_view->AddChildView(
        "MasterFader", {.subject = View::ReferenceSubject{
                            .name = std::string(Scene::kMasterTrack)}});
    CHECK(master_track_view != nullptr);
    master_track_view->AddMapping(
        ViewMapping::kReadWriteControl, TrackProperties::kVolume,
        absl::StrCat("XTouch/", DeviceXTouch::kMasterFader));
    master_track_view->Enable();

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
    root_view->AddMapping(ViewMapping::kWriteControl, kStateTimelinePosition,
                          absl::StrCat("XTouch/", DeviceXTouch::kTimecode));
    root_view->AddMapping(ViewMapping::kWriteControl, kStateRulerFrames,
                          absl::StrCat("XTouch/", DeviceXTouch::kSmpteLed));
    root_view->AddMapping(ViewMapping::kWriteControl, kStateRulerBeats,
                          absl::StrCat("XTouch/", DeviceXTouch::kBeatsLed));
    root_view->AddMapping(
        ViewMapping::kReadControl, kStateRulerMode,
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
    root_view->AddMapping(ViewMapping::kWriteControl, kStateAnyItemSelected,
                          cancel);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdRemoveTimeSelection,
                          cancel, {.read = {.required_modifiers = kModShift}});
    const std::string enter = absl::StrCat("XTouch/", DeviceXTouch::kEnter);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertMidiItem, enter);
    root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertEmptyItem, enter,
                          {.read = {.required_modifiers = kModShift}});
    root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertClickSource,
                          enter, {.read = {.required_modifiers = kModCtrl}});

    // Automation buttons. Group turns the global automation override on and
    // off, and is lit while it is on.
    //
    // Without an override, each mode button sets the selected tracks' mode,
    // and is lit while any selected track is in its mode: solid if they all
    // are, and blinking if only some are.
    //
    // With an override, each mode button sets the override to its mode (or to
    // Bypass if it already is), and is lit while the override is its mode.
    // There is no Latch Preview button, so Latch blinks for Latch Preview, and
    // pressing it sets Latch.
    root_view->AddMapping(ViewMapping::kReadWriteControl,
                          kStateAutoOverrideActive,
                          absl::StrCat("XTouch/", DeviceXTouch::kAutoGroup));
    const ViewCondition::Config no_override = {
        .property = std::string(kStateAutoOverrideActive), .value = false};
    const ViewCondition::Config in_override = {
        .property = std::string(kStateAutoOverrideActive), .value = true};
    struct AutoModeButton {
      std::string_view button;
      std::string_view command;
      std::string_view selected_state;
      std::string_view override_state;

      // The light during an override, if it isn't override_state.
      std::string_view override_light = {};
    };
    static constexpr AutoModeButton kAutoModeButtons[] = {
        {DeviceXTouch::kAutoTrim, kCmdAutoModeTrim, kStateSelectedAutoTrimRead,
         kStateAutoOverrideTrimRead},
        {DeviceXTouch::kAutoRead, kCmdAutoModeRead, kStateSelectedAutoRead,
         kStateAutoOverrideRead},
        {DeviceXTouch::kAutoTouch, kCmdAutoModeTouch, kStateSelectedAutoTouch,
         kStateAutoOverrideTouch},
        {DeviceXTouch::kAutoWrite, kCmdAutoModeWrite, kStateSelectedAutoWrite,
         kStateAutoOverrideWrite},
        {DeviceXTouch::kAutoLatch, kCmdAutoModeLatch, kStateSelectedAutoLatch,
         kStateAutoOverrideLatch, kStateAutoOverrideAnyLatch},
    };
    for (const AutoModeButton& info : kAutoModeButtons) {
      const std::string control = absl::StrCat("XTouch/", info.button);
      root_view->AddMapping(ViewMapping::kReadControl, info.command, control,
                            {.condition = no_override});
      root_view->AddMapping(
          ViewMapping::kWriteControl, info.selected_state, control,
          {.write = {.mode_overrides = {{std::string(kStateSelectedAutoMixed),
                                         {{true, 1}}}}},
           .condition = no_override});
      root_view->AddMapping(ViewMapping::kReadControl, info.override_state,
                            control, {.condition = in_override});
      // The other override lights are never on during Latch Preview, so the
      // blink only affects Latch.
      root_view->AddMapping(
          ViewMapping::kWriteControl,
          info.override_light.empty() ? info.override_state
                                      : info.override_light,
          control,
          {.write = {.mode_overrides = {{std::string(
                                             kStateAutoOverrideLatchPreview),
                                         {{true, 1}}}}},
           .condition = in_override});
    }

    // Misc buttons (above transport). Marker and Nudge are with the transport
    // controls below.
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

    // Transport controls. Rewind and Forward move by the transport step, which
    // Marker and Nudge pick. It is one value, so turning one on turns the other
    // off. Each toggles between its step and the first (a measure), and is lit
    // while its step is picked.
    std::vector<std::string> step_names;
    for (const TransportStep& step : kTransportSteps) {
      step_names.emplace_back(step.name);
    }
    CHECK(scene_->AddUserProperty(std::make_unique<EnumeratedValueProperty>(
              kTransportStep, std::move(step_names))) != nullptr);
    const std::string rewind = absl::StrCat("XTouch/", DeviceXTouch::kRewind);
    const std::string forward = absl::StrCat("XTouch/", DeviceXTouch::kForward);
    for (int i = 0; i < static_cast<int>(std::size(kTransportSteps)); ++i) {
      const TransportStep& step = kTransportSteps[i];
      const ViewCondition::Config on_step = {
          .property = std::string(kTransportStep), .value = i};
      root_view->AddMapping(ViewMapping::kReadControl, step.prev_command,
                            rewind, {.condition = on_step});
      root_view->AddMapping(ViewMapping::kReadControl, step.next_command,
                            forward, {.condition = on_step});
      if (step.button.empty()) {
        continue;
      }
      const std::string button = absl::StrCat("XTouch/", step.button);
      root_view->AddMapping(
          ViewMapping::kReadControl, kTransportStep, button,
          {.read = {
               .property_min = 0, .property_max = i, .press_toggles = true}});
      root_view->AddMapping(ViewMapping::kWriteControl, lit, button,
                            {.condition = on_step});
    }
    root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportStop,
                          absl::StrCat("XTouch/", DeviceXTouch::kStop));
    root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportPlayPause,
                          absl::StrCat("XTouch/", DeviceXTouch::kPlay));
    root_view->AddMapping(ViewMapping::kWriteControl, kCmdTransportPlay,
                          absl::StrCat("XTouch/", DeviceXTouch::kPlay));
    root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRecord,
                          absl::StrCat("XTouch/", DeviceXTouch::kRecord));
  }

  // Surface modes. Track returns to Track mode, and each mode view maps what
  // tapping Send does. Entering Send/Receive mode is two mappings on the same
  // press, with the same condition: one picks the current track, and the other
  // sets the mode. The send hold modifier is added even without an X-Touch, as
  // Track mode mappings refer to it.
  CHECK(scene_->AddUserProperty(std::make_unique<EnumeratedValueProperty>(
            kSurfaceMode,
            std::vector<std::string>(std::begin(kSurfaceModeNames),
                                     std::end(kSurfaceModeNames)))) != nullptr);
  const Modifiers send_hold_modifier = scene_->AddModifierProperty(kSendHold);
  CHECK(send_hold_modifier != 0);
  CHECK(scene_->AddUserProperty(std::make_unique<TrackPickProperty>(
            kPickSelectedTrack, *current_track,
            scene_->GetTrackReference(Scene::kSelectedTrack))) != nullptr);
  const std::string track_button =
      absl::StrCat("XTouch/", DeviceXTouch::kAssignTrack);
  const std::string send_button =
      absl::StrCat("XTouch/", DeviceXTouch::kAssignSend);
  if (has_xtouch) {
    root_view->AddMapping(
        ViewMapping::kReadControl, kSurfaceMode, track_button,
        {.read = {.property_min = kTrackMode, .property_max = kTrackMode}});
    root_view->AddMapping(ViewMapping::kReadControl, kSendHold, send_button,
                          {.read = {.press_release = true}});
  }
  root_view->Enable();

  // Add the Track mode view, which holds everything that is specific to Track
  // mode.
  View* track_mode_view = root_view->AddChildView(
      "TrackMode",
      {.condition = ViewCondition::Config{.property = std::string(kSurfaceMode),
                                          .value = kTrackMode}});
  CHECK(track_mode_view != nullptr);

  // On while a track's select button is held as the anchor for a range.
  const Modifiers select_anchor_modifier =
      scene_->AddModifierProperty(kSelectAnchor);
  CHECK(select_anchor_modifier != 0);

  // Add TrackList view with a track view for each strip, which show the child
  // tracks of the folder. Bank left/right pages by 8, a device at a time. It
  // reveals the current track whenever it changes, and when returning to Track
  // mode.
  View* track_list_view = track_mode_view->AddChildView(
      "TrackList",
      {.subject = View::ReferenceSubject{.name = std::string(kFolder)},
       .list = View::ListConfig{
           .items = View::ChildTracks{.reveal = std::string(kCurrentTrack)},
           .bank_size = 8}});
  CHECK(track_list_view != nullptr);
  int child_view_index = 0;
  for (int d = 0; d < 2; ++d) {
    if ((d == 0 && !has_xtouch_ext) || (d == 1 && !has_xtouch)) {
      continue;
    }
    std::string device_prefix = (d == 0) ? "XTouchExt/" : "XTouch/";
    for (int i = 0; i < 8; ++i) {
      View* track_view = track_list_view->AddChildView(
          absl::StrCat("Track", ++child_view_index),
          {.subject = View::ListItemSubject{}});
      CHECK(track_view != nullptr);
      // Select selects the track, and double press navigates into it.
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
      AddTrackAnchorMapping(track_view, kAnchorSelect,
                            {.action = TrackBoolProperty::kSelected,
                             .modifier = select_anchor_modifier},
                            select, InputConfig::PressBehavior::kLongPress);
      track_view->AddMapping(
          ViewMapping::kReadControl, TrackProperties::kUiSelected, select,
          {.read = {.required_modifiers = select_anchor_modifier}});

      // While Send is held, pressing select instead enters Send/Receive mode
      // for the track, if it has routes, and otherwise does nothing. This uses
      // required modifiers rather than a condition, so holding Send never
      // resets a pending press. Required modifiers must match exactly, so this
      // also needs mappings for Send held along with a select anchor, where
      // picking wins over the range.
      track_view->AddUserProperty(
          std::make_unique<TrackPickProperty>(kPickTrack, *current_track));
      const ViewCondition::Config has_routes = {
          .property = std::string(TrackProperties::kTrackHasRoutes)};
      for (const Modifiers modifiers :
           {send_hold_modifier, send_hold_modifier | select_anchor_modifier}) {
        track_view->AddMapping(ViewMapping::kReadControl, kPickTrack, select,
                               {.read = {.required_modifiers = modifiers},
                                .condition = has_routes});
        track_view->AddMapping(ViewMapping::kReadControl, kSurfaceMode, select,
                               {.read = {.required_modifiers = modifiers,
                                         .property_min = kSendReceiveMode,
                                         .property_max = kSendReceiveMode},
                                .condition = has_routes});
      }

      // The select light shows whether the track is selected, or while Send is
      // held, whether it has routes to show in Send/Receive mode.
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kUiSelected, select,
          {.condition = ViewCondition::Config{
               .property = std::string(kSendHold), .value = false}});
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kTrackHasRoutes, select,
          {.condition = ViewCondition::Config{
               .property = std::string(kSendHold), .value = true}});
      AddTrackStripMappings(track_view, device_prefix, i);

      // Holding mute, solo, or record arm anchors the track, so pressing the
      // same button on another track sets the range between them to the held
      // track's value.
      AddTrackAnchorMapping(track_view, kAnchorMute,
                            {.action = TrackBoolProperty::kMute},
                            absl::StrCat(device_prefix, DeviceXTouch::Mute(i)));
      AddTrackAnchorMapping(track_view, kAnchorSolo,
                            {.action = TrackBoolProperty::kSolo},
                            absl::StrCat(device_prefix, DeviceXTouch::Solo(i)));
      AddTrackAnchorMapping(track_view, kAnchorRecArm,
                            {.action = TrackBoolProperty::kRecArm},
                            absl::StrCat(device_prefix, DeviceXTouch::Rec(i)));
      track_view->AddMapping(
          ViewMapping::kWriteControl, TrackProperties::kUiVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 1)));
      track_view->Enable();
    }
  }
  if (has_xtouch) {
    // Global navigates up one level, or all the way to the root when held. It
    // is lit while there is a level to go up to.
    const std::string global = absl::StrCat("XTouch/", DeviceXTouch::kGlobal);
    track_list_view->AddMapping(ViewMapping::kReadControl, View::kTrackParent,
                                global);
    track_list_view->AddMapping(
        ViewMapping::kReadControl, View::kTrackRoot, global,
        {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});
    track_list_view->AddMapping(ViewMapping::kWriteControl,
                                TrackProperties::kTrackHasParent, global);
    track_list_view->AddMapping(
        ViewMapping::kReadControl, View::kChildDec,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelLeft));
    track_list_view->AddMapping(
        ViewMapping::kReadControl, View::kChildInc,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelRight));
    track_list_view->AddMapping(
        ViewMapping::kReadControl, View::kBankDec,
        absl::StrCat("XTouch/", DeviceXTouch::kBankLeft));
    track_list_view->AddMapping(
        ViewMapping::kReadControl, View::kBankInc,
        absl::StrCat("XTouch/", DeviceXTouch::kBankRight));
  }
  track_list_view->Enable();
  if (has_xtouch) {
    // Tapping Send enters Send/Receive mode for the selected track, if it has
    // routes.
    const ViewCondition::Config has_routes = {
        .property = std::string(kSelectedTrackHasRoutes)};
    const InputConfig::PressBehavior tap = InputConfig::PressBehavior::kTap;
    track_mode_view->AddMapping(
        ViewMapping::kReadControl, kPickSelectedTrack, send_button,
        {.read = {.press_behavior = tap}, .condition = has_routes});
    track_mode_view->AddMapping(ViewMapping::kReadControl, kSurfaceMode,
                                send_button,
                                {.read = {.press_behavior = tap,
                                          .property_min = kSendReceiveMode,
                                          .property_max = kSendReceiveMode},
                                 .condition = has_routes});

    // Track blinks, as it is the current mode, and Send is lit while the
    // selected track has routes to show.
    track_mode_view->AddMapping(ViewMapping::kWriteControl, lit, track_button,
                                {.write = {.mode = 1}});
    track_mode_view->AddMapping(ViewMapping::kWriteControl,
                                kSelectedTrackHasRoutes, send_button);
  }
  track_mode_view->Enable();

  // Add the Send/Receive mode view, which shows the routes of the current
  // track: its sends, unless it only has receives. Bank left/right pages
  // through all the route strips at once. If the track is deleted, it shows
  // nothing until another track is touched.
  View* send_receive_mode_view = root_view->AddChildView(
      "SendReceiveMode",
      {.condition = ViewCondition::Config{.property = std::string(kSurfaceMode),
                                          .value = kSendReceiveMode},
       .subject = View::ReferenceSubject{.name = std::string(kCurrentTrack)},
       .list = View::ListConfig{.items = View::Routes{}}});
  CHECK(send_receive_mode_view != nullptr);

  // Add a route view for each channel strip, which will show consecutive sends
  // or receives of the Send/Receive mode track.
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
      View* route_view = send_receive_mode_view->AddChildView(
          absl::StrCat("Route", ++route_view_index),
          {.subject = View::ListItemSubject{}});
      CHECK(route_view != nullptr);
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
          ViewMapping::kWriteControl, kOtherTrackName,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 0)));
      route_view->AddMapping(
          ViewMapping::kWriteControl, RouteProperties::kVolume,
          absl::StrCat(device_prefix, DeviceXTouch::Scribble(i, 1)));
      route_view->AddMapping(
          ViewMapping::kWriteControl, kOtherTrackColor,
          absl::StrCat(device_prefix, DeviceXTouch::ScribbleColor(i)));
      route_view->Enable();
    }
  }
  if (has_xtouch) {
    send_receive_mode_view->AddMapping(
        ViewMapping::kReadControl, View::kChildDec,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelLeft));
    send_receive_mode_view->AddMapping(
        ViewMapping::kReadControl, View::kChildInc,
        absl::StrCat("XTouch/", DeviceXTouch::kChannelRight));
    send_receive_mode_view->AddMapping(
        ViewMapping::kReadControl, View::kBankDec,
        absl::StrCat("XTouch/", DeviceXTouch::kBankLeft));
    send_receive_mode_view->AddMapping(
        ViewMapping::kReadControl, View::kBankInc,
        absl::StrCat("XTouch/", DeviceXTouch::kBankRight));

    // The Info strip shows the Send/Receive mode track itself, the same as in
    // Track mode, except the bottom scribble line shows whether its sends or
    // receives are shown.
    AddTrackStripMappings(send_receive_mode_view, "XTouch/", kInfoStrip);
    send_receive_mode_view->AddMapping(
        ViewMapping::kWriteControl, View::kChildRouteTypeName,
        absl::StrCat("XTouch/", DeviceXTouch::Scribble(kInfoStrip, 1)));

    // Tapping Send switches between showing sends and receives.
    send_receive_mode_view->AddMapping(
        ViewMapping::kReadControl, View::kChildRouteToggle, send_button,
        {.read = {.press_behavior = InputConfig::PressBehavior::kTap}});

    // Send blinks, as it is the current mode, even if the track no longer has
    // routes, and Track is lit, as it is always available.
    send_receive_mode_view->AddMapping(ViewMapping::kWriteControl, lit,
                                       send_button, {.write = {.mode = 1}});
    send_receive_mode_view->AddMapping(ViewMapping::kWriteControl, lit,
                                       track_button);
  }
  send_receive_mode_view->Enable();

  // Finally activate the scene, which will start it running and activate all
  // enabled views.
  scene_->Activate(scene_runner_);
}

}  // namespace jpr