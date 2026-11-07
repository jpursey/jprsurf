// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/surface_spec.h"

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control_input.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::StartsWith;

using Param = std::tuple<std::string_view, ParamKind, ParamUse>;

// Returns a spec with an X-Touch, whose widgets are its catalog's, and nothing
// else.
SurfaceSpec GetXTouchSpec() {
  return {.devices = {{.name = "xtouch",
                       .type = "XTouch",
                       .input_port = "X-Touch",
                       .output_port = "X-Touch"}},
          .widgets = {.includes = {{.path = "xtouch"}}}};
}

// Returns a view bound to the reference.
ViewSpec BoundView(std::string name, std::string reference) {
  return {.name = std::move(name),
          .config = {.subject = View::ReferenceSubject{std::move(reference)}}};
}

// Returns a view with a track subject, and a list of its child tracks, which
// reveals `reveal` if it isn't empty.
ViewSpec ChildTracksView(std::string name,
                         std::string reference = "state:master_track",
                         std::string reveal = "") {
  ViewSpec view = BoundView(std::move(name), std::move(reference));
  view.config.list =
      View::ListConfig{.items = View::ChildTracks{.reveal = std::move(reveal)}};
  return view;
}

// Returns a view repeated over the array, showing its parent's list.
ViewSpec RepeatedView(std::string array, std::string element) {
  return {.config = {.subject = View::ListItemSubject{}},
          .repeat = RepeatSpec{.array = std::move(array),
                               .element = std::move(element)}};
}

// Returns the view with the child added.
ViewSpec WithChild(ViewSpec view, ViewSpec child) {
  view.children.push_back(std::move(child));
  return view;
}

// Returns a child tracks view, as ChildTracksView() does, with a view repeated
// over the X-Touch's strips.
ViewSpec ListView(std::string name,
                  std::string reference = "state:master_track",
                  std::string reveal = "") {
  return WithChild(
      ChildTracksView(std::move(name), std::move(reference), std::move(reveal)),
      RepeatedView("strip", "strip"));
}

// Returns the errors validating the spec found, expecting it to be valid
// exactly when there are none.
std::vector<std::string> Validate(const SurfaceSpec& spec) {
  std::vector<std::string> errors;
  const bool valid = ValidateSurfaceSpec(spec, &errors);
  EXPECT_EQ(valid, errors.empty());
  EXPECT_EQ(ValidateSurfaceSpec(spec, nullptr), valid);
  return errors;
}

// Returns the parameters of the spec's kind.
template <typename Spec>
std::vector<Param> GetParams() {
  std::vector<Param> params;
  for (const ParamInfo& param : GetComponentKind(Spec{}).params) {
    params.emplace_back(param.name, param.kind, param.use);
  }
  return params;
}

TEST(SurfaceSpecTest, ComponentKindsAreTheSpecsAlternatives) {
  EXPECT_EQ(std::ssize(GetComponentKinds()),
            std::variant_size_v<ComponentSpec>);
  EXPECT_EQ(GetComponentKind(ModifierSpec{}).name, "modifier");
  EXPECT_EQ(GetComponentKind(EnumeratedValueSpec{}).name, "enumerated_value");
  EXPECT_EQ(GetComponentKind(TrackReferenceSpec{}).name, "track_reference");
  EXPECT_EQ(GetComponentKind(TrackAnchorSpec{}).name, "track_anchor");
  EXPECT_EQ(GetComponentKind(TrackPickSpec{}).name, "track_pick");
  EXPECT_EQ(GetComponentName(TrackPickSpec{.name = "user:pick"}), "user:pick");
}

// A spec's fields are its name, its kind's parameters in order, then its line:
// binding them doesn't compile once a field is added, until its parameter is
// added here. The kinds and uses are what the tests below show checking
// enforces.
TEST(SurfaceSpecTest, EachSpecHasItsKindsParameters) {
  [[maybe_unused]] auto [modifier_name, modifier_line] = ModifierSpec{};
  EXPECT_THAT(GetParams<ModifierSpec>(), IsEmpty());

  [[maybe_unused]] auto [value_name, values, initial, value_line] =
      EnumeratedValueSpec{};
  EXPECT_THAT(
      GetParams<EnumeratedValueSpec>(),
      ElementsAre(
          Param("values", ParamKind::kNewNameList, ParamUse::kRequired),
          Param("initial", ParamKind::kValueName, ParamUse::kOptional)));

  [[maybe_unused]] auto [reference_name, fallback, follow, reference_line] =
      TrackReferenceSpec{};
  EXPECT_THAT(
      GetParams<TrackReferenceSpec>(),
      ElementsAre(
          Param("fallback", ParamKind::kTrackReference, ParamUse::kOptional),
          Param("follow", ParamKind::kTrackReference, ParamUse::kOptional)));

  [[maybe_unused]] auto [anchor_name, action, modifier, anchor_line] =
      TrackAnchorSpec{};
  EXPECT_THAT(
      GetParams<TrackAnchorSpec>(),
      ElementsAre(
          Param("action", ParamKind::kChoice, ParamUse::kRequired),
          Param("modifier", ParamKind::kModifier, ParamUse::kOptional)));

  [[maybe_unused]] auto [pick_name, reference, source, pick_line] =
      TrackPickSpec{};
  EXPECT_THAT(GetParams<TrackPickSpec>(),
              ElementsAre(Param("reference", ParamKind::kTrackReference,
                                ParamUse::kRequired),
                          Param("source", ParamKind::kTrackReference,
                                ParamUse::kTopLevelOnly)));
}

TEST(SurfaceSpecTest, AnEmptySpecIsValid) {
  EXPECT_THAT(Validate({}), IsEmpty());
}

