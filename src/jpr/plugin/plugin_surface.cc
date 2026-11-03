// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin_surface.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/config/text_config.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/prefixed_name.h"
#include "jpr/common/reaper_api.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/widget.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_path.h"
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
// are the modes below, in kSurfaceModeNames' order.
constexpr std::string_view kSurfaceMode = kUserName<"surface_mode">;

// Strips show tracks in the hierarchy.
constexpr std::string_view kTrackMode = "track";

// Strips show one track's routes.
constexpr std::string_view kSendReceiveMode = "send_receive";

constexpr std::string_view kSurfaceModeNames[] = {kTrackMode, kSendReceiveMode};

// The track whose child tracks the track list shows. It starts as, and returns
// to, the master track if its track is deleted. A folder that is only hidden is
// kept: REAPER hides its tracks with it, so its strips go blank, and come back
// as soon as it is shown again.
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
    {"beat", "nudge", kCmdGoPrevBeat, kCmdGoNextBeat},
    {"marker", "marker", kCmdGoPrevMarker, kCmdGoNextMarker},
};

// Returns JPRSurf's devices: an X-Touch, and an X-Touch Extender to its left,
// if there is one.
std::vector<DeviceSpec> GetDeviceSpecs() {
  return {
      {.name = "xtouch",
       .type = "XTouch",
       .input_port = "X-Touch",
       .output_port = "X-Touch"},
      {.name = "xtouch_ext",
       .type = "XTouchExt",
       .input_port = "X-Touch-Ext",
       .output_port = "X-Touch-Ext",
       .required = false,
       // My extender unit's third fader lost touch sensing, so it falls back
       // on delayed feedback.
       .overrides = {{.control = "strip[2].fader",
                      .inputs = ControlInput::Type::kPress}}},
  };
}

// Returns JPRSurf's widgets: the X-Touch's controls apart from its strips, and
// these arrays of strips, each from left to right:
// - strip: every strip, the extender's and then the X-Touch's.
// - route_strip: the strips that show routes in Send/Receive mode, which are
//   all but the X-Touch's last.
// - info_strip: the X-Touch's last strip, which shows the Send/Receive mode
//   track itself.
WidgetStructDefinition GetWidgetDefinition() {
  return {.includes = {{.path = "xtouch", .excluded = {"strip"}}},
          .fields = {
              {.name = "strip",
               .widget =
                   WidgetJoinDefinition{.slices = {{.path = "xtouch_ext.strip"},
                                                   {.path = "xtouch.strip"}}}},
              {.name = "route_strip",
               .widget =
                   WidgetJoinDefinition{
                       .slices = {{.path = "xtouch_ext.strip"},
                                  {.path = "xtouch.strip", .count = 7}}}},
              {.name = "info_strip", .widget = std::string("xtouch.strip[7]")},
          }};
}

// Returns the number of elements present in the array that is the field of
// the scene's widgets.
int GetElementCount(const Scene& scene, std::string_view field) {
  const Widget* array = scene.GetWidgets().widget->GetField(field);
  CHECK(array != nullptr) << field;
  return static_cast<int>(array->GetElements().size());
}

// Maps the control's touch to the touched property (such as
// TrackProperties::kVolumeTouched), so REAPER records Touch and Latch
// automation while a hand is on the control.
void AddTouchMapping(View* view, std::string_view property,
                     std::string_view control) {
  view->AddMapping(ViewMapping::kReadControl, property, control,
                   {.read = {.press_release = true, .touch = true}});
}

