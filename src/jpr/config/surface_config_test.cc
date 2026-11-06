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

}  // namespace
}  // namespace jpr