// A spec with each kind of thing checking accepts, as the X-Touch example
// config uses them.
TEST(SurfaceSpecTest, AValidSpecHasNoErrors) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {
      ModifierSpec{"mod:send_hold"},
      EnumeratedValueSpec{
          "user:surface_mode", {"track", "send_receive"}, "send_receive"},
      TrackReferenceSpec{"user:folder", "state:master_track"},
      TrackReferenceSpec{"user:current_track", "user:folder",
                         "state:last_touched_track"},
      TrackPickSpec{"user:pick_selected_track", "user:current_track",
                    "state:selected_track"},
  };
  // A template's paths start with its widget parameters, and its property
  // parameters stand for the names they are given.
  spec.templates = {
      {.name = "track_strip",
       .widget_params = {"strip"},
       .property_params = {{.name = "anchor"}},
       .mappings =
           {{.property = "track:mute",
             .control = "strip.mute",
             .type = ViewMapping::kReadWriteControl},
            {.property = "anchor",
             .control = "strip.select",
             .type = ViewMapping::kReadControl,
             .config = {.read = {.press_release = true}}},
            {.property = "track:pan",
             .control = "strip.pot",
             .type = ViewMapping::kReadWriteControl,
             .config = {.write = {.mode = "center",
                                  .mode_overrides = {{"track:exists",
                                                      {{false, "off"}}}}}}},
            {.property = "track:pan",
             .control = "strip.pot_button",
             .type = ViewMapping::kReadControl,
             .config = {.read = {.property_min = 0.0, .property_max = 0.0}}},
            {.property = "track:volume",
             .control = "strip.fader",
             .type = ViewMapping::kReadWriteControl},
            {.property = "track:volume_touched",
             .control = "strip.fader",
             .type = ViewMapping::kReadControl,
             .config = {.read = {.press_release = true, .touch = true}}},
            {.property = "track:name",
             .control = "strip.scribble_top",
             .type = ViewMapping::kWriteControl}}},
      {.name = "anchored_strip",
       .widget_params = {"strip"},
       .property_params = {{.name = "anchor"}},
       .uses = {{.name = "track_strip",
                 .widgets = {{"strip", "strip"}},
                 .properties = {{"anchor", "anchor"}}}}},
  };

  // Commands are known only to REAPER, so only their ids are checked. A fixed
  // value is a constant, which can only be written.
  spec.root.mappings = {
      {.property = "cmd:40044",
       .control = "play",
       .type = ViewMapping::kReadWriteControl},
      {.property = "cmd:_SWS_ABOUT",
       .control = "stop",
       .type = ViewMapping::kReadControl,
       .config = {.read = {.required_modifiers = {"mod:send_hold"}}}},
      {.property = "state:timeline_position",
       .control = "timecode",
       .type = ViewMapping::kWriteControl,
       .config = {.write = {.mode = "beats"}}},
      {.property = "user:surface_mode",
       .control = "assign_track",
       .type = ViewMapping::kReadWriteControl,
       .config = {.read = {.property_min = std::string("track"),
                           .property_max = 0},
                  .condition =
                      ViewCondition::Config{.property = "state:can_redo",
                                            .value = true}}},
      {.property =
           FixedValue{.type = ViewProperty::Type::kToggle, .value = true},
       .control = "smpte_light",
       .type = ViewMapping::kWriteControl,
       .config = {.write = {.mode = "blink"}}},
  };

  ViewSpec track_mode =
      ChildTracksView("track_mode", "user:folder", "user:current_track");
  track_mode.config.condition = ViewCondition::Config{
      .property = "user:surface_mode", .value = std::string("track")};
  track_mode.mappings = {{.property = "view:bank_next",
                          .control = "function[0]",
                          .type = ViewMapping::kReadControl},
                         {.property = "view:to_parent_track",
                          .control = "function[1]",
                          .type = ViewMapping::kReadControl}};
  ViewSpec strip = RepeatedView("strip", "strip");
  strip.components = {
      TrackAnchorSpec{"user:anchor_select", TrackAnchorAction::kSelected,
                      "mod:send_hold"},
      TrackAnchorSpec{"user:anchor_mute", TrackAnchorAction::kMute,
                      "mod:shift"},
      TrackPickSpec{"user:pick_track", "user:current_track"},
  };
  strip.uses = {{.name = "anchored_strip",
                 .widgets = {{"strip", "strip"}},
                 .properties = {{"anchor", "user:anchor_select"}}}};
  strip.mappings = {{.property = "view:parent_to_track",
                     .control = "strip.rec",
                     .type = ViewMapping::kReadControl},
                    {.property = "user:pick_track",
                     .control = "strip.solo",
                     .type = ViewMapping::kReadControl}};
  // A sibling may declare what another does, as neither sees the other's.
  ViewSpec info = {.name = "info",
                   .components = {TrackAnchorSpec{"user:anchor_select"}}};
  track_mode.children = {strip, info};

  ViewSpec send_receive_mode =
      BoundView("send_receive_mode", "user:current_track");
  send_receive_mode.config.list =
      View::ListConfig{.items = View::Routes{}, .bank_size = 8};
  send_receive_mode.mappings = {{.property = "view:route_type_name",
                                 .control = "strip[7].scribble_bottom",
                                 .type = ViewMapping::kWriteControl}};
  ViewSpec route = RepeatedView("strip", "route_strip");
  route.mappings = {{.property = "route:volume",
                     .control = "route_strip.fader",
                     .type = ViewMapping::kReadWriteControl},
                    {.property = "route:other_track.name",
                     .control = "route_strip.scribble_top",
                     .type = ViewMapping::kWriteControl},
                    {.property = "view:parent_to_other_track",
                     .control = "route_strip.select",
                     .type = ViewMapping::kReadControl},
                    {.property = "state:master_track.volume",
                     .control = "master_fader",
                     .type = ViewMapping::kReadWriteControl}};
  send_receive_mode.children = {route};
  spec.root.children = {track_mode, send_receive_mode};

  EXPECT_THAT(Validate(spec), IsEmpty());
}

TEST(SurfaceSpecTest, DevicesAndWidgetsAreValidated) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.devices.push_back(spec.devices[0]);
  spec.devices[1].name = "xtouch_2";
  spec.devices[1].input_port = "X-Touch-2";
  EXPECT_THAT(Validate(spec), ElementsAre("xtouch_2: output port \"X-Touch\" "
                                          "is also xtouch's"));

  spec = GetXTouchSpec();
  spec.widgets.includes[0].path = "xtouch.nope";
  EXPECT_THAT(Validate(spec),
              ElementsAre("include \"xtouch.nope\" names nothing"));
}

// A device that isn't valid has no catalog, so what uses its widgets isn't
// checked.
TEST(SurfaceSpecTest, AnInvalidDeviceSkipsWhatUsesItsWidgets) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.devices[0].type = "XTouh";
  ViewSpec list_view = ChildTracksView("list");
  list_view.children = {RepeatedView("strip", "strip")};
  spec.root.children = {list_view};
  EXPECT_THAT(Validate(spec),
              ElementsAre("xtouch: type \"XTouh\" isn't a device type"));
}

TEST(SurfaceSpecTest, ComponentsAreDeclaredWhereTheirKindMayBe) {
  SurfaceSpec spec;
  spec.components = {TrackAnchorSpec{"user:anchor"}};
  ViewSpec view = BoundView("view", "state:master_track");
  view.components = {ModifierSpec{"mod:fn"},
                     EnumeratedValueSpec{"user:mode", {"a"}},
                     TrackReferenceSpec{"user:track"},
                     TrackPickSpec{"user:pick", "user:track"}};
  spec.root.children = {view};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "user:anchor: is of kind \"track_anchor\", which can't be declared "
          "at the top level",
          "/view: mod:fn: is of kind \"modifier\", which can't be declared on "
          "a view",
          "/view: user:mode: is of kind \"enumerated_value\", which can't be "
          "declared on a view",
          "/view: user:track: is of kind \"track_reference\", which can't be "
          "declared on a view"));
}