// Adds the mappings for the channel strip controls that show a track the same
// way in every mode: mute, solo, record arm, pan, volume, name, color, and
// meter, and the touch of the pot and fader. The select button and the bottom
// scribble line are left to the caller, as their meaning depends on the mode.
// `strip` is the strip's path, such as "strip[3]".
void AddTrackStripMappings(View* view, std::string_view strip) {
  const std::string pot = GetFieldPathText(strip, "pot");
  const std::string fader = GetFieldPathText(strip, "fader");
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiMute,
                   GetFieldPathText(strip, "mute"));
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiSolo,
                   GetFieldPathText(strip, "solo"));
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiRecArm,
                   GetFieldPathText(strip, "rec"));
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiPan, pot,
                   {.write = {.mode = "center",
                              .mode_overrides = {
                                  {std::string(TrackProperties::kTrackExists),
                                   {{false, "off"}}},
                                  {std::string(TrackProperties::kTrackIsFolder),
                                   {{true, "center_edges"}}}}}});
  view->AddMapping(ViewMapping::kReadControl, TrackProperties::kUiPan,
                   GetFieldPathText(strip, "pot_button"),
                   {.read = {.property_min = 0.0, .property_max = 0.0}});
  AddTouchMapping(view, TrackProperties::kPanTouched, pot);
  view->AddMapping(ViewMapping::kReadWriteControl, TrackProperties::kUiVolume,
                   fader);
  AddTouchMapping(view, TrackProperties::kVolumeTouched, fader);
  view->AddMapping(ViewMapping::kWriteControl, TrackProperties::kName,
                   GetFieldPathText(strip, "scribble_top"));
  view->AddMapping(ViewMapping::kWriteControl, TrackProperties::kColor,
                   GetFieldPathText(strip, "scribble_color"));
  view->AddMapping(ViewMapping::kWriteControl, TrackProperties::kMeter,
                   GetFieldPathText(strip, "meter"));
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

