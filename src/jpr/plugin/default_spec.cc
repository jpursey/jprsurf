// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_spec.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "jpr/common/prefixed_name.h"
#include "jpr/common/track.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/widget_definition.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/state_properties.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

// The top level mode of the surface, which determines what the channel strips
// control. Each mode has a view that is active while it is the mode. Its values
// are the modes below, in order.
constexpr std::string_view kSurfaceMode = kUserName<"surface_mode">;

// Strips show tracks in the hierarchy.
constexpr std::string_view kTrackMode = "track";

// Strips show one track's routes.
constexpr std::string_view kSendReceiveMode = "send_receive";

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

// The anchors each track strip declares for its track, for ranges of tracks.
constexpr std::string_view kAnchorSelect = kUserName<"anchor_select">;
constexpr std::string_view kAnchorMute = kUserName<"anchor_mute">;
constexpr std::string_view kAnchorSolo = kUserName<"anchor_solo">;
constexpr std::string_view kAnchorRecArm = kUserName<"anchor_rec_arm">;

// Picks the current track for Send/Receive mode: the selected track, or a track
// strip's track, which each track strip declares.
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

// An automation mode button: the arguments of the auto_mode_button template.
struct AutoModeButton {
  std::string_view button;
  std::string_view command;
  std::string_view selected_state;
  std::string_view override_state;
  std::string_view override_light;
};
constexpr AutoModeButton kAutoModeButtons[] = {
    {"auto_trim", kCmdAutoModeTrim, kStateSelectedAutoTrimRead,
     kStateAutoOverrideTrimRead, kStateAutoOverrideTrimRead},
    {"auto_read", kCmdAutoModeRead, kStateSelectedAutoRead,
     kStateAutoOverrideRead, kStateAutoOverrideRead},
    {"auto_touch", kCmdAutoModeTouch, kStateSelectedAutoTouch,
     kStateAutoOverrideTouch, kStateAutoOverrideTouch},
    {"auto_write", kCmdAutoModeWrite, kStateSelectedAutoWrite,
     kStateAutoOverrideWrite, kStateAutoOverrideWrite},
    {"auto_latch", kCmdAutoModeLatch, kStateSelectedAutoLatch,
     kStateAutoOverrideLatch, kStateAutoOverrideAnyLatch},
};

// The press behaviors the spec uses.
constexpr InputConfig::PressBehavior kTap = InputConfig::PressBehavior::kTap;
constexpr InputConfig::PressBehavior kLongPress =
    InputConfig::PressBehavior::kLongPress;
constexpr InputConfig::PressBehavior kDoublePress =
    InputConfig::PressBehavior::kDoublePress;

// Returns a mapping of the property to the control, of the type.
MappingSpec Map(ViewMapping::TypeFlags type, std::string_view property,
                std::string_view control, ViewMapping::Config config) {
  return {.property = std::string(property),
          .control = std::string(control),
          .type = type,
          .config = std::move(config)};
}

// Returns a mapping that reads the control into the property.
MappingSpec Read(std::string_view property, std::string_view control,
                 ViewMapping::Config config = {}) {
  return Map(ViewMapping::kReadControl, property, control, std::move(config));
}

// Returns a mapping that writes the property to the control.
MappingSpec Write(std::string_view property, std::string_view control,
                  ViewMapping::Config config = {}) {
  return Map(ViewMapping::kWriteControl, property, control, std::move(config));
}

// Returns a mapping that reads the control into the property, and writes the
// property to the control.
MappingSpec ReadWrite(std::string_view property, std::string_view control,
                      ViewMapping::Config config = {}) {
  return Map(ViewMapping::kReadWriteControl, property, control,
             std::move(config));
}

// Returns a mapping that lights the control, for a light that is lit whenever
// the mapping is active.
MappingSpec Light(std::string_view control, ViewMapping::Config config = {}) {
  return {.property =
              FixedValue{.type = ViewProperty::Type::kToggle, .value = true},
          .control = std::string(control),
          .type = ViewMapping::kWriteControl,
          .config = std::move(config)};
}

