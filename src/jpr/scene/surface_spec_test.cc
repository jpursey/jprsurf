// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/surface_spec.h"

#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/track.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/view.h"

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

// A spec's fields are its name, then its kind's parameters, in order: binding
// them doesn't compile once a field is added, until its parameter is added
// here. The kinds and uses are what the tests below show checking enforces.
TEST(SurfaceSpecTest, EachSpecHasItsKindsParameters) {
  [[maybe_unused]] auto [modifier_name] = ModifierSpec{};
  EXPECT_THAT(GetParams<ModifierSpec>(), IsEmpty());

  [[maybe_unused]] auto [value_name, values, initial] = EnumeratedValueSpec{};
  EXPECT_THAT(
      GetParams<EnumeratedValueSpec>(),
      ElementsAre(
          Param("values", ParamKind::kNewNameList, ParamUse::kRequired),
          Param("initial", ParamKind::kValueName, ParamUse::kOptional)));

  [[maybe_unused]] auto [reference_name, fallback, follow] =
      TrackReferenceSpec{};
  EXPECT_THAT(
      GetParams<TrackReferenceSpec>(),
      ElementsAre(
          Param("fallback", ParamKind::kTrackReference, ParamUse::kOptional),
          Param("follow", ParamKind::kTrackReference, ParamUse::kOptional)));

  [[maybe_unused]] auto [anchor_name, action, modifier] = TrackAnchorSpec{};
  EXPECT_THAT(
      GetParams<TrackAnchorSpec>(),
      ElementsAre(
          Param("action", ParamKind::kChoice, ParamUse::kRequired),
          Param("modifier", ParamKind::kModifier, ParamUse::kOptional)));

  [[maybe_unused]] auto [pick_name, reference, source] = TrackPickSpec{};
  EXPECT_THAT(GetParams<TrackPickSpec>(),
              ElementsAre(Param("reference", ParamKind::kTrackReference,
                                ParamUse::kRequired),
                          Param("source", ParamKind::kTrackReference,
                                ParamUse::kTopLevelOnly)));
}

TEST(SurfaceSpecTest, AnEmptySpecIsValid) {
  EXPECT_THAT(Validate({}), IsEmpty());
}

// A spec with each kind of thing checking accepts, as JPRSurf's own uses them.
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
  spec.templates = {
      {.name = "track_strip",
       .widget_params = {"strip"},
       .property_params = {"anchor"}},
      {.name = "anchored_strip",
       .widget_params = {"strip"},
       .uses = {{.name = "track_strip",
                 .widgets = {{"strip", "strip"}},
                 .properties = {{"anchor", "anchor_select"}}}}},
  };

  ViewSpec track_mode =
      ChildTracksView("track_mode", "user:folder", "user:current_track");
  ViewSpec strip = RepeatedView("strip", "strip");
  strip.components = {
      TrackAnchorSpec{"user:anchor_select", TrackBoolProperty::kSelected,
                      "mod:send_hold"},
      TrackAnchorSpec{"user:anchor_mute", TrackBoolProperty::kMute,
                      "mod:shift"},
      TrackPickSpec{"user:pick_track", "user:current_track"},
  };
  strip.uses = {{.name = "anchored_strip", .widgets = {{"strip", "strip"}}}};
  // A sibling may declare what another does, as neither sees the other's.
  ViewSpec info = {.name = "info",
                   .components = {TrackAnchorSpec{"user:anchor_select"}}};
  track_mode.children = {strip, info};

  ViewSpec send_receive_mode =
      BoundView("send_receive_mode", "user:current_track");
  send_receive_mode.config.list =
      View::ListConfig{.items = View::Routes{}, .bank_size = 8};
  send_receive_mode.children = {RepeatedView("strip", "route_strip")};
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
      TrackAnchorSpec{"user:built_in", TrackBoolProperty::kMute, "mod:shift"},
      TrackAnchorSpec{"user:declared", TrackBoolProperty::kSolo, "mod:fn"},
      TrackAnchorSpec{"user:other", TrackBoolProperty::kRecArm, "user:fn"}};
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
       .property_params = {"a.b", "anchor", "anchor"}},
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
                  "template strip: has the name of another template"));
}

TEST(SurfaceSpecTest, AUseGivesExactlyItsTemplatesParameters) {
  SurfaceSpec spec;
  spec.templates = {
      {.name = "strip",
       .widget_params = {"strip", "button"},
       .property_params = {"anchor"}},
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

}  // namespace
}  // namespace jpr