TEST(SurfaceSpecTest, ComponentNamesAreInTheirNamespaces) {
  SurfaceSpec spec;
  spec.components = {ModifierSpec{"user:fn"},
                     ModifierSpec{"mod:fn"},
                     EnumeratedValueSpec{"mod:mode", {"a"}},
                     EnumeratedValueSpec{"user:", {"a"}},
                     EnumeratedValueSpec{"user:a.b", {"a"}},
                     EnumeratedValueSpec{"user:mode", {"a"}}};
  EXPECT_THAT(Validate(spec),
              ElementsAre("modifier name \"user:fn\" isn't \"mod:\" followed "
                          "by a C identifier",
                          "enumerated_value name \"mod:mode\" isn't \"user:\" "
                          "followed by a C identifier",
                          "enumerated_value name \"user:\" isn't \"user:\" "
                          "followed by a C identifier",
                          "enumerated_value name \"user:a.b\" isn't \"user:\" "
                          "followed by a C identifier"));
}

TEST(SurfaceSpecTest, ADeclarationHidesNoOtherVisibleWhereItIsMade) {
  SurfaceSpec spec;
  spec.components = {ModifierSpec{"mod:shift"}, ModifierSpec{"mod:fn"},
                     ModifierSpec{"mod:fn"}, TrackReferenceSpec{"user:track"},
                     EnumeratedValueSpec{"user:track", {"a"}}};
  ViewSpec child = {.name = "child",
                    .components = {TrackAnchorSpec{"user:anchor"}}};
  ViewSpec view = BoundView("view", "user:track");
  view.components = {TrackAnchorSpec{"user:anchor"},
                     TrackAnchorSpec{"user:anchor"},
                     TrackAnchorSpec{"user:track"}};
  view.children = {child};
  // Once the view is done, its declarations are no longer visible.
  ViewSpec sibling = BoundView("sibling", "user:track");
  sibling.components = {TrackAnchorSpec{"user:anchor"}};
  spec.root.children = {view, sibling};
  constexpr std::string_view kHides =
      ": is already declared where it is visible, or is built in";
  EXPECT_THAT(Validate(spec),
              ElementsAre(absl::StrCat("mod:shift", kHides),
                          absl::StrCat("mod:fn", kHides),
                          absl::StrCat("user:track", kHides),
                          absl::StrCat("/view: user:anchor", kHides),
                          absl::StrCat("/view: user:track", kHides),
                          absl::StrCat("/view/child: user:anchor", kHides)));
}

TEST(SurfaceSpecTest, EnumeratedValuesAreUniqueNamesWithTheInitialOne) {
  SurfaceSpec spec;
  spec.components = {
      EnumeratedValueSpec{"user:none"},
      EnumeratedValueSpec{"user:bad", {"a b", "a", "a"}, "b"},
      EnumeratedValueSpec{"user:first", {"a", "b"}},
      EnumeratedValueSpec{"user:second", {"a", "b"}, "b"},
  };
  EXPECT_THAT(Validate(spec),
              ElementsAre("user:none: has no values",
                          "user:bad: value \"a b\" isn't a C identifier",
                          "user:bad: value \"a\" is repeated",
                          "user:bad: initial value \"b\" isn't one of its "
                          "values"));
}

TEST(SurfaceSpecTest, AReferenceFallsBackToAndFollowsEarlierReferences) {
  SurfaceSpec spec;
  spec.components = {
      TrackReferenceSpec{"user:early", "user:late", "user:nothing"},
      TrackReferenceSpec{"user:late", "state:master_track",
                         "state:last_touched_track"},
      TrackReferenceSpec{"user:latest", "user:late", "user:early"},
      TrackReferenceSpec{"user:other", "state:can_redo"},
  };
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("user:early: fallback \"user:late\" isn't a track "
                  "reference, built in or declared before it",
                  "user:early: follow \"user:nothing\" isn't a track "
                  "reference, built in or declared before it",
                  "user:other: fallback \"state:can_redo\" isn't a track "
                  "reference, built in or declared before it"));
}

TEST(SurfaceSpecTest, APickSetsADeclaredReferenceFromItsSource) {
  SurfaceSpec spec;
  spec.components = {
      TrackPickSpec{"user:early", "user:track", "state:selected_track"},
      TrackReferenceSpec{"user:track"},
      TrackPickSpec{"user:built_in", "state:master_track",
                    "state:selected_track"},
      TrackPickSpec{"user:no_reference", "", "state:selected_track"},
      TrackPickSpec{"user:no_source", "user:track"},
      TrackPickSpec{"user:bad_source", "user:track", "user:nothing"},
      TrackPickSpec{"user:pick", "user:track", "user:track"},
  };
  ViewSpec view = BoundView("view", "state:master_track");
  view.components = {
      TrackPickSpec{"user:view_pick", "user:track"},
      TrackPickSpec{"user:sourced", "user:track", "state:selected_track"}};
  spec.root.children = {view};
  spec.root.components = {TrackPickSpec{"user:root_pick", "user:track"}};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "user:early: reference \"user:track\" isn't a track reference "
          "declared before it",
          "user:built_in: reference \"state:master_track\" isn't a track "
          "reference declared before it",
          "user:no_reference: has no reference",
          "user:no_source: has no source",
          "user:bad_source: source \"user:nothing\" isn't a track reference, "
          "built in or declared before it",
          "/: user:root_pick: is on a view without a track subject",
          "/view: user:sourced: has a source, which only a pick at the top "
          "level has"));
}

TEST(SurfaceSpecTest, AnAnchorIsOnATrackWithAModifier) {
  SurfaceSpec spec;
  spec.components = {ModifierSpec{"mod:fn"}};
  ViewSpec view = BoundView("view", "state:master_track");
  view.components = {
      TrackAnchorSpec{"user:none"},
      TrackAnchorSpec{"user:built_in", TrackAnchorAction::kMute, "mod:shift"},
      TrackAnchorSpec{"user:declared", TrackAnchorAction::kSolo, "mod:fn"},
      TrackAnchorSpec{"user:other", TrackAnchorAction::kRecArm, "user:fn"}};
  spec.root.children = {
      view, {.name = "none", .components = {TrackAnchorSpec{"user:a"}}}};
  EXPECT_THAT(Validate(spec),
              ElementsAre("/view: user:other: modifier \"user:fn\" isn't a "
                          "modifier",
                          "/none: user:a: is on a view without a track "
                          "subject"));
}