// Returns a mapping of the control's touch to the touched property (such as
// TrackProperties::kVolumeTouched), so REAPER records Touch and Latch
// automation while a hand is on the control.
MappingSpec Touch(std::string_view property, std::string_view control) {
  return Read(property, control,
              {.read = {.press_release = true, .touch = true}});
}

// Returns a condition that the property is the value.
ViewCondition::Config When(std::string_view property,
                           ViewProperty::Value value = true) {
  return {.property = std::string(property), .value = std::move(value)};
}

// Returns `read`, setting the property to the value: a range whose ends are the
// value.
ViewMapping::ReadConfig SetTo(ViewProperty::Value value,
                              ViewMapping::ReadConfig read = {}) {
  read.property_min = value;
  read.property_max = std::move(value);
  return read;
}

// Returns a config that reads a press and a release, for a property that is on
// while the control is held.
ViewMapping::Config Held() { return {.read = {.press_release = true}}; }

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

// Returns the components declared at the top level.
std::vector<ComponentSpec> GetComponents() {
  std::vector<std::string> step_names;
  for (const TransportStep& step : kTransportSteps) {
    step_names.emplace_back(step.name);
  }
  return {
      ModifierSpec{.name = std::string(kSendHold)},
      ModifierSpec{.name = std::string(kSelectAnchor)},
      EnumeratedValueSpec{.name = std::string(kTransportStep),
                          .values = std::move(step_names)},
      EnumeratedValueSpec{
          .name = std::string(kSurfaceMode),
          .values = {std::string(kTrackMode), std::string(kSendReceiveMode)}},
      TrackReferenceSpec{.name = std::string(kFolder),
                         .fallback = std::string(Scene::kMasterTrack)},
      TrackReferenceSpec{.name = std::string(kCurrentTrack),
                         .follow = std::string(Scene::kLastTouchedTrack)},
      TrackPickSpec{.name = std::string(kPickSelectedTrack),
                    .reference = std::string(kCurrentTrack),
                    .source = std::string(Scene::kSelectedTrack)},
  };
}

// Returns the track_strip template, which maps the controls of the strip that
// show a track the same way in every mode: mute, solo, record arm, pan,
// volume, name, color, and meter, and the touch of the pot and fader. The
// select button and the bottom scribble line are left to the view, as their
// meaning depends on the mode.
TemplateSpec GetTrackStripTemplate() {
  return {.name = "track_strip",
          .widget_params = {"strip"},
          .mappings = {
              ReadWrite(TrackProperties::kUiMute, "strip.mute"),
              ReadWrite(TrackProperties::kUiSolo, "strip.solo"),
              ReadWrite(TrackProperties::kUiRecArm, "strip.rec"),
              ReadWrite(
                  TrackProperties::kUiPan, "strip.pot",
                  {.write = {.mode = "center",
                             .mode_overrides =
                                 {{std::string(TrackProperties::kTrackExists),
                                   {{false, "off"}}},
                                  {std::string(TrackProperties::kTrackIsFolder),
                                   {{true, "center_edges"}}}}}}),
              Read(TrackProperties::kUiPan, "strip.pot_button",
                   {.read = SetTo(0.0)}),
              Touch(TrackProperties::kPanTouched, "strip.pot"),
              ReadWrite(TrackProperties::kUiVolume, "strip.fader"),
              Touch(TrackProperties::kVolumeTouched, "strip.fader"),
              Write(TrackProperties::kName, "strip.scribble_top"),
              Write(TrackProperties::kColor, "strip.scribble_color"),
              Write(TrackProperties::kMeter, "strip.meter"),
          }};
}

