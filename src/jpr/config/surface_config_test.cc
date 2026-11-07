// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/config/surface_config.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/track.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/widget_definition.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/surface_spec.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The version, and the blocks every config has, with nothing in them.
constexpr std::string_view kVersion = "jprsurf 1\n";
constexpr std::string_view kEmptyBlocks = "devices {}\nwidgets {}\n";

// Reads the config, expecting no errors.
SurfaceSpec ReadValid(std::string_view text) {
  std::vector<std::string> errors;
  std::optional<SurfaceSpec> spec =
      ReadSurfaceConfig(std::string(text), &errors);
  EXPECT_TRUE(spec.has_value());
  EXPECT_THAT(errors, IsEmpty());
  return spec.value_or(SurfaceSpec());
}

// Reads the config, expecting it to fail, and returns its errors.
std::vector<std::string> ReadErrors(std::string_view text) {
  std::vector<std::string> errors;
  EXPECT_EQ(ReadSurfaceConfig(std::string(text), &errors), std::nullopt);
  return errors;
}

//------------------------------------------------------------------------------
// Statements
//------------------------------------------------------------------------------

TEST(ReadSurfaceConfigTest, EmptyBlocksAreAnEmptySpec) {
  EXPECT_EQ(ReadValid(absl::StrCat(kVersion, kEmptyBlocks)), SurfaceSpec());
  EXPECT_EQ(ReadValid("# A comment first\n"
                      "jprsurf 1  # And after\n"
                      "widgets { }  devices { }\n"),
            SurfaceSpec());
}

TEST(ReadSurfaceConfigTest, ReadsDevices) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch Out")
  xtouch_ext = XTouchExt(output = "X-Touch-Ext", input = "X-Touch-Ext",
                         optional = true) {
    without strip[2].fader [press]
    without strip[0].scribble_top [text, value]
  }
  other = XTouch(input = "In", output = "Out", optional = false) {}
}
widgets {}
)");
  EXPECT_THAT(
      spec.devices,
      ElementsAre(
          DeviceSpec{.name = "xtouch",
                     .type = "XTouch",
                     .input_port = "X-Touch",
                     .output_port = "X-Touch Out"},
          DeviceSpec{.name = "xtouch_ext",
                     .type = "XTouchExt",
                     .input_port = "X-Touch-Ext",
                     .output_port = "X-Touch-Ext",
                     .required = false,
                     .overrides = {{.control = "strip[2].fader",
                                    .inputs = ControlInput::Type::kPress},
                                   {.control = "strip[0].scribble_top",
                                    .inputs = ControlInput::Type::kValue,
                                    .outputs = ControlOutput::Type::kText}}},
          DeviceSpec{.name = "other",
                     .type = "XTouch",
                     .input_port = "In",
                     .output_port = "Out"}));
  ASSERT_EQ(spec.devices.size(), 3);
  EXPECT_EQ(spec.devices[0].line.number, 3);
  EXPECT_EQ(spec.devices[1].line.number, 4);
  ASSERT_EQ(spec.devices[1].overrides.size(), 2);
  EXPECT_EQ(spec.devices[1].overrides[0].line.number, 6);
  EXPECT_EQ(spec.devices[1].overrides[1].line.number, 7);
  EXPECT_EQ(spec.devices[2].line.number, 9);
}

TEST(ReadSurfaceConfigTest, ReadsWidgets) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {
  include xtouch { except strip  except master_fader }
  strip = join(ext.strip, xtouch.strip)
  route_strip = join(ext.strip, xtouch.strip[0..6], xtouch.strip[2..])
  info_strip = xtouch.strip[7]
  transport = {
    include xtouch.transport
    play = xtouch.play
  }
  include other
}
)");
  EXPECT_EQ(
      spec.widgets,
      (WidgetStructDefinition{
          .includes = {{.path = "xtouch",
                        .excluded = {"strip", "master_fader"}},
                       {.path = "other"}},
          .fields = {
              {.name = "strip",
               .widget =
                   WidgetJoinDefinition{.slices = {{.path = "ext.strip"},
                                                   {.path = "xtouch.strip"}}}},
              {.name = "route_strip",
               .widget =
                   WidgetJoinDefinition{
                       .slices =
                           {{.path = "ext.strip"},
                            {.path = "xtouch.strip", .first = 0, .count = 7},
                            {.path = "xtouch.strip", .first = 2}}}},
              {.name = "info_strip", .widget = std::string("xtouch.strip[7]")},
              {.name = "transport",
               .widget =
                   WidgetStructDefinition{
                       .includes = {{.path = "xtouch.transport"}},
                       .fields = {{.name = "play",
                                   .widget = std::string("xtouch.play")}}}},
          }}));
  ASSERT_EQ(spec.widgets.includes.size(), 2);
  EXPECT_EQ(spec.widgets.includes[0].line.number, 4);
  EXPECT_EQ(spec.widgets.includes[1].line.number, 12);
  ASSERT_EQ(spec.widgets.fields.size(), 4);
  EXPECT_EQ(spec.widgets.fields[0].line.number, 5);
  EXPECT_EQ(spec.widgets.fields[3].line.number, 8);
  const auto& transport =
      std::get<WidgetStructDefinition>(spec.widgets.fields[3].widget);
  ASSERT_EQ(transport.fields.size(), 1);
  EXPECT_EQ(transport.includes[0].line.number, 9);
  EXPECT_EQ(transport.fields[0].line.number, 10);
}