TEST(SurfaceSpecTest, AConfigDeclaresAsManyModifiersAsTheSceneHas) {
  SurfaceSpec spec;
  for (int i = 0; i <= kModUserCount; ++i) {
    spec.components.push_back(ModifierSpec{absl::StrCat("mod:m", i)});
  }
  EXPECT_THAT(Validate(spec), ElementsAre("mod:m60: is modifier 61, of the 60 "
                                          "a config can declare"));
  spec.components.pop_back();
  EXPECT_THAT(Validate(spec), IsEmpty());

  // A modifier that isn't declared takes no flag.
  spec.components.push_back(ModifierSpec{"mod:m0"});
  EXPECT_THAT(Validate(spec),
              ElementsAre("mod:m0: is already declared where it is visible, or "
                          "is built in"));
}

TEST(SurfaceSpecTest, ViewsHaveNamesUniqueAmongSiblings) {
  SurfaceSpec spec = GetXTouchSpec();
  ViewSpec list_view = ChildTracksView("list");
  ViewSpec named = RepeatedView("strip", "strip");
  named.name = "strip";
  list_view.children = {named};
  spec.root.children = {{.name = "a"},    {.name = ""}, {.name = "a b"},
                        {.name = "a"},    list_view,    {.name = "list"},
                        {.name = "other"}};
  EXPECT_THAT(Validate(spec),
              ElementsAre("/: view name \"\" isn't a C identifier",
                          "/: view name \"a b\" isn't a C identifier",
                          "/: has more than one child view named \"a\"",
                          "/list/strip[]: is repeated, so it has no name, but "
                          "is named \"strip\"",
                          "/: has more than one child view named \"list\""));
}

TEST(SurfaceSpecTest, TheRootViewIsTheScenes) {
  SurfaceSpec spec;
  spec.root = BoundView("root", "state:master_track");
  spec.root.config.condition = ViewCondition::Config{.property = "user:x"};
  spec.root.config.list = View::ListConfig{};
  spec.root.repeat = RepeatSpec{};
  EXPECT_THAT(Validate(spec), ElementsAre("/: the root view has no name",
                                          "/: the root view has no condition",
                                          "/: the root view has no subject",
                                          "/: the root view has no list",
                                          "/: the root view isn't repeated"));
}

TEST(SurfaceSpecTest, ASubjectIsAReferenceOrAnItemOfTheParentsList) {
  SurfaceSpec spec = GetXTouchSpec();
  ViewSpec unlisted = {.name = "unlisted",
                       .config = {.subject = View::ListItemSubject{}}};
  ViewSpec unrepeated = RepeatedView("strip", "strip");
  unrepeated.config.subject = View::ParentSubject{};
  ViewSpec list_view = ChildTracksView("list");
  list_view.children = {RepeatedView("strip", "strip"), unlisted};
  spec.root.children = {BoundView("bound", "user:nothing"),
                        BoundView("built_in", "state:selected_track"),
                        RepeatedView("strip", "strip"), list_view, unrepeated};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("/bound: is bound to \"user:nothing\", which isn't a "
                  "reference",
                  "/strip[]: shows an item of its parent's list, but its "
                  "parent has no list",
                  "/list/unlisted: shows an item of its parent's list, but "
                  "isn't repeated",
                  "/strip[]: is repeated, but its subject isn't an item of its "
                  "parent's list",
                  "/strip[]: shows an item of its parent's list, but its "
                  "parent has no list"));
}

TEST(SurfaceSpecTest, AListIsOfATracksChildrenOrRoutes) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {TrackReferenceSpec{"user:track"}};

  ViewSpec routes = BoundView("routes", "state:master_track");
  routes.config.list = View::ListConfig{.items = View::Routes{}};
  ViewSpec route = RepeatedView("strip", "route");
  route.config.list = View::ListConfig{.items = View::Routes{}};
  route.children = {RepeatedView("strip", "inner")};
  routes.children = {route};

  ViewSpec no_subject = {
      .name = "no_subject",
      .config = {.list = View::ListConfig{.items = View::Routes{}}},
      .children = {RepeatedView("strip", "strip")}};
  ViewSpec empty_bank = ListView("empty_bank");
  empty_bank.config.list->bank_size = 0;

  // A list can only reveal another reference that is a track's, and can only
  // move a declared one to reveal it.
  spec.root.children = {
      routes,
      no_subject,
      empty_bank,
      ListView("reveals_nothing", "user:track", "user:nothing"),
      ListView("reveals_itself", "user:track", "user:track"),
      ListView("reveals_built_in", "state:master_track", "user:track"),
      ListView("reveals", "user:track", "state:selected_track")};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("/routes/route[]: has a list, but no track subject",
                  "/no_subject: has a list, but no track subject",
                  "/empty_bank: has a bank size of 0, which isn't positive",
                  "/reveals_nothing: reveals \"user:nothing\", which isn't a "
                  "track reference",
                  "/reveals_itself: reveals \"user:track\", but isn't bound to "
                  "a declared reference other than it",
                  "/reveals_built_in: reveals \"user:track\", but isn't bound "
                  "to a declared reference other than it"));
}

TEST(SurfaceSpecTest, AViewWithAListHasOneRepeatedChild) {
  SurfaceSpec spec = GetXTouchSpec();
  ViewSpec none = ChildTracksView("none");
  none.children = {{.name = "other"}};
  ViewSpec two = ChildTracksView("two");
  two.children = {RepeatedView("strip", "strip"),
                  RepeatedView("strip", "strip")};
  spec.root.children = {none, two};
  EXPECT_THAT(Validate(spec),
              ElementsAre("/none: has a list, so it has one repeated child, "
                          "but has 0",
                          "/two: has a list, so it has one repeated child, but "
                          "has 2"));
}

TEST(SurfaceSpecTest, ARepeatedViewIsOverAnArray) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.root.children = {
      WithChild(ChildTracksView("missing"), RepeatedView("strp", "strip")),
      WithChild(ChildTracksView("bad"), RepeatedView("strip[", "strip")),
      WithChild(ChildTracksView("control"), RepeatedView("play", "strip")),
      WithChild(ChildTracksView("element"), RepeatedView("strip", "a b")),
      WithChild(ChildTracksView("index"), RepeatedView("[0]", "strip"))};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("/missing/strip[]: array \"strp\" names nothing",
                  StartsWith("/bad/strip[]: array \"strip[\" isn't a path: "),
                  "/control/strip[]: array \"play\" isn't an array",
                  "/element/a b[]: element \"a b\" isn't a C identifier",
                  "/index/strip[]: array \"[0]\" names nothing"));
}

// An element hides the top-level widget of the same name, so a repeated view
// in another is over the element's array if it has one.
TEST(SurfaceSpecTest, AnInnerElementHidesAnOuterName) {
  SurfaceSpec spec = GetXTouchSpec();
  ViewSpec outer = ChildTracksView("list");
  ViewSpec hidden = RepeatedView("strip", "strip");
  hidden.config.list = View::ListConfig{.items = View::ChildTracks{}};
  hidden.children = {RepeatedView("strip", "inner")};
  outer.children = {hidden};

  ViewSpec other = ChildTracksView("other");
  ViewSpec renamed = RepeatedView("strip", "outer");
  renamed.config.list = View::ListConfig{.items = View::ChildTracks{}};
  renamed.children = {RepeatedView("strip", "inner")};
  other.children = {renamed};

  spec.root.children = {outer, other};
  EXPECT_THAT(Validate(spec),
              ElementsAre("/list/strip[]/inner[]: array \"strip\" isn't an "
                          "array"));
}