// Returns the auto_mode_button template, which maps an automation mode button
// (see GetRootView()).
TemplateSpec GetAutoModeButtonTemplate() {
  const ViewCondition::Config no_override =
      When(kStateAutoOverrideActive, false);
  const ViewCondition::Config in_override = When(kStateAutoOverrideActive);
  return {
      .name = "auto_mode_button",
      .widget_params = {"button"},
      .property_params = {"command", "selected_state", "override_state",
                          "override_light"},
      .mappings = {
          Read("command", "button", {.condition = no_override}),
          Write("selected_state", "button",
                {.write = {.mode_overrides = {{std::string(
                                                   kStateSelectedAutoMixed),
                                               {{true, "blink"}}}}},
                 .condition = no_override}),
          Read("override_state", "button", {.condition = in_override}),
          // The other override lights are never on during Latch Preview, so
          // the blink only affects Latch.
          Write("override_light", "button",
                {.write = {.mode_overrides =
                               {{std::string(kStateAutoOverrideLatchPreview),
                                 {{true, "blink"}}}}},
                 .condition = in_override}),
      }};
}

// Returns the list_navigation template, which scrolls the view's list: Channel
// by one item, and Bank by its bank size.
TemplateSpec GetListNavigationTemplate() {
  return {.name = "list_navigation",
          .mappings = {
              Read(View::kChildDec, "channel_left"),
              Read(View::kChildInc, "channel_right"),
              Read(View::kBankDec, "bank_left"),
              Read(View::kBankInc, "bank_right"),
          }};
}

// Returns the view of each strip in the track list, which shows a track.
ViewSpec GetTrackView() {
  // While Send is held, pressing select instead enters Send/Receive mode for
  // the track, if it has routes, and otherwise does nothing. This uses
  // required modifiers rather than a condition, so holding Send never resets
  // a pending press. Required modifiers must match exactly, so this also
  // needs mappings for Send held along with a select anchor, where picking
  // wins over the range.
  const ViewCondition::Config has_routes =
      When(TrackProperties::kTrackHasRoutes);
  const std::vector<std::string> send_hold = {std::string(kSendHold)};
  const std::vector<std::string> send_hold_and_anchor = {
      std::string(kSendHold), std::string(kSelectAnchor)};
  const std::string send_receive(kSendReceiveMode);

  return {
      .config = {.subject = View::ListItemSubject{}},
      .repeat = RepeatSpec{.array = "strip", .element = "strip"},
      .components =
          {
              TrackAnchorSpec{.name = std::string(kAnchorSelect),
                              .action = TrackBoolProperty::kSelected,
                              .modifier = std::string(kSelectAnchor)},
              TrackAnchorSpec{.name = std::string(kAnchorMute),
                              .action = TrackBoolProperty::kMute},
              TrackAnchorSpec{.name = std::string(kAnchorSolo),
                              .action = TrackBoolProperty::kSolo},
              TrackAnchorSpec{.name = std::string(kAnchorRecArm),
                              .action = TrackBoolProperty::kRecArm},
              TrackPickSpec{.name = std::string(kPickTrack),
                            .reference = std::string(kCurrentTrack)},
          },
      .uses = {{.name = "track_strip", .widgets = {{"strip", "strip"}}}},
      .mappings =
          {
              // Select selects the track, and double press navigates into it.
              Read(TrackProperties::kUiSelected, "strip.select"),
              Read(View::kParentTrackChild, "strip.select",
                   {.read = {.press_behavior = kDoublePress}}),

              // Holding select (a long press) selects the track and anchors
              // it, so pressing another track's select selects the range
              // between them. The select anchor turns on a modifier, which puts
              // the other select buttons in a group with no double press, so
              // the range is selected as soon as they are pressed.
              Read(TrackProperties::kUiSelected, "strip.select",
                   {.read = {.press_behavior = kLongPress}}),
              Read(kAnchorSelect, "strip.select",
                   {.read = {.press_behavior = kLongPress,
                             .press_release = true}}),
              Read(TrackProperties::kUiSelected, "strip.select",
                   {.read = {.required_modifiers = {std::string(
                                 kSelectAnchor)}}}),

              // Picking the track, and entering Send/Receive mode, while Send
              // is held (see above).
              Read(kPickTrack, "strip.select",
                   {.read = {.required_modifiers = send_hold},
                    .condition = has_routes}),
              Read(kSurfaceMode, "strip.select",
                   {.read =
                        SetTo(send_receive, {.required_modifiers = send_hold}),
                    .condition = has_routes}),
              Read(kPickTrack, "strip.select",
                   {.read = {.required_modifiers = send_hold_and_anchor},
                    .condition = has_routes}),
              Read(kSurfaceMode, "strip.select",
                   {.read = SetTo(send_receive,
                                  {.required_modifiers = send_hold_and_anchor}),
                    .condition = has_routes}),

              // The select light shows whether the track is selected, or while
              // Send is held, whether it has routes to show in Send/Receive
              // mode.
              Write(TrackProperties::kUiSelected, "strip.select",
                    {.condition = When(kSendHold, false)}),
              Write(TrackProperties::kTrackHasRoutes, "strip.select",
                    {.condition = When(kSendHold)}),

              // Holding mute, solo, or record arm anchors the track, so
              // pressing the same button on another track sets the range
              // between them to the held track's value.
              Read(kAnchorMute, "strip.mute", Held()),
              Read(kAnchorSolo, "strip.solo", Held()),
              Read(kAnchorRecArm, "strip.rec", Held()),

              // The bottom scribble line shows the volume, and is blank for an
              // empty strip.
              Write(TrackProperties::kUiVolume, "strip.scribble_bottom",
                    {.condition = When(TrackProperties::kTrackExists)}),
          },
  };
}