TEST(ReadSurfaceConfigTest, ReadsSettings) {
  const std::pair<std::string_view, TrackFilter> kCases[] = {
      {"", TrackFilter::kMcp},
      {"track_filter mixer", TrackFilter::kMcp},
      {"track_filter track_panel", TrackFilter::kTcp},
      {"track_filter all", TrackFilter::kAll},
  };
  for (const auto& [settings, filter] : kCases) {
    SCOPED_TRACE(settings);
    EXPECT_EQ(ReadValid(absl::StrCat(kVersion, kEmptyBlocks, "settings { ",
                                     settings, " }"))
                  .track_filter,
              filter);
  }
}

TEST(ReadSurfaceConfigTest, ReadsComponents) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
components {
  modifier mod:send_hold
  enumerated_value user:step(values = [measure, beat, marker], initial = beat)
  enumerated_value user:mode(values = [track, send_receive])
  track_reference user:folder(fallback = state:master_track,
                              follow = state:last_touched_track)
  track_reference user:current_track()
  track_anchor user:anchor(action = rec_arm, modifier = mod:send_hold)
  track_pick user:pick(reference = user:folder, source = state:selected_track)
}
components {
  modifier mod:select_anchor
}
)");
  EXPECT_THAT(
      spec.components,
      ElementsAre(ModifierSpec{.name = "mod:send_hold"},
                  EnumeratedValueSpec{.name = "user:step",
                                      .values = {"measure", "beat", "marker"},
                                      .initial = "beat"},
                  EnumeratedValueSpec{.name = "user:mode",
                                      .values = {"track", "send_receive"}},
                  TrackReferenceSpec{.name = "user:folder",
                                     .fallback = "state:master_track",
                                     .follow = "state:last_touched_track"},
                  TrackReferenceSpec{.name = "user:current_track"},
                  TrackAnchorSpec{.name = "user:anchor",
                                  .action = TrackBoolProperty::kRecArm,
                                  .modifier = "mod:send_hold"},
                  TrackPickSpec{.name = "user:pick",
                                .reference = "user:folder",
                                .source = "state:selected_track"},
                  ModifierSpec{.name = "mod:select_anchor"}));
  ASSERT_EQ(spec.components.size(), 8);
  EXPECT_EQ(GetComponentLine(spec.components[0]).number, 5);
  EXPECT_EQ(GetComponentLine(spec.components[3]).number, 8);
  EXPECT_EQ(GetComponentLine(spec.components[7]).number, 15);
}

// Returns a value for a component parameter of the kind, the first or the
// second of two that differ.
std::string GetParamValue(const ParamInfo& param, bool second) {
  switch (param.kind) {
    case ParamKind::kNewName:
    case ParamKind::kValueName:
      return second ? "b" : "a";
    case ParamKind::kNewNameList:
      return second ? "[b]" : "[a, b]";
    case ParamKind::kTrackReference:
      return second ? "user:b" : "user:a";
    case ParamKind::kModifier:
      return second ? "mod:b" : "mod:a";
    case ParamKind::kChoice:
      return std::string(second ? param.choices.back() : param.choices.front());
  }
  return {};
}

// Returns the config of one component of the kind, with every parameter, each
// the first of its values except `second`.
std::string GetComponentConfig(const ComponentKindInfo& kind,
                               std::string_view second) {
  std::vector<std::string> params;
  for (const ParamInfo& param : kind.params) {
    params.push_back(absl::StrCat(param.name, " = ",
                                  GetParamValue(param, param.name == second)));
  }
  return absl::StrCat(kVersion, kEmptyBlocks, "components { ", kind.name, " ",
                      kind.name_space, "x(", absl::StrJoin(params, ", "),
                      ") }");
}