TEST(SurfaceSpecTest, TemplatesHaveUniqueNamesAndParameters) {
  SurfaceSpec spec;
  spec.templates = {
      {.name = "a b"},
      {.name = "strip",
       .widget_params = {"strip", "", "strip"},
       .property_params = {{.name = "a.b"},
                           {.name = "anchor"},
                           {.name = "anchor"},
                           {.name = "strip"}}},
      {.name = "strip"},
  };
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("template name \"a b\" isn't a C identifier",
                  "template strip: widget parameter \"\" isn't a C identifier",
                  "template strip: widget parameter \"strip\" is repeated",
                  "template strip: property parameter \"a.b\" isn't a C "
                  "identifier",
                  "template strip: property parameter \"anchor\" is repeated",
                  "template strip: property parameter \"strip\" is repeated",
                  "template strip: has the name of another template"));
}

TEST(SurfaceSpecTest, AUseGivesItsTemplatesParametersWithoutDefaults) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "strip",
       .widget_params = {"strip", "button"},
       .property_params = {{.name = "anchor"}}},
      {.name = "user",
       .uses = {{.name = "strip",
                 .widgets = {{"strip", "strip"}, {"z", "a"}, {"y", "b"}},
                 .properties = {{"anchor", "a"}}}}},
  };
  spec.root.uses = {{.name = "nothing"},
                    {.name = "strip",
                     .widgets = {{"strip", "strip"}, {"button", "play"}},
                     .properties = {{"anchor", "anchor"}}},
                    {.name = "strip", .properties = {{"other", "a"}}}};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre("template user: uses strip without its widget parameter "
                  "\"button\"",
                  "template user: uses strip with \"y\", which isn't one of "
                  "its widget parameters",
                  "template user: uses strip with \"z\", which isn't one of "
                  "its widget parameters",
                  "/: uses \"nothing\", which isn't a template",
                  "/: uses strip without its widget parameter \"strip\"",
                  "/: uses strip without its widget parameter \"button\"",
                  "/: uses strip without its property parameter \"anchor\"",
                  "/: uses strip with \"other\", which isn't one of its "
                  "property parameters"));
}

// Returns a mapping of the property to the control, of the type.
MappingSpec Mapping(
    std::string property, std::string control,
    ViewMapping::TypeFlags type = ViewMapping::kReadWriteControl,
    ViewMapping::Config config = {}) {
  return {.property = std::move(property),
          .control = std::move(control),
          .type = type,
          .config = std::move(config)};
}

// Returns an X-Touch spec, with the components and a view of the master track
// with the mappings.
SurfaceSpec MappingSpecs(std::vector<MappingSpec> mappings,
                         std::vector<ComponentSpec> components = {}) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = std::move(components);
  ViewSpec view = BoundView("view", "state:master_track");
  view.mappings = std::move(mappings);
  spec.root.children = {view};
  return spec;
}

TEST(SurfaceSpecTest, AMappingIsOfAControl) {
  EXPECT_THAT(
      Validate(MappingSpecs(
          {Mapping("track:mute", "nothing"), Mapping("track:mute", "strip"),
           Mapping("track:mute", "strip[0]"), Mapping("track:mute", "play[0]"),
           Mapping("track:mute", "strip["),
           Mapping("track:mute", "play", {})})),
      ElementsAre(
          "/view: nothing: names nothing", "/view: strip: isn't a control",
          "/view: strip[0]: isn't a control", "/view: play[0]: names nothing",
          StartsWith("/view: strip[: isn't a path: "),
          "/view: play: neither reads nor writes its control"));
}

TEST(SurfaceSpecTest, AMappingsPropertyIsOneWhereItIs) {
  SurfaceSpec spec = MappingSpecs(
      {Mapping("track:nothing", "play"), Mapping("route:volume", "play"),
       Mapping("view:bank_next", "play"), Mapping("view:nothing", "play"),
       Mapping("user:track", "play"), Mapping("user:track.nothing", "play"),
       Mapping("user:nothing.mute", "play"), Mapping("user:nothing", "play"),
       Mapping("state:nothing", "play"), Mapping("cmd:", "play"),
       Mapping("cmd:_", "play"), Mapping("cmd:0", "play"),
       Mapping("cmd:a", "play"), Mapping("nothing", "play"),
       Mapping("user:anchor", "play")},
      {TrackReferenceSpec{"user:track"}});
  // A built in reference can't be moved, so its list has no navigation, nor
  // do its items, and what a view declares is visible only in it.
  ViewSpec list =
      WithChild(ChildTracksView("list"), RepeatedView("strip", "strip"));
  list.mappings = {Mapping("view:to_parent_track", "play")};
  list.children[0].mappings = {Mapping("view:parent_to_track", "play")};
  ViewSpec anchored = BoundView("anchored", "state:master_track");
  anchored.components = {TrackAnchorSpec{"user:anchor"}};
  spec.root.children.push_back(list);
  spec.root.children.push_back(anchored);
  spec.root.mappings = {Mapping("track:mute", "play")};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "/: play: property \"track:mute\" needs a view with a track subject",
          "/view: play: property \"track:nothing\" isn't a property",
          "/view: play: property \"route:volume\" needs a view with a route "
          "subject",
          "/view: play: property \"view:bank_next\" isn't on this view",
          "/view: play: property \"view:nothing\" isn't a property",
          "/view: play: property \"user:track\" is a reference, whose fields "
          "are properties",
          "/view: play: property \"user:track.nothing\" isn't a property",
          "/view: play: property \"user:nothing.mute\" isn't a property",
          "/view: play: property \"user:nothing\" isn't a property",
          "/view: play: property \"state:nothing\" isn't a property",
          "/view: play: property \"cmd:\" isn't a command id",
          "/view: play: property \"cmd:_\" isn't a command id",
          "/view: play: property \"cmd:0\" isn't a command id",
          "/view: play: property \"cmd:a\" isn't a command id",
          "/view: play: property \"nothing\" isn't a property",
          "/view: play: property \"user:anchor\" isn't a property",
          "/list: play: property \"view:to_parent_track\" isn't on this view",
          "/list/strip[]: play: property \"view:parent_to_track\" isn't on "
          "this view"));
}