// Returns the Track mode view, which shows the child tracks of the folder.
ViewSpec GetTrackModeView() {
  // The track list has a track view for each strip. Bank left/right pages by
  // 8, a device at a time. It reveals the current track whenever it changes,
  // and when returning to Track mode.
  ViewSpec track_list = {
      .name = "track_list",
      .config = {.subject =
                     View::ReferenceSubject{.name = std::string(kFolder)},
                 .list =
                     View::ListConfig{
                         .items = View::ChildTracks{.reveal = std::string(
                                                        kCurrentTrack)},
                         .bank_size = 8}},
      .uses = {{.name = "list_navigation"}},
      .mappings =
          {
              // Global navigates up one level, or all the way to the root when
              // held. It is lit while there is a level to go up to.
              Read(View::kTrackParent, "global"),
              Read(View::kTrackRoot, "global",
                   {.read = {.press_behavior = kLongPress}}),
              Write(TrackProperties::kTrackHasParent, "global"),
          },
      .children = {GetTrackView()},
  };

  // Tapping Send enters Send/Receive mode for the selected track, if it has
  // routes.
  const ViewCondition::Config has_routes = When(kSelectedTrackHasRoutes);
  return {
      .name = "track_mode",
      .config = {.condition = When(kSurfaceMode, std::string(kTrackMode))},
      .mappings =
          {
              Read(kPickSelectedTrack, "assign_send",
                   {.read = {.press_behavior = kTap}, .condition = has_routes}),
              Read(kSurfaceMode, "assign_send",
                   {.read = SetTo(std::string(kSendReceiveMode),
                                  {.press_behavior = kTap}),
                    .condition = has_routes}),
              // Track blinks, as it is the current mode, and Send is lit while
              // the selected track has routes to show.
              Light("assign_track", {.write = {.mode = "blink"}}),
              Write(kSelectedTrackHasRoutes, "assign_send"),
          },
      .children = {std::move(track_list)},
  };
}