// So that a kind added to the catalog is read, with each of its parameters.
TEST(ReadSurfaceConfigTest, ReadsEveryParameterOfEveryComponentKind) {
  for (const ComponentKindInfo& kind : GetComponentKinds()) {
    SCOPED_TRACE(kind.name);
    const SurfaceSpec spec = ReadValid(GetComponentConfig(kind, {}));
    ASSERT_EQ(spec.components.size(), 1);
    EXPECT_EQ(GetComponentKind(spec.components[0]).name, kind.name);
    EXPECT_EQ(GetComponentName(spec.components[0]),
              absl::StrCat(kind.name_space, "x"));
    for (const ParamInfo& param : kind.params) {
      SCOPED_TRACE(param.name);
      EXPECT_NE(ReadValid(GetComponentConfig(kind, param.name)), spec);
    }
  }
}

//------------------------------------------------------------------------------
// Mappings
//------------------------------------------------------------------------------

// Returns the mappings of a config of one "mappings" block, with `body`.
std::vector<MappingSpec> ReadMappings(std::string_view body) {
  return ReadValid(
             absl::StrCat(kVersion, kEmptyBlocks, "mappings {\n", body, "\n}"))
      .root.mappings;
}

// Returns the mapping a config of one "mappings" block with it has.
MappingSpec ReadMapping(std::string_view text) {
  std::vector<MappingSpec> mappings = ReadMappings(text);
  EXPECT_EQ(mappings.size(), 1);
  return mappings.empty() ? MappingSpec() : std::move(mappings[0]);
}

TEST(ReadSurfaceConfigTest, ReadsEachArrow) {
  EXPECT_THAT(ReadMappings("a -> cmd:1\n"
                           "b <- state:can_redo\n"
                           "c.d[2] <-> track:ui_mute"),
              ElementsAre(MappingSpec{.property = "cmd:1",
                                      .control = "a",
                                      .type = ViewMapping::kReadControl},
                          MappingSpec{.property = "state:can_redo",
                                      .control = "b",
                                      .type = ViewMapping::kWriteControl},
                          MappingSpec{.property = "track:ui_mute",
                                      .control = "c.d[2]",
                                      .type = ViewMapping::kReadWriteControl}));
}

TEST(ReadSurfaceConfigTest, ReadsHowAControlIsRead) {
  using Behavior = InputConfig::PressBehavior;
  const std::pair<std::string_view, ViewMapping::ReadConfig> kCases[] = {
      {"", {}},
      {"[press]", {.input_type = ControlInput::Type::kPress}},
      {"[value]", {.input_type = ControlInput::Type::kValue}},
      {"[delta]", {.input_type = ControlInput::Type::kDelta}},
      {"[long]", {.press_behavior = Behavior::kLongPress}},
      {"[double]", {.press_behavior = Behavior::kDoublePress}},
      {"[tap]", {.press_behavior = Behavior::kTap}},
      {"[held]", {.press_release = true}},
      {"[touch, held]", {.press_release = true, .touch = true}},
      {"[mod:a, mod:b]", {.required_modifiers = {"mod:a", "mod:b"}}},
      {"[long, held, mod:a]",
       {.required_modifiers = {"mod:a"},
        .press_behavior = Behavior::kLongPress,
        .press_release = true}},
  };
  for (const auto& [how, read] : kCases) {
    SCOPED_TRACE(how);
    EXPECT_EQ(ReadMapping(absl::StrCat("a.b ", how, " -> cmd:1")).config.read,
              read);
  }
}

TEST(ReadSurfaceConfigTest, ReadsFixedValuesOfEachType) {
  const std::pair<std::string_view, FixedValue> kCases[] = {
      {"true", {.type = ViewProperty::Type::kToggle, .value = true}},
      {"false", {.type = ViewProperty::Type::kToggle, .value = false}},
      {"1", {.type = ViewProperty::Type::kNormalized, .value = 1.0}},
      {"0.25", {.type = ViewProperty::Type::kNormalized, .value = 0.25}},
      {"\"Hi\"",
       {.type = ViewProperty::Type::kText, .value = std::string("Hi")}},
      {"rgb(255, 0, 16)",
       {.type = ViewProperty::Type::kColor, .value = Color{255, 0, 16}}},
  };
  for (const auto& [text, value] : kCases) {
    SCOPED_TRACE(text);
    EXPECT_EQ(ReadMapping(absl::StrCat("a <- ", text)).property,
              (std::variant<std::string, FixedValue>(value)));
  }
}