TEST(SurfaceSpecTest, EachSideOfAMappingNeedsItsAccess) {
  SurfaceSpec spec = MappingSpecs({Mapping("track:exists", "strip[0].mute"),
                                   Mapping("user:anchor", "strip[0].mute"),
                                   Mapping("cmd:40044", "strip[0].mute")},
                                  {ModifierSpec{"mod:fn"}});
  spec.root.children[0].components = {TrackAnchorSpec{"user:anchor"}};
  spec.root.mappings = {{.property = FixedValue{.value = true},
                         .control = "play",
                         .type = ViewMapping::kReadWriteControl},
                        {.property = FixedValue{.value = 1},
                         .control = "stop",
                         .type = ViewMapping::kWriteControl}};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "/: play: reads into its fixed value, which can't be written "
          "or triggered",
          "/: stop: has a fixed value that isn't a toggle constant",
          "/view: strip[0].mute: reads into \"track:exists\", which "
          "can't be written or triggered",
          "/view: strip[0].mute: writes \"user:anchor\", which can't be "
          "read"));
}

TEST(SurfaceSpecTest, EachSideOfAMappingNeedsAControlThatSuitsItsType) {
  EXPECT_THAT(
      Validate(MappingSpecs(
          {Mapping("track:name", "strip[0].mute"),
           Mapping("track:volume", "smpte_light"),
           Mapping("track:name", "play", ViewMapping::kReadControl),
           Mapping("track:pan", "play", ViewMapping::kReadControl,
                   {.read = {.press_release = true}}),
           Mapping("track:mute", "play", ViewMapping::kReadControl,
                   {.read = {.input_type = ControlInput::Type::kDelta}}),
           Mapping("track:mute", "timecode", ViewMapping::kReadControl,
                   {.read = {.touch = true}}),
           Mapping("state:timeline_position", "smpte_light",
                   ViewMapping::kWriteControl)})),
      ElementsAre(
          "/view: strip[0].mute: can't read into \"track:name\" (text) from "
          "the control with its read config",
          "/view: strip[0].mute: can't write \"track:name\" (text) to the "
          "control",
          "/view: smpte_light: can't read into \"track:volume\" (volume) from "
          "the control with its read config",
          "/view: play: can't read into \"track:name\" (text) from the control "
          "with its read config",
          "/view: play: can't read into \"track:pan\" (pan) from the control "
          "with its read config",
          "/view: play: can't read into \"track:mute\" (toggle) from the "
          "control with its read config",
          "/view: timecode: can't read into \"track:mute\" (toggle) from the "
          "control with its read config",
          "/view: smpte_light: can't write \"state:timeline_position\" "
          "(timeline_position) to the control"));
}

TEST(SurfaceSpecTest, AMappingsModesAreItsControls) {
  EXPECT_THAT(
      Validate(MappingSpecs(
          {Mapping("track:mute", "strip[0].mute", ViewMapping::kWriteControl,
                   {.write = {.mode = "fast",
                              .mode_overrides = {{"track:exists",
                                                  {{false, "blink"},
                                                   {true, "slow"}}}}}}),
           Mapping("state:timeline_position", "timecode",
                   ViewMapping::kWriteControl, {.write = {.mode = "blink"}}),
           Mapping("cmd:40044", "play", ViewMapping::kWriteControl,
                   {.write = {.mode = "beats"}})})),
      ElementsAre("/view: strip[0].mute: has no mode \"fast\"",
                  "/view: strip[0].mute: has no mode \"slow\"",
                  "/view: timecode: has no mode \"blink\"",
                  "/view: play: has no mode \"beats\""));
}

TEST(SurfaceSpecTest, ARangeIsOfValuesOfThePropertysType) {
  constexpr ViewMapping::TypeFlags kRead = ViewMapping::kReadControl;
  auto range = [](ViewProperty::Value min, ViewProperty::Value max) {
    return ViewMapping::Config{.read = {.property_min = std::move(min),
                                        .property_max = std::move(max)}};
  };
  EXPECT_THAT(
      Validate(MappingSpecs(
          {Mapping("track:mute", "play", kRead, range(false, true)),
           Mapping("track:pan", "strip[0].pot", kRead, range(-1.0, 1)),
           Mapping("track:name", "play", kRead,
                   range(std::string("a"), Color{})),
           Mapping("user:mode", "play", kRead,
                   range(std::string("a"), std::string("c"))),
           Mapping("user:mode", "play", kRead, range(0, 1.0)),
           Mapping("state:ruler_mode", "play", kRead,
                   range(std::string("beats"), 1))},
          {EnumeratedValueSpec{"user:mode", {"a", "b"}}})),
      ElementsAre(
          "/view: play: has a range min, which \"track:mute\" (toggle) "
          "ignores",
          "/view: play: has a range max, which \"track:mute\" (toggle) "
          "ignores",
          "/view: strip[0].pot: range max isn't a value of \"track:pan\" "
          "(pan)",
          "/view: play: range max isn't a value of \"track:name\" (text)",
          "/view: play: can't read into \"track:name\" (text) from the "
          "control with its read config",
          "/view: play: range max \"c\" isn't one of \"user:mode\"'s values",
          "/view: play: range max isn't a value of \"user:mode\" "
          "(enumerated)",
          "/view: play: range min \"beats\" isn't one of "
          "\"state:ruler_mode\"'s values"));
}

TEST(SurfaceSpecTest, ABuiltInEnumeratedValueIsNamedByItsValues) {
  auto range = [](std::string min, std::string max) {
    return ViewMapping::Config{.read = {.property_min = std::move(min),
                                        .property_max = std::move(max)}};
  };
  EXPECT_THAT(Validate(MappingSpecs(
                  {Mapping("track:rec_mode", "play", ViewMapping::kReadControl,
                           range("none", "midi_overdub")),
                   Mapping("track:ui_monitor_mode", "play",
                           ViewMapping::kReadControl, range("on", "loud"))})),
              ElementsAre("/view: play: range max \"loud\" isn't one of "
                          "\"track:ui_monitor_mode\"'s values"));
}