bool PluginSurface::Register(reaper_plugin_info_t& plugin_info,
                             const ReaperProfiler::Options& profile) {
  return ControlSurface::Register(plugin_info,
                                  {.type_string = kTypeString,
                                   .description = kDescString,
                                   .create_listener = PluginSurface::Create,
                                   .profile = profile});
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
  // The devices let go of their ports as the scene goes, and destroying the
  // MIDI ports then sends it (see MidiPorts).
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
  // Without the devices, as when the X-Touch is absent, there is no scene, so
  // the surface does nothing.
  std::vector<std::string> device_errors;
  std::optional<DeviceList> devices = CreateDevices(
      GetDeviceSpecs(), midi_ports_, device_runner_, &device_errors);
  for (const std::string& error : device_errors) {
    LOG(ERROR) << "Failed to create the devices: " << error;
  }
  if (!devices.has_value()) {
    return;
  }
  std::vector<std::string> widget_errors;
  std::optional<WidgetRoot> widgets = ResolveWidgets(
      GetWidgetDefinition(), devices->widget_devices, &widget_errors);
  for (const std::string& error : widget_errors) {
    LOG(ERROR) << "Invalid widget: " << error;
  }
  CHECK(widgets.has_value());
  scene_ = std::make_unique<Scene>("Scene", std::move(devices->devices),
                                   std::move(*widgets));
  const int strip_count = GetElementCount(*scene_, "strip");
  const int route_strip_count = GetElementCount(*scene_, "route_strip");

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
  // Master fader
  View* master_track_view = root_view->AddChildView(
      "MasterFader", {.subject = View::ReferenceSubject{
                          .name = std::string(Scene::kMasterTrack)}});
  CHECK(master_track_view != nullptr);
  master_track_view->AddMapping(ViewMapping::kReadWriteControl,
                                TrackProperties::kVolume, "master_fader");
  AddTouchMapping(master_track_view, TrackProperties::kVolumeTouched,
                  "master_fader");
  master_track_view->Enable();

  // Modifiers
  root_view->AddMapping(ViewMapping::kReadWriteControl,
                        ModifierProperty::kShift, "shift",
                        {.read = {.press_release = true}});
  root_view->AddMapping(ViewMapping::kReadWriteControl, ModifierProperty::kCtrl,
                        "control", {.read = {.press_release = true}});
  root_view->AddMapping(ViewMapping::kReadWriteControl, ModifierProperty::kAlt,
                        "alt", {.read = {.press_release = true}});
  root_view->AddMapping(ViewMapping::kReadWriteControl, ModifierProperty::kOpt,
                        "option", {.read = {.press_release = true}});

  // Timecode display
  root_view->AddMapping(ViewMapping::kWriteControl, kStateTimelinePosition,
                        "timecode");
  root_view->AddMapping(ViewMapping::kWriteControl, kStateRulerFrames,
                        "smpte_light");
  root_view->AddMapping(ViewMapping::kWriteControl, kStateRulerBeats,
                        "beats_light");
  root_view->AddMapping(ViewMapping::kReadControl, kStateRulerMode,
                        "show_time_beats");
  root_view->AddMapping(ViewMapping::kWriteControl, kStateAnyTrackSolo,
                        "solo_light");

  // Utility buttons
  const ViewMapping::ReadConfig shift = {
      .required_modifiers = {std::string(ModifierProperty::kShift)}};
  const ViewMapping::ReadConfig ctrl = {
      .required_modifiers = {std::string(ModifierProperty::kCtrl)}};
  root_view->AddMapping(ViewMapping::kReadControl, kCmdUndo, "undo");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdRedo, "undo",
                        {.read = shift});
  root_view->AddMapping(ViewMapping::kWriteControl, kStateCanRedo, "undo");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdSaveProject, "save");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdSaveNewProjectVersion,
                        "save", {.read = shift});
  root_view->AddMapping(ViewMapping::kWriteControl, kStateProjectDirty, "save",
                        {.write = {.mode = "blink"}});
  root_view->AddMapping(ViewMapping::kReadControl, kCmdUnselectAllItems,
                        "cancel");
  root_view->AddMapping(ViewMapping::kWriteControl, kStateAnyItemSelected,
                        "cancel");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdRemoveTimeSelection,
                        "cancel", {.read = shift});
  root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertMidiItem, "enter");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertEmptyItem, "enter",
                        {.read = shift});
  root_view->AddMapping(ViewMapping::kReadControl, kCmdInsertClickSource,
                        "enter", {.read = ctrl});

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
                        kStateAutoOverrideActive, "auto_group");
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
      {"auto_trim", kCmdAutoModeTrim, kStateSelectedAutoTrimRead,
       kStateAutoOverrideTrimRead},
      {"auto_read", kCmdAutoModeRead, kStateSelectedAutoRead,
       kStateAutoOverrideRead},
      {"auto_touch", kCmdAutoModeTouch, kStateSelectedAutoTouch,
       kStateAutoOverrideTouch},
      {"auto_write", kCmdAutoModeWrite, kStateSelectedAutoWrite,
       kStateAutoOverrideWrite},
      {"auto_latch", kCmdAutoModeLatch, kStateSelectedAutoLatch,
       kStateAutoOverrideLatch, kStateAutoOverrideAnyLatch},
  };
  for (const AutoModeButton& info : kAutoModeButtons) {
    root_view->AddMapping(ViewMapping::kReadControl, info.command, info.button,
                          {.condition = no_override});
    root_view->AddMapping(
        ViewMapping::kWriteControl, info.selected_state, info.button,
        {.write = {.mode_overrides = {{std::string(kStateSelectedAutoMixed),
                                       {{true, "blink"}}}}},
         .condition = no_override});
    root_view->AddMapping(ViewMapping::kReadControl, info.override_state,
                          info.button, {.condition = in_override});
    // The other override lights are never on during Latch Preview, so the
    // blink only affects Latch.
    root_view->AddMapping(
        ViewMapping::kWriteControl,
        info.override_light.empty() ? info.override_state : info.override_light,
        info.button,
        {.write = {.mode_overrides = {{std::string(
                                           kStateAutoOverrideLatchPreview),
                                       {{true, "blink"}}}}},
         .condition = in_override});
  }

  // Misc buttons (above transport). Marker and Nudge are with the transport
  // controls below.
  root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRepeat,
                        "cycle");
  root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdMetronome, "click");
  root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdSoloInFront,
                        "solo");
  root_view->AddMapping(
      ViewMapping::kReadControl, kCmdSoloDefeat, "solo",
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
  for (const TransportStep& step : kTransportSteps) {
    const ViewCondition::Config on_step = {
        .property = std::string(kTransportStep),
        .value = std::string(step.name)};
    root_view->AddMapping(ViewMapping::kReadControl, step.prev_command,
                          "rewind", {.condition = on_step});
    root_view->AddMapping(ViewMapping::kReadControl, step.next_command,
                          "forward", {.condition = on_step});
    if (step.button.empty()) {
      continue;
    }
    root_view->AddMapping(
        ViewMapping::kReadControl, kTransportStep, step.button,
        {.read = {.property_min = std::string(kTransportSteps[0].name),
                  .property_max = std::string(step.name),
                  .press_toggles = true}});
    root_view->AddMapping(ViewMapping::kWriteControl, lit, step.button,
                          {.condition = on_step});
  }
  root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportStop, "stop");
  root_view->AddMapping(ViewMapping::kReadControl, kCmdTransportPlayPause,
                        "play");
  root_view->AddMapping(ViewMapping::kWriteControl, kCmdTransportPlay, "play");
  root_view->AddMapping(ViewMapping::kReadWriteControl, kCmdTransportRecord,
                        "record");

  // Surface modes. Track returns to Track mode, and each mode view maps what
  // tapping Send does. Entering Send/Receive mode is two mappings on the same
  // press, with the same condition: one picks the current track, and the other
  // sets the mode.
  CHECK(scene_->AddUserProperty(std::make_unique<EnumeratedValueProperty>(
            kSurfaceMode,
            std::vector<std::string>(std::begin(kSurfaceModeNames),
                                     std::end(kSurfaceModeNames)))) != nullptr);
  CHECK(scene_->AddModifierProperty(kSendHold) != 0);
  CHECK(scene_->AddUserProperty(std::make_unique<TrackPickProperty>(
            kPickSelectedTrack, *current_track,
            scene_->GetTrackReference(Scene::kSelectedTrack))) != nullptr);
  root_view->AddMapping(ViewMapping::kReadControl, kSurfaceMode, "assign_track",
                        {.read = {.property_min = std::string(kTrackMode),
                                  .property_max = std::string(kTrackMode)}});
  root_view->AddMapping(ViewMapping::kReadControl, kSendHold, "assign_send",
                        {.read = {.press_release = true}});
  root_view->Enable();

  // Add the Track mode view, which holds everything that is specific to Track
  // mode.
  View* track_mode_view = root_view->AddChildView(
      "TrackMode",
      {.condition = ViewCondition::Config{.property = std::string(kSurfaceMode),
                                          .value = std::string(kTrackMode)}});
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
  for (int i = 0; i < strip_count; ++i) {
    const std::string strip = GetElementPathText("strip", i);
    View* track_view = track_list_view->AddChildView(
        absl::StrCat("Track", i + 1), {.subject = View::ListItemSubject{}});
    CHECK(track_view != nullptr);
    // Select selects the track, and double press navigates into it.
    const std::string select = GetFieldPathText(strip, "select");
    track_view->AddMapping(ViewMapping::kReadControl,
                           TrackProperties::kUiSelected, select);
    track_view->AddMapping(
        ViewMapping::kReadControl, View::kParentTrackChild, select,
        {.read = {.press_behavior = InputConfig::PressBehavior::kDoublePress}});

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
        {.read = {.required_modifiers = {std::string(kSelectAnchor)}}});

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
    const std::string send_hold(kSendHold);
    for (const std::vector<std::string>& modifiers :
         {std::vector{send_hold},
          std::vector{send_hold, std::string(kSelectAnchor)}}) {
      track_view->AddMapping(
          ViewMapping::kReadControl, kPickTrack, select,
          {.read = {.required_modifiers = modifiers}, .condition = has_routes});
      track_view->AddMapping(
          ViewMapping::kReadControl, kSurfaceMode, select,
          {.read = {.required_modifiers = modifiers,
                    .property_min = std::string(kSendReceiveMode),
                    .property_max = std::string(kSendReceiveMode)},
           .condition = has_routes});
    }

    // The select light shows whether the track is selected, or while Send is
    // held, whether it has routes to show in Send/Receive mode.
    track_view->AddMapping(
        ViewMapping::kWriteControl, TrackProperties::kUiSelected, select,
        {.condition = ViewCondition::Config{.property = std::string(kSendHold),
                                            .value = false}});
    track_view->AddMapping(
        ViewMapping::kWriteControl, TrackProperties::kTrackHasRoutes, select,
        {.condition = ViewCondition::Config{.property = std::string(kSendHold),
                                            .value = true}});
    AddTrackStripMappings(track_view, strip);

    // Holding mute, solo, or record arm anchors the track, so pressing the
    // same button on another track sets the range between them to the held
    // track's value.
    AddTrackAnchorMapping(track_view, kAnchorMute,
                          {.action = TrackBoolProperty::kMute},
                          GetFieldPathText(strip, "mute"));
    AddTrackAnchorMapping(track_view, kAnchorSolo,
                          {.action = TrackBoolProperty::kSolo},
                          GetFieldPathText(strip, "solo"));
    AddTrackAnchorMapping(track_view, kAnchorRecArm,
                          {.action = TrackBoolProperty::kRecArm},
                          GetFieldPathText(strip, "rec"));
    // The bottom scribble line shows the volume, and is blank for an empty
    // strip.
    track_view->AddMapping(
        ViewMapping::kWriteControl, TrackProperties::kUiVolume,
        GetFieldPathText(strip, "scribble_bottom"),
        {.condition = ViewCondition::Config{
             .property = std::string(TrackProperties::kTrackExists)}});
    track_view->Enable();
  }
  // Global navigates up one level, or all the way to the root when held. It
  // is lit while there is a level to go up to.
  track_list_view->AddMapping(ViewMapping::kReadControl, View::kTrackParent,
                              "global");
  track_list_view->AddMapping(
      ViewMapping::kReadControl, View::kTrackRoot, "global",
      {.read = {.press_behavior = InputConfig::PressBehavior::kLongPress}});
  track_list_view->AddMapping(ViewMapping::kWriteControl,
                              TrackProperties::kTrackHasParent, "global");
  track_list_view->AddMapping(ViewMapping::kReadControl, View::kChildDec,
                              "channel_left");
  track_list_view->AddMapping(ViewMapping::kReadControl, View::kChildInc,
                              "channel_right");
  track_list_view->AddMapping(ViewMapping::kReadControl, View::kBankDec,
                              "bank_left");
  track_list_view->AddMapping(ViewMapping::kReadControl, View::kBankInc,
                              "bank_right");
  track_list_view->Enable();
  // Tapping Send enters Send/Receive mode for the selected track, if it has
  // routes.
  const ViewCondition::Config has_routes = {
      .property = std::string(kSelectedTrackHasRoutes)};
  const InputConfig::PressBehavior tap = InputConfig::PressBehavior::kTap;
  track_mode_view->AddMapping(
      ViewMapping::kReadControl, kPickSelectedTrack, "assign_send",
      {.read = {.press_behavior = tap}, .condition = has_routes});
  track_mode_view->AddMapping(
      ViewMapping::kReadControl, kSurfaceMode, "assign_send",
      {.read = {.press_behavior = tap,
                .property_min = std::string(kSendReceiveMode),
                .property_max = std::string(kSendReceiveMode)},
       .condition = has_routes});

  // Track blinks, as it is the current mode, and Send is lit while the
  // selected track has routes to show.
  track_mode_view->AddMapping(ViewMapping::kWriteControl, lit, "assign_track",
                              {.write = {.mode = "blink"}});
  track_mode_view->AddMapping(ViewMapping::kWriteControl,
                              kSelectedTrackHasRoutes, "assign_send");
  track_mode_view->Enable();

  // Add the Send/Receive mode view, which shows the routes of the current
  // track: its sends, unless it only has receives. Bank left/right pages
  // through all the route strips at once. If the track is deleted, it shows
  // nothing until another track is touched.
  View* send_receive_mode_view = root_view->AddChildView(
      "SendReceiveMode",
      {.condition =
           ViewCondition::Config{.property = std::string(kSurfaceMode),
                                 .value = std::string(kSendReceiveMode)},
       .subject = View::ReferenceSubject{.name = std::string(kCurrentTrack)},
       .list = View::ListConfig{.items = View::Routes{}}});
  CHECK(send_receive_mode_view != nullptr);

  // Add a route view for each channel strip, which will show consecutive sends
  // or receives of the Send/Receive mode track.
  for (int i = 0; i < route_strip_count; ++i) {
    const std::string strip = GetElementPathText("route_strip", i);
    View* route_view = send_receive_mode_view->AddChildView(
        absl::StrCat("Route", i + 1), {.subject = View::ListItemSubject{}});
    CHECK(route_view != nullptr);
    // Select navigates across the route to the track at its other end.
    route_view->AddMapping(ViewMapping::kReadControl,
                           View::kParentRouteOtherTrack,
                           GetFieldPathText(strip, "select"));
    route_view->AddMapping(ViewMapping::kReadWriteControl,
                           RouteProperties::kMute,
                           GetFieldPathText(strip, "mute"));
    route_view->AddMapping(
        ViewMapping::kReadWriteControl, RouteProperties::kPan,
        GetFieldPathText(strip, "pot"),
        {.write = {.mode = "center",
                   .mode_overrides = {{std::string(RouteProperties::kExists),
                                       {{false, "off"}}}}}});
    route_view->AddMapping(
        ViewMapping::kReadControl, RouteProperties::kPan,
        GetFieldPathText(strip, "pot_button"),
        {.read = {.property_min = 0.0, .property_max = 0.0}});
    route_view->AddMapping(ViewMapping::kReadWriteControl,
                           RouteProperties::kVolume,
                           GetFieldPathText(strip, "fader"));
    route_view->AddMapping(ViewMapping::kWriteControl, kOtherTrackName,
                           GetFieldPathText(strip, "scribble_top"));
    // The bottom scribble line shows the volume, and is blank for an empty
    // strip.
    route_view->AddMapping(
        ViewMapping::kWriteControl, RouteProperties::kVolume,
        GetFieldPathText(strip, "scribble_bottom"),
        {.condition = ViewCondition::Config{
             .property = std::string(RouteProperties::kExists)}});
    route_view->AddMapping(ViewMapping::kWriteControl, kOtherTrackColor,
                           GetFieldPathText(strip, "scribble_color"));
    route_view->Enable();
  }
  send_receive_mode_view->AddMapping(ViewMapping::kReadControl, View::kChildDec,
                                     "channel_left");
  send_receive_mode_view->AddMapping(ViewMapping::kReadControl, View::kChildInc,
                                     "channel_right");
  send_receive_mode_view->AddMapping(ViewMapping::kReadControl, View::kBankDec,
                                     "bank_left");
  send_receive_mode_view->AddMapping(ViewMapping::kReadControl, View::kBankInc,
                                     "bank_right");

  // The Info strip shows the Send/Receive mode track itself, the same as in
  // Track mode, except the bottom scribble line shows whether its sends or
  // receives are shown, and is blank once the track is deleted.
  AddTrackStripMappings(send_receive_mode_view, "info_strip");
  send_receive_mode_view->AddMapping(
      ViewMapping::kWriteControl, View::kChildRouteTypeName,
      "info_strip.scribble_bottom",
      {.condition = ViewCondition::Config{
           .property = std::string(TrackProperties::kTrackExists)}});

  // Tapping Send switches between showing sends and receives.
  send_receive_mode_view->AddMapping(
      ViewMapping::kReadControl, View::kChildRouteToggle, "assign_send",
      {.read = {.press_behavior = InputConfig::PressBehavior::kTap}});

  // Send blinks, as it is the current mode, even if the track no longer has
  // routes, and Track is lit, as it is always available.
  send_receive_mode_view->AddMapping(ViewMapping::kWriteControl, lit,
                                     "assign_send",
                                     {.write = {.mode = "blink"}});
  send_receive_mode_view->AddMapping(ViewMapping::kWriteControl, lit,
                                     "assign_track");
  send_receive_mode_view->Enable();

  // Finally activate the scene, which will start it running and activate all
  // enabled views.
  scene_->Activate(scene_runner_);
}

}  // namespace jpr