TEST(ReadSurfaceConfigTest, ReadsOptions) {
  using Value = ViewProperty::Value;
  const std::pair<std::string_view, ViewMapping::Config> kCases[] = {
      {"{ set 0.0 }",
       {.read = {.property_min = Value(0.0), .property_max = Value(0.0)}}},
      {"{ set -0.5 }",
       {.read = {.property_min = Value(-0.5), .property_max = Value(-0.5)}}},
      {"{ set send_receive }",
       {.read = {.property_min = Value(std::string("send_receive")),
                 .property_max = Value(std::string("send_receive"))}}},
      {"{ range 1 .. 3 }",
       {.read = {.property_min = Value(1), .property_max = Value(3)}}},
      {"{ range \"a\" .. \"b\" }",
       {.read = {.property_min = Value(std::string("a")),
                 .property_max = Value(std::string("b"))}}},
      {"{ range rgb(1, 2, 3) .. rgb(4, 5, 6) }",
       {.read = {.property_min = Value(Color{1, 2, 3}),
                 .property_max = Value(Color{4, 5, 6})}}},
      {"{ toggle measure .. beat }",
       {.read = {.property_min = Value(std::string("measure")),
                 .property_max = Value(std::string("beat")),
                 .press_toggles = true}}},
      {"{ mode blink }", {.write = {.mode = "blink"}}},
      {"{ set true mode blink }",
       {.read = {.property_min = Value(true), .property_max = Value(true)},
        .write = {.mode = "blink"}}},
  };
  for (const auto& [options, config] : kCases) {
    SCOPED_TRACE(options);
    EXPECT_EQ(ReadMapping(absl::StrCat("a <-> user:x ", options)).config,
              config);
  }
}

TEST(ReadSurfaceConfigTest, AWhenAroundAModeIsAModeOverride) {
  EXPECT_EQ(
      ReadMapping(R"(strip.pot <-> track:ui_pan {
    mode center
    when track:exists = false { mode off }
    when user:step = beat { mode blink }
    when track:is_folder { mode center_edges }
    when user:x = 2 { }
  })")
          .config.write,
      (ViewMapping::WriteConfig{
          .mode = "center",
          .mode_overrides = {
              {.property = "track:exists", .value_to_mode = {{false, "off"}}},
              {.property = "user:step",
               .value_to_mode = {{std::string("beat"), "blink"}}},
              {.property = "track:is_folder",
               .value_to_mode = {{true, "center_edges"}}}}}));
}

TEST(ReadSurfaceConfigTest, AWhenAroundMappingsIsEachOnesCondition) {
  EXPECT_THAT(
      ReadMappings(R"(a -> cmd:1
when user:step = measure { b -> cmd:2 }
when state:any_track_solo {
  c -> cmd:3
  d <- true
}
when user:x = 0.5 { e -> cmd:4 }
when user:x = "x" { f -> cmd:5 }
)"),
      ElementsAre(
          MappingSpec{.property = "cmd:1",
                      .control = "a",
                      .type = ViewMapping::kReadControl},
          MappingSpec{
              .property = "cmd:2",
              .control = "b",
              .type = ViewMapping::kReadControl,
              .config = {.condition = {{.property = "user:step",
                                        .value = std::string("measure")}}}},
          MappingSpec{
              .property = "cmd:3",
              .control = "c",
              .type = ViewMapping::kReadControl,
              .config = {.condition = {{.property = "state:any_track_solo"}}}},
          MappingSpec{
              .property = FixedValue{.value = true},
              .control = "d",
              .type = ViewMapping::kWriteControl,
              .config = {.condition = {{.property = "state:any_track_solo"}}}},
          MappingSpec{
              .property = "cmd:4",
              .control = "e",
              .type = ViewMapping::kReadControl,
              .config = {.condition = {{.property = "user:x", .value = 0.5}}}},
          MappingSpec{.property = "cmd:5",
                      .control = "f",
                      .type = ViewMapping::kReadControl,
                      .config = {.condition = {{.property = "user:x",
                                                .value = std::string("x")}}}}));
}

TEST(ReadSurfaceConfigTest, MappingsHaveTheirLines) {
  const std::vector<MappingSpec> mappings = ReadMappings(R"(a -> cmd:1
when user:x {

  b -> cmd:2
}
c
  -> cmd:3)");
  ASSERT_EQ(mappings.size(), 3);
  EXPECT_EQ(mappings[0].line.number, 5);
  EXPECT_EQ(mappings[1].line.number, 8);
  EXPECT_EQ(mappings[2].line.number, 10);
}

//------------------------------------------------------------------------------
// Templates and uses
//------------------------------------------------------------------------------