// Returns the Send/Receive mode view, which shows the routes of the current
// track: its sends, unless it only has receives. Bank left/right pages through
// all the route strips at once. If the track is deleted, it shows nothing
// until another track is touched.
ViewSpec GetSendReceiveModeView() {
  // A route view for each route strip, which show consecutive sends or
  // receives of the current track.
  ViewSpec route_view = {
      .config = {.subject = View::ListItemSubject{}},
      .repeat = RepeatSpec{.array = "route_strip", .element = "strip"},
      .mappings =
          {
              // Select navigates across the route to the track at its other
              // end.
              Read(View::kParentRouteOtherTrack, "strip.select"),
              ReadWrite(RouteProperties::kMute, "strip.mute"),
              ReadWrite(RouteProperties::kPan, "strip.pot",
                        {.write = {.mode = "center",
                                   .mode_overrides =
                                       {{std::string(RouteProperties::kExists),
                                         {{false, "off"}}}}}}),
              Read(RouteProperties::kPan, "strip.pot_button",
                   {.read = SetTo(0.0)}),
              ReadWrite(RouteProperties::kVolume, "strip.fader"),
              Write(kOtherTrackName, "strip.scribble_top"),
              // The bottom scribble line shows the volume, and is blank for an
              // empty strip.
              Write(RouteProperties::kVolume, "strip.scribble_bottom",
                    {.condition = When(RouteProperties::kExists)}),
              Write(kOtherTrackColor, "strip.scribble_color"),
          },
  };

  return {
      .name = "send_receive_mode",
      .config = {.condition = When(kSurfaceMode, std::string(kSendReceiveMode)),
                 .subject =
                     View::ReferenceSubject{.name = std::string(kCurrentTrack)},
                 .list = View::ListConfig{.items = View::Routes{}}},
      // The Info strip shows the current track itself, the same as in Track
      // mode, except the bottom scribble line.
      .uses = {{.name = "list_navigation"},
               {.name = "track_strip", .widgets = {{"strip", "info_strip"}}}},
      .mappings =
          {
              // The Info strip's bottom scribble line shows whether sends or
              // receives are shown, and is blank once the track is deleted.
              Write(View::kChildRouteTypeName, "info_strip.scribble_bottom",
                    {.condition = When(TrackProperties::kTrackExists)}),
              // Tapping Send switches between showing sends and receives.
              Read(View::kChildRouteToggle, "assign_send",
                   {.read = {.press_behavior = kTap}}),
              // Send blinks, as it is the current mode, even if the track no
              // longer has routes, and Track is lit, as it is always
              // available.
              Light("assign_send", {.write = {.mode = "blink"}}),
              Light("assign_track"),
          },
      .children = {std::move(route_view)},
  };
}