TEST(SurfaceSpecTest, AConditionReadsAValueOfItsPropertysType) {
  SurfaceSpec spec = MappingSpecs(
      {Mapping("track:mute", "play", ViewMapping::kReadWriteControl,
               {.write = {.mode_overrides = {{"user:mode",
                                              {{std::string("c"), "blink"}}},
                                             {"user:anchor", {{true, ""}}},
                                             {"user:nothing", {}}}},
                .condition = ViewCondition::Config{.property = "user:mode",
                                                   .value = std::string("c")}}),
       Mapping("track:mute", "stop", ViewMapping::kReadWriteControl,
               {.condition = ViewCondition::Config{.property = "user:anchor"}}),
       Mapping(
           "track:mute", "record", ViewMapping::kReadWriteControl,
           {.condition = ViewCondition::Config{.property = "track:mute",
                                               .value = TimelinePosition()}}),
       Mapping(
           "track:mute", "rewind", ViewMapping::kReadWriteControl,
           {.condition = ViewCondition::Config{.property = "track:mute",
                                               .value = std::monostate()}})},
      {EnumeratedValueSpec{"user:mode", {"a", "b"}}});
  spec.root.children[0].components = {TrackAnchorSpec{"user:anchor"}};

  // A view's condition is made before its declarations.
  spec.root.children[0].config.condition =
      ViewCondition::Config{.property = "user:anchor"};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "/view: condition \"user:anchor\" isn't a property",
          "/view: play: mode override \"c\" isn't one of \"user:mode\"'s "
          "values",
          "/view: play: mode override on \"user:anchor\", which can't be read",
          "/view: play: mode override \"user:nothing\" isn't a property",
          "/view: play: condition \"c\" isn't one of \"user:mode\"'s values",
          "/view: stop: condition on \"user:anchor\", which can't be read",
          "/view: record: condition on \"track:mute\" (toggle) has a value it "
          "can never equal",
          "/view: rewind: condition on \"track:mute\" (toggle) has a value it "
          "can never equal"));
}

// An item view's view: properties are its list's, added once it is made, so
// its condition doesn't see them, though its mappings do.
TEST(SurfaceSpecTest, AnItemsConditionIsMadeBeforeItsListsProperties) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {TrackReferenceSpec{"user:folder"}};
  ViewSpec list = ListView("list", "user:folder");
  list.children[0].config.condition =
      ViewCondition::Config{.property = "view:parent_to_track"};
  list.children[0].mappings = {Mapping("view:parent_to_track", "strip.select",
                                       ViewMapping::kReadControl)};
  spec.root.children = {list};
  EXPECT_THAT(Validate(spec),
              ElementsAre("/list/strip[]: condition "
                          "\"view:parent_to_track\" isn't on this view"));
}

TEST(SurfaceSpecTest, AMappingRequiresModifiers) {
  EXPECT_THAT(
      Validate(MappingSpecs(
          {Mapping("track:mute", "play", ViewMapping::kReadControl,
                   {.read = {.required_modifiers = {"mod:shift", "mod:fn",
                                                    "mod:nothing", "user:mode",
                                                    "mod:shift"}}})},
          {ModifierSpec{"mod:fn"}, EnumeratedValueSpec{"user:mode", {"a"}}})),
      ElementsAre("/view: play: requires \"mod:nothing\", which isn't a "
                  "modifier",
                  "/view: play: requires \"user:mode\", which isn't a "
                  "modifier",
                  "/view: play: requires \"mod:shift\" twice"));
}

// A template sees its parameters and the top-level widgets, not the elements
// of the view that uses it, and its parameters hide the top-level widgets.
TEST(SurfaceSpecTest, ATemplatesPathsStartAtItsParameters) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "button",
       .widget_params = {"play"},
       .property_params = {{.name = "toggle"}},
       .mappings = {Mapping("toggle", "play"), Mapping("toggle", "stop"),
                    Mapping("toggle", "strip.mute")}},
      {.name = "outer",
       .widget_params = {"button"},
       .property_params = {{.name = "outer_toggle"}},
       .uses = {{.name = "button",
                 .widgets = {{"play", "button"}},
                 .properties = {{"toggle", "outer_toggle"}}}}},
  };
  ViewSpec list = ListView("list");
  list.children[0].uses = {
      {.name = "button",
       .widgets = {{"play", "strip.mute"}},
       .properties = {{"toggle", "track:mute"}}},
      {.name = "outer",
       .widgets = {{"button", "strip.fader"}},
       .properties = {{"outer_toggle", "track:name"}}},
      {.name = "button", .widgets = {{"play", "strip.nothing"}}}};
  spec.root.children = {list};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "/list/strip[]: uses button without its property parameter "
          "\"toggle\"",
          "/list/strip[]: button: strip.mute: names nothing",
          "/list/strip[]: outer: button: play: can't write \"track:name\" "
          "(text) to the control",
          "/list/strip[]: outer: button: stop: can't read into "
          "\"track:name\" (text) from the control with its read config",
          "/list/strip[]: outer: button: stop: can't write \"track:name\" "
          "(text) to the control",
          "/list/strip[]: outer: button: strip.mute: names nothing",
          "/list/strip[]: uses button with widget \"play\" as "
          "\"strip.nothing\", which names nothing",
          "/list/strip[]: button: strip.mute: names nothing"));
}

// A default is an earlier parameter, which stands for its argument, or a name,
// which is checked as an argument is, where a use gives none.
TEST(SurfaceSpecTest, AUseThatGivesNoArgumentGetsTheDefault) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "lit",
       .widget_params = {"button"},
       .property_params = {{.name = "toggle"},
                           {.name = "light", .default_value = "toggle"},
                           {.name = "label", .default_value = "track:name"},
                           {.name = "other", .default_value = "track:nothing"}},
       .mappings = {Mapping("toggle", "button", ViewMapping::kWriteControl),
                    Mapping("light", "button", ViewMapping::kWriteControl),
                    Mapping("label", "button", ViewMapping::kWriteControl),
                    Mapping("other", "button", ViewMapping::kWriteControl)}},
  };
  ViewSpec list = ListView("list");
  list.children[0].uses = {
      {.name = "lit",
       .widgets = {{"button", "strip.mute"}},
       .properties = {{"toggle", "track:name"}}},
      {.name = "lit",
       .widgets = {{"button", "strip.solo"}},
       .properties = {{"toggle", "track:mute"},
                      {"light", "track:solo"},
                      {"label", "track:solo"},
                      {"other", "track:solo"}}},
      {.name = "lit", .widgets = {{"button", "strip.rec"}}}};
  spec.root.children = {list};
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "/list/strip[]: uses lit without its property parameter \"toggle\"",
          // The first use's toggle, its light (the toggle), and its label.
          "/list/strip[]: lit: button: can't write \"track:name\" (text) to "
          "the control",
          "/list/strip[]: lit: button: can't write \"track:name\" (text) to "
          "the control",
          "/list/strip[]: lit: button: can't write \"track:name\" (text) to "
          "the control",
          "/list/strip[]: lit: button: property \"track:nothing\" isn't a "
          "property",
          // The third use's label. Its toggle, and so its light, isn't known.
          "/list/strip[]: lit: button: can't write \"track:name\" (text) to "
          "the control",
          "/list/strip[]: lit: button: property \"track:nothing\" isn't a "
          "property"));
}