TEST(ReadSurfaceConfigTest, ReadsTemplates) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
mappings list_navigation {
  channel_left -> view:scroll_prev
}
mappings button(widget button, property command, property light = command,
                property other = state:can_redo) {
  button -> command
  when state:x { button <- light }
  use list_navigation
}
)");
  EXPECT_THAT(
      spec.templates,
      ElementsAre(
          TemplateSpec{.name = "list_navigation",
                       .mappings = {{.property = "view:scroll_prev",
                                     .control = "channel_left",
                                     .type = ViewMapping::kReadControl}}},
          TemplateSpec{
              .name = "button",
              .widget_params = {"button"},
              .property_params = {{.name = "command"},
                                  {.name = "light", .default_value = "command"},
                                  {.name = "other",
                                   .default_value = "state:can_redo"}},
              .mappings = {{.property = "command",
                            .control = "button",
                            .type = ViewMapping::kReadControl},
                           {.property = "light",
                            .control = "button",
                            .type = ViewMapping::kWriteControl,
                            .config = {.condition = {{.property =
                                                          "state:x"}}}}},
              .uses = {{.name = "list_navigation"}}}));
  ASSERT_EQ(spec.templates.size(), 2);
  EXPECT_EQ(spec.templates[0].line.number, 4);
  EXPECT_EQ(spec.templates[1].line.number, 7);
  EXPECT_EQ(spec.templates[1].mappings[1].line.number, 10);
  EXPECT_EQ(spec.templates[1].uses[0].line.number, 11);
}

TEST(ReadSurfaceConfigTest, AUseSortsItsArgumentsByItsTemplate) {
  // The uses are before the template, as order doesn't matter, and an
  // argument a template doesn't have goes by how it is written. One its
  // parameter can't be is left to checking.
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
mappings {
  use button(button = auto_trim, command = cmd:40400, light = mod:x)
  use button(button = strip[2].select, command = state:a, other = b,
             another = mod:y)
  use unknown(a = b.c, d = user:e)
  use button(button = cmd:1, command = a.b)
}
mappings button(widget button, property command, property light = command) {
  use inner(widget = button, property = command)
}
mappings inner(widget widget, property property) {
  widget -> property
}
)");
  EXPECT_THAT(
      spec.root.uses,
      ElementsAre(
          TemplateUse{
              .name = "button",
              .widgets = {{"button", "auto_trim"}},
              .properties = {{"command", "cmd:40400"}, {"light", "mod:x"}}},
          TemplateUse{
              .name = "button",
              .widgets = {{"button", "strip[2].select"}, {"other", "b"}},
              .properties = {{"command", "state:a"}, {"another", "mod:y"}}},
          TemplateUse{.name = "unknown",
                      .widgets = {{"a", "b.c"}},
                      .properties = {{"d", "user:e"}}},
          TemplateUse{.name = "button",
                      .widgets = {{"button", "cmd:1"}},
                      .properties = {{"command", "a.b"}}}));
  ASSERT_EQ(spec.templates.size(), 2);
  EXPECT_THAT(
      spec.templates[0].uses,
      ElementsAre(TemplateUse{.name = "inner",
                              .widgets = {{"widget", "button"}},
                              .properties = {{"property", "command"}}}));
  ASSERT_EQ(spec.root.uses.size(), 4);
  EXPECT_EQ(spec.root.uses[1].line.number, 6);
}

//------------------------------------------------------------------------------
// Views
//------------------------------------------------------------------------------

TEST(ReadSurfaceConfigTest, ReadsViews) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
view master(subject = state:master_track) {
  mappings { master_fader <-> track:volume }
}
view folder(subject = user:folder,
            list = child_tracks(reveal = user:current_track, bank = 8)) {
  view strip in xtouch.strip {
    components { track_pick user:pick(reference = user:current_track) }
    mappings { strip.select -> track:ui_selected }
    components { track_anchor user:anchor(action = mute) }
    mappings { use strip(strip = strip) }
  }
}
view routes(list = routes) {}
view sends(list = routes(type = sends)) {}
view receives(list = routes(bank = 4, type = receives)) {}
view tracks(list = child_tracks) {}
)");
  EXPECT_THAT(
      spec.root.children,
      ElementsAre(
          ViewSpec{.name = "master",
                   .config = {.subject =
                                  View::ReferenceSubject{"state:master_track"}},
                   .mappings = {{.property = "track:volume",
                                 .control = "master_fader",
                                 .type = ViewMapping::kReadWriteControl}}},
          ViewSpec{
              .name = "folder",
              .config = {.subject = View::ReferenceSubject{"user:folder"},
                         .list = View::ListConfig{.items =
                                                      View::ChildTracks{
                                                          "user:current_track"},
                                                  .bank_size = 8}},
              .children = {ViewSpec{
                  .config = {.subject = View::ListItemSubject{}},
                  .repeat =
                      RepeatSpec{.array = "xtouch.strip", .element = "strip"},
                  .components =
                      {TrackPickSpec{.name = "user:pick",
                                     .reference = "user:current_track"},
                       TrackAnchorSpec{.name = "user:anchor",
                                       .action = TrackBoolProperty::kMute}},
                  .uses = {{.name = "strip", .widgets = {{"strip", "strip"}}}},
                  .mappings = {{.property = "track:ui_selected",
                                .control = "strip.select",
                                .type = ViewMapping::kReadControl}}}}},
          ViewSpec{
              .name = "routes",
              .config = {.list = View::ListConfig{.items = View::Routes{}}}},
          ViewSpec{
              .name = "sends",
              .config = {.list = View::ListConfig{.items =
                                                      View::Routes{
                                                          View::RouteTypeRule::
                                                              kSends}}}},
          ViewSpec{
              .name = "receives",
              .config = {.list = View::ListConfig{.items =
                                                      View::Routes{
                                                          View::RouteTypeRule::
                                                              kReceives},
                                                  .bank_size = 4}}},
          ViewSpec{.name = "tracks",
                   .config = {.list = View::ListConfig{
                                  .items = View::ChildTracks{}}}}));
  EXPECT_THAT(spec.components, IsEmpty());
  ASSERT_EQ(spec.root.children.size(), 6);
  EXPECT_EQ(spec.root.children[1].line.number, 7);
  ASSERT_EQ(spec.root.children[1].children.size(), 1);
  const ViewSpec& strip = spec.root.children[1].children[0];
  EXPECT_EQ(strip.line.number, 9);
  ASSERT_EQ(strip.components.size(), 2);
  EXPECT_EQ(GetComponentLine(strip.components[1]).number, 12);
}