// Returns the root view, with the mappings of the controls that do the same in
// every mode, and a view for the master fader and for each surface mode.
ViewSpec GetRootView() {
  const ViewMapping::Config shift = {
      .read = {.required_modifiers = {std::string(ModifierProperty::kShift)}}};
  const ViewMapping::Config ctrl = {
      .read = {.required_modifiers = {std::string(ModifierProperty::kCtrl)}}};
  ViewSpec root = {
      .mappings =
          {
              // Modifiers
              ReadWrite(ModifierProperty::kShift, "shift", Held()),
              ReadWrite(ModifierProperty::kCtrl, "control", Held()),
              ReadWrite(ModifierProperty::kAlt, "alt", Held()),
              ReadWrite(ModifierProperty::kOpt, "option", Held()),

              // Timecode display
              Write(kStateTimelinePosition, "timecode"),
              Write(kStateRulerFrames, "smpte_light"),
              Write(kStateRulerBeats, "beats_light"),
              Read(kStateRulerMode, "show_time_beats"),
              Write(kStateAnyTrackSolo, "solo_light"),

              // Utility buttons
              Read(kCmdUndo, "undo"),
              Read(kCmdRedo, "undo", shift),
              Write(kStateCanRedo, "undo"),
              Read(kCmdSaveProject, "save"),
              Read(kCmdSaveNewProjectVersion, "save", shift),
              Write(kStateProjectDirty, "save", {.write = {.mode = "blink"}}),
              Read(kCmdUnselectAllItems, "cancel"),
              Write(kStateAnyItemSelected, "cancel"),
              Read(kCmdRemoveTimeSelection, "cancel", shift),
              Read(kCmdInsertMidiItem, "enter"),
              Read(kCmdInsertEmptyItem, "enter", shift),
              Read(kCmdInsertClickSource, "enter", ctrl),

              // Automation buttons. Group turns the global automation override
              // on and off, and is lit while it is on. The mode buttons are
              // uses of auto_mode_button, below.
              ReadWrite(kStateAutoOverrideActive, "auto_group"),

              // Misc buttons (above transport). Marker and Nudge are with the
              // transport steps, below.
              ReadWrite(kCmdTransportRepeat, "cycle"),
              ReadWrite(kCmdMetronome, "click"),
              ReadWrite(kCmdSoloInFront, "solo"),
              Read(kCmdSoloDefeat, "solo",
                   {.read = {.press_behavior = kLongPress}}),

              // Transport controls, apart from Rewind and Forward, below.
              Read(kCmdTransportStop, "stop"),
              Read(kCmdTransportPlayPause, "play"),
              Write(kCmdTransportPlay, "play"),
              ReadWrite(kCmdTransportRecord, "record"),

              // Surface modes. Track returns to Track mode, and each mode view
              // maps what tapping Send does. Entering Send/Receive mode is two
              // mappings on the same press, with the same condition: one picks
              // the current track, and the other sets the mode.
              Read(kSurfaceMode, "assign_track",
                   {.read = SetTo(std::string(kTrackMode))}),
              Read(kSendHold, "assign_send", Held()),
          },
      .children =
          {
              {.name = "master_fader",
               .config = {.subject =
                              View::ReferenceSubject{
                                  .name = std::string(Scene::kMasterTrack)}},
               .mappings = {ReadWrite(TrackProperties::kVolume, "master_fader"),
                            Touch(TrackProperties::kVolumeTouched,
                                  "master_fader")}},
              GetTrackModeView(),
              GetSendReceiveModeView(),
          },
  };

  // Without an override, each automation mode button sets the selected tracks'
  // mode, and is lit while any selected track is in its mode: solid if they
  // all are, and blinking if only some are.
  //
  // With an override, each mode button sets the override to its mode (or to
  // Bypass if it already is), and is lit while the override is its mode. There
  // is no Latch Preview button, so Latch blinks for Latch Preview, and pressing
  // it sets Latch.
  for (const AutoModeButton& info : kAutoModeButtons) {
    root.uses.push_back(
        {.name = "auto_mode_button",
         .widgets = {{"button", std::string(info.button)}},
         .properties = {{"command", std::string(info.command)},
                        {"selected_state", std::string(info.selected_state)},
                        {"override_state", std::string(info.override_state)},
                        {"override_light", std::string(info.override_light)}}});
  }

  // Rewind and Forward move by the transport step, which Marker and Nudge pick.
  // It is one value, so turning one on turns the other off. Each toggles
  // between its step and the first (a measure), and is lit while its step is
  // picked.
  for (const TransportStep& step : kTransportSteps) {
    const ViewCondition::Config on_step =
        When(kTransportStep, std::string(step.name));
    root.mappings.push_back(
        Read(step.prev_command, "rewind", {.condition = on_step}));
    root.mappings.push_back(
        Read(step.next_command, "forward", {.condition = on_step}));
    if (step.button.empty()) {
      continue;
    }
    root.mappings.push_back(
        Read(kTransportStep, step.button,
             {.read = {.property_min = std::string(kTransportSteps[0].name),
                       .property_max = std::string(step.name),
                       .press_toggles = true}}));
    root.mappings.push_back(Light(step.button, {.condition = on_step}));
  }
  return root;
}

}  // namespace

SurfaceSpec GetDefaultSpec() {
  return {.devices = GetDeviceSpecs(),
          .widgets = GetWidgetDefinition(),
          .components = GetComponents(),
          .templates = {GetTrackStripTemplate(), GetAutoModeButtonTemplate(),
                        GetListNavigationTemplate()},
          .root = GetRootView()};
}

}  // namespace jpr