TEST(SurfaceSpecTest, ADefaultIsAnEarlierParameterOrAName) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {
      {.name = "lit",
       .widget_params = {"button"},
       .property_params = {{.name = "light", .default_value = "toggle"},
                           {.name = "toggle"},
                           {.name = "self", .default_value = "self"}},
       .mappings = {Mapping("light", "button", ViewMapping::kWriteControl),
                    Mapping("self", "button", ViewMapping::kWriteControl)}},
  };
  spec.root.uses = {{.name = "lit",
                     .widgets = {{"button", "play"}},
                     .properties = {{"toggle", "mod:shift"}}}};
  EXPECT_THAT(Validate(spec),
              ElementsAre("template lit: property parameter \"light\" "
                          "defaults to \"toggle\", which isn't an earlier "
                          "parameter",
                          "template lit: property parameter \"self\" defaults "
                          "to \"self\", which isn't an earlier parameter"));
}

// Returns true if the use gives a parameter the name, as an argument or a
// default.
bool IsArg(const std::optional<PropertyArg>& arg, std::string_view name,
           bool is_default) {
  return arg.has_value() && arg->name == name && arg->is_default == is_default;
}

TEST(SurfaceSpecTest, AUseGivesEachPropertyParameterItsArgumentOrDefault) {
  const TemplateSpec spec = {
      .name = "lit",
      .property_params = {
          {.name = "given", .default_value = "track:solo"},
          {.name = "name", .default_value = "track:mute"},
          {.name = "earlier_given", .default_value = "given"},
          {.name = "earlier_name", .default_value = "name"},
          {.name = "missing"},
          {.name = "earlier_missing", .default_value = "missing"},
          {.name = "self", .default_value = "self"},
          {.name = "later", .default_value = "last"},
          {.name = "last"}}};
  const TemplateUse use = {
      .name = "lit",
      .properties = {{"given", "outer"}, {"last", "track:name"}}};
  const std::vector<std::optional<PropertyArg>> args =
      GetPropertyArgs(spec, use);
  ASSERT_EQ(args.size(), spec.property_params.size());
  EXPECT_TRUE(IsArg(args[0], "outer", false));
  EXPECT_TRUE(IsArg(args[1], "track:mute", true));
  EXPECT_TRUE(IsArg(args[2], "outer", false));
  EXPECT_TRUE(IsArg(args[3], "track:mute", true));
  EXPECT_FALSE(args[4].has_value());
  EXPECT_FALSE(args[5].has_value());
  EXPECT_FALSE(args[6].has_value());
  EXPECT_FALSE(args[7].has_value());
  EXPECT_TRUE(IsArg(args[8], "track:name", false));
}

TEST(SurfaceSpecTest, ATemplateThatUsesItselfIsExpandedOnce) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.templates = {{.name = "self",
                     .mappings = {Mapping("state:can_redo", "play")},
                     .uses = {{.name = "self"}}}};
  spec.root.uses = {{.name = "self"}};
  EXPECT_THAT(Validate(spec),
              ElementsAre("template self: uses itself",
                          "/: self: play: reads into \"state:can_redo\", which "
                          "can't be written or triggered"));
}

TEST(SurfaceSpecTest, NoTemplateUsesItself) {
  SurfaceSpec spec;
  spec.templates = {
      {.name = "self", .uses = {{.name = "self"}}},
      {.name = "a", .uses = {{.name = "b"}}},
      {.name = "b", .uses = {{.name = "leaf"}, {.name = "c"}}},
      {.name = "c", .uses = {{.name = "a"}}},
      {.name = "leaf"},
      {.name = "user", .uses = {{.name = "leaf"}, {.name = "leaf"}}},
  };
  EXPECT_THAT(Validate(spec),
              ElementsAre("template self: uses itself",
                          "template a: uses itself, through b, c",
                          "template b: uses itself, through c, a",
                          "template c: uses itself, through a, b"));
}

// Returns the line, as a config's reader sets it.
SpecLine At(int number) { return {.number = number}; }

TEST(SurfaceSpecTest, ErrorsStartWithTheLineOfWhatTheyAreAbout) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {ModifierSpec{.name = "bad", .line = At(3)}};
  MappingSpec in_template = Mapping("state:can_redo", "button");
  in_template.line = At(7);
  spec.templates = {{.name = "a b",
                     .uses = {{.name = "nothing", .line = At(8)}},
                     .line = At(5)},
                    {.name = "strip",
                     .widget_params = {"button"},
                     .mappings = {in_template},
                     .line = At(6)}};
  ViewSpec view = BoundView("view", "state:master_track");
  view.line = At(11);
  view.components = {ModifierSpec{.name = "mod:on_view", .line = At(12)}};
  view.uses = {
      {.name = "strip", .line = At(13)},
      {.name = "strip", .widgets = {{"button", "play"}}, .line = At(14)}};
  MappingSpec in_view = Mapping("track:mute", "nothing");
  in_view.line = At(15);
  view.mappings = {in_view, Mapping("track:mute", "nowhere")};
  spec.root.children = {
      {.name = "a b", .line = At(10)}, view, {.name = "view", .line = At(16)}};

  // A mapping in a template is on its own line, with the path of the view
  // that uses it, and one without a line is as it was.
  EXPECT_THAT(
      Validate(spec),
      ElementsAre(
          "line 3: modifier name \"bad\" isn't \"mod:\" followed by a C "
          "identifier",
          "line 5: template name \"a b\" isn't a C identifier",
          "line 8: template a b: uses \"nothing\", which isn't a template",
          "line 10: /: view name \"a b\" isn't a C identifier",
          "line 12: /view: mod:on_view: is of kind \"modifier\", which can't "
          "be declared on a view",
          "line 13: /view: uses strip without its widget parameter "
          "\"button\"",
          "line 15: /view: nothing: names nothing",
          "/view: nowhere: names nothing",
          "line 7: /view: strip: button: reads into \"state:can_redo\", which "
          "can't be written or triggered",
          "line 16: /: has more than one child view named \"view\""));
}

TEST(SurfaceSpecTest, SpecsAreEqualWhereverTheyWereWritten) {
  SurfaceSpec spec = GetXTouchSpec();
  spec.components = {ModifierSpec{.name = "mod:a"}};
  spec.templates = {
      {.name = "strip", .mappings = {Mapping("track:mute", "play")}}};
  spec.root.children = {BoundView("view", "state:master_track")};
  spec.root.children[0].uses = {{.name = "strip"}};

  SurfaceSpec written = spec;
  std::get<ModifierSpec>(written.components[0]).line = At(1);
  written.templates[0].line = At(2);
  written.templates[0].mappings[0].line = At(3);
  written.root.children[0].line = At(4);
  written.root.children[0].uses[0].line = At(5);
  EXPECT_EQ(written, spec);

  // What a spec says is compared, down to its views' and mappings' configs.
  SurfaceSpec other = written;
  other.root.children[0].uses[0].name = "other";
  EXPECT_NE(other, spec);
  other = written;
  other.templates[0].mappings[0].config.read.press_toggles = true;
  EXPECT_NE(other, spec);
  other = written;
  other.root.children[0].config.subject = View::ParentSubject{};
  EXPECT_NE(other, spec);
}

}  // namespace
}  // namespace jpr