TEST(ReadSurfaceConfigTest, AWhenAroundViewsIsEachOnesCondition) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
when user:mode = track {
  view a {
    when state:x { view b {} }
  }
  view c(subject = user:folder) {}
}
)");
  const ViewCondition::Config track = {.property = "user:mode",
                                       .value = std::string("track")};
  EXPECT_THAT(
      spec.root.children,
      ElementsAre(
          ViewSpec{.name = "a",
                   .config = {.condition = track},
                   .children = {ViewSpec{
                       .name = "b",
                       .config = {.condition = {{.property = "state:x"}}}}}},
          ViewSpec{
              .name = "c",
              .config = {.condition = track,
                         .subject = View::ReferenceSubject{"user:folder"}}}));
}

// A config's views, components, mappings, and uses, all in one place.
TEST(ReadSurfaceConfigTest, ReadsTheTopLevelAsTheRootView) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
mappings { a -> cmd:1 }
components { modifier mod:a }
view b {}
widgets {}
mappings { use c }
mappings c {}
)");
  EXPECT_EQ(
      spec,
      (SurfaceSpec{.components = {ModifierSpec{.name = "mod:a"}},
                   .templates = {{.name = "c"}},
                   .root = {.uses = {{.name = "c"}},
                            .mappings = {{.property = "cmd:1",
                                          .control = "a",
                                          .type = ViewMapping::kReadControl}},
                            .children = {ViewSpec{.name = "b"}}}}));
}

//------------------------------------------------------------------------------
// Errors
//------------------------------------------------------------------------------

TEST(ReadSurfaceConfigTest, CheckingErrorsStartWithTheirLines) {
  // Reading leaves parameters left out to checking, which knows where each
  // component is, apart from a required choice.
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {
  xtouch = XTouh(input = "In", output = "Out")
  portless = XTouchExt()
}
widgets {}
components {
  modifier user:shift
  enumerated_value user:step
  track_pick user:pick()
}
)");
  std::vector<std::string> errors;
  EXPECT_FALSE(ValidateSurfaceSpec(spec, &errors));
  EXPECT_THAT(errors,
              ElementsAre("line 3: xtouch: type \"XTouh\" isn't a device type",
                          "line 4: portless: has no input port",
                          "line 4: portless: has no output port",
                          "line 8: modifier name \"user:shift\" isn't \"mod:\" "
                          "followed by a C identifier",
                          "line 9: user:step: has no values",
                          "line 10: user:pick: has no reference",
                          "line 10: user:pick: has no source"));
}

TEST(ReadSurfaceConfigTest, ASyntaxErrorStopsReading) {
  EXPECT_THAT(ReadErrors(R"(jprsurf 1
devices { xtouch = XTouch(input = "In", output = "Out", other = 3) }
widgets {
  strip = join(xtouch.strip
}
)"),
              ElementsAre("line 5: Parse error at \"}\": Expected ')'"));
  EXPECT_THAT(ReadErrors(absl::StrCat(kVersion, kEmptyBlocks, "views {}")),
              ElementsAre("line 4: Parse error at \"views\": Expected a "
                          "statement"));
}

TEST(ReadSurfaceConfigTest, TheVersionIsFirst) {
  EXPECT_THAT(ReadErrors(kEmptyBlocks),
              ElementsAre("line 1: Parse error at \"devices\": Expected the "
                          "version first, such as \"jprsurf 1\""));
  EXPECT_THAT(ReadErrors(""),
              ElementsAre("line 1: Parse error at \"\": Expected the version "
                          "first, such as \"jprsurf 1\""));
}

TEST(ReadSurfaceConfigTest, AnUnknownVersionStopsReading) {
  EXPECT_THAT(ReadErrors("\njprsurf 2\nanything {"),
              ElementsAre("line 2: version 2 isn't known: JPRSurf reads "
                          "version 1"));
}

TEST(ReadSurfaceConfigTest, DevicesAndWidgetsAreGivenOnce) {
  EXPECT_THAT(ReadErrors(kVersion),
              ElementsAre("the config has no \"devices\" block",
                          "the config has no \"widgets\" block"));
  EXPECT_THAT(
      ReadErrors(absl::StrCat(kVersion, kEmptyBlocks, "settings {}\n",
                              kEmptyBlocks, "settings {}\n")),
      ElementsAre(
          "line 5: the config already has a \"devices\" block, on line 2",
          "line 6: the config already has a \"widgets\" block, on line 3",
          "line 7: the config already has a \"settings\" block, on line 4"));
}

TEST(ReadSurfaceConfigTest, DeviceErrors) {
  EXPECT_THAT(
      ReadErrors(R"(jprsurf 1
devices {
  a = XTouch(input = "In", output = "Out", outptu = "Out")
  b = XTouch(input = "In", input = "In")
  c = XTouch(input = In, output = "Out", optional = yes) {
    without strip[0].fader [press, presss]
  }
}
widgets {}
)"),
      ElementsAre("line 3: a: \"outptu\" isn't a parameter",
                  "line 4: b: \"input\" is given twice",
                  "line 5: c: input isn't a string",
                  "line 5: c: optional \"yes\" isn't one of: true, false",
                  "line 6: c: strip[0].fader: \"presss\" isn't an input or an "
                  "output"));
}

// Checking reports a slice that ends before it starts, as it has no elements.
TEST(ReadSurfaceConfigTest, ASliceThatEndsBeforeItStartsIsEmpty) {
  const SurfaceSpec spec = ReadValid(absl::StrCat(
      kVersion, "devices {}\nwidgets { strip = join(xtouch.strip[3..2]) }"));
  ASSERT_EQ(spec.widgets.fields.size(), 1);
  EXPECT_EQ(
      spec.widgets.fields[0].widget,
      (std::variant<std::string, WidgetStructDefinition, WidgetJoinDefinition>(
          WidgetJoinDefinition{
              .slices = {{.path = "xtouch.strip", .first = 3, .count = 0}}})));
}

TEST(ReadSurfaceConfigTest, SettingErrors) {
  EXPECT_THAT(
      ReadErrors(absl::StrCat(kVersion, kEmptyBlocks, R"(settings {
  track_filter mixer
  track_filter all
  track_filtre all
  }
settings {}
)")),
      ElementsAre("line 6: \"track_filter\" is given twice",
                  "line 7: \"track_filtre\" isn't a setting",
                  "line 9: the config already has a \"settings\" block, on "
                  "line 4"));
  EXPECT_THAT(ReadErrors(absl::StrCat(kVersion, kEmptyBlocks,
                                      "settings { track_filter mcp }")),
              ElementsAre("line 4: track_filter \"mcp\" isn't one of: mixer, "
                          "track_panel, all"));
  EXPECT_THAT(ReadErrors(absl::StrCat(kVersion, kEmptyBlocks,
                                      "settings { track_filter \"all\" }")),
              ElementsAre("line 4: track_filter isn't a name"));
}

TEST(ReadSurfaceConfigTest, ComponentErrors) {
  EXPECT_THAT(
      ReadErrors(R"(jprsurf 1
devices {}
widgets {}
components {
  modifer mod:a
  modifier mod:b(values = [a])
  track_anchor user:c
  enumerated_value user:d(values = a, initial = [a])
  enumerated_value user:e(values = [a, "b"], values = [a])
  track_reference user:f(fallback = folder)
  track_anchor user:g(action = muted)
}
)"),
      ElementsAre("line 5: mod:a: \"modifer\" isn't a component kind",
                  "line 6: mod:b: \"values\" isn't a parameter",
                  "line 7: user:c: has no action",
                  "line 8: user:d: values isn't a list of names",
                  "line 8: user:d: initial isn't a name",
                  "line 9: user:e: values isn't a list of names",
                  "line 9: user:e: \"values\" is given twice",
                  "line 10: user:f: fallback isn't a name with a namespace",
                  "line 11: user:g: action \"muted\" isn't one of: selected, "
                  "mute, solo, rec_arm"));
}

TEST(ReadSurfaceConfigTest, StatementErrors) {
  EXPECT_THAT(
      ReadErrors(R"(jprsurf 1
devices {}
widgets {}
view a {
  devices {}
  settings {}
  mappings b {}
  when user:x {
    components {}
    mappings {}
    when user:y {}
    view c {}
  }
}
when user:x {
  devices {}
}
)"),
      ElementsAre(
          "line 5: \"devices\" is only at the top level",
          "line 6: \"settings\" is only at the top level",
          "line 7: named mappings are only at the top level",
          "line 9: \"components\" can't be in a \"when\" here, which holds "
          "only views",
          "line 10: \"mappings\" can't be in a \"when\" here, which holds "
          "only views",
          "line 11: a \"when\" can't be in another \"when\"",
          "line 16: \"devices\" can't be in a \"when\" here, which holds only "
          "views"));
}

TEST(ReadSurfaceConfigTest, MappingErrors) {
  EXPECT_THAT(
      ReadErrors(R"(jprsurf 1
devices {}
widgets {}
mappings {
  a [held, held, long, tap, value, press, mod:a, mod:a, hold] -> cmd:1
  b <- rgb(256, 0, 0)
  c -> user:x { set 1 range 0 .. 1 mode a mode b }
  when user:x {
    when user:y { d -> cmd:1 }
    use t
  }
  e <-> user:x {
    when user:y { set 1 mode a mode b when user:z { mode c } }
  }
}
)"),
      ElementsAre(
          "line 5: /: a: \"held\" is given twice",
          "line 5: /: a: has more than one press behavior",
          "line 5: /: a: has more than one input",
          "line 5: /: a: \"hold\" isn't an input, a press behavior, \"held\", "
          "\"touch\", or a modifier",
          "line 6: /: b: rgb(256, 0, 0) has a value over 255",
          "line 7: /: c: has more than one of \"set\", \"range\", and "
          "\"toggle\"",
          "line 7: /: c: \"mode\" is given twice",
          "line 9: a \"when\" can't be in another \"when\"",
          "line 10: a \"use\" can't be in a \"when\", as a use has no "
          "condition",
          "line 13: /: e: \"set\" can't be in a \"when\": only a mode can",
          "line 13: /: e: \"mode\" is given twice",
          "line 13: /: e: a \"when\" can't be in another \"when\""));
}

TEST(ReadSurfaceConfigTest, AUseGivesEachArgumentOnce) {
  EXPECT_THAT(ReadErrors(R"(jprsurf 1
devices {}
widgets {}
mappings t(widget w, property p) {}
view v {
  mappings { use t(p = mod:x, w = a, p = mod:y, w = b) }
}
)"),
              ElementsAre("line 6: /v: uses t with \"p\" given twice",
                          "line 6: /v: uses t with \"w\" given twice"));
}

TEST(ReadSurfaceConfigTest, ViewErrors) {
  EXPECT_THAT(
      ReadErrors(R"(jprsurf 1
devices {}
widgets {}
view a(subject = folder, lists = routes) {}
view b(list = tracks) {}
view c(list = routes(reveal = user:x, bank = "8", type = all)) {}
view d(list = child_tracks(type = sends)) {
  view e(list = [a]) {}
}
)"),
      ElementsAre(
          "line 4: /a: subject isn't a name with a namespace",
          "line 4: /a: \"lists\" isn't a parameter",
          "line 5: /b: list \"tracks\" isn't one of: child_tracks, routes",
          "line 6: /c: \"reveal\" isn't a parameter",
          "line 6: /c: bank isn't an integer",
          "line 6: /c: type \"all\" isn't one of: sends, receives",
          "line 7: /d: \"type\" isn't a parameter",
          "line 8: /d/e: list isn't a name, with or without parameters"));
}

TEST(ReadSurfaceConfigTest, CheckingErrorsAreAtTheirMappingsAndUsesLines) {
  const SurfaceSpec spec = ReadValid(R"(jprsurf 1
devices {}
widgets {}
mappings t(widget w) {
  w -> cmd:1
}
view v {
  mappings {
    nothing -> cmd:1
    use t(w = missing)
  }
}
)");
  std::vector<std::string> errors;
  EXPECT_FALSE(ValidateSurfaceSpec(spec, &errors));
  EXPECT_THAT(errors, ElementsAre("line 9: /v: nothing: names nothing",
                                  "line 10: /v: uses t with widget \"w\" as "
                                  "\"missing\", which names nothing"));
}

}  // namespace
}  // namespace jpr
