// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/scene_pages.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {
namespace {

using ::testing::HasSubstr;

using Type = ViewProperty::Type;

TEST(GeneratePropertyNamespacePageTest, ListsPropertiesAndReferences) {
  const PropertyInfo properties[] = {
      {"state:play", Type::kAction, kTriggerAccess, "Plays."},
      {"state:volume", Type::kVolume, kReadWriteAccess, "Loud | soft.\nLevel."},
      {"state:position", Type::kTimelinePosition, kReadAccess, "Where."},
  };
  const ReferenceInfo references[] = {
      {"state:track", SubjectKind::kTrack, "A track."},
      {"state:route", SubjectKind::kRoute, "A route."},
  };
  const PropertyNamespaceInfo info = {"state:", "The state.", properties,
                                      references};

  EXPECT_EQ(
      GeneratePropertyNamespacePage(info),
      R"(<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `state:` properties

The state.

## Properties

| Property | Type | Access | Description |
| --- | --- | --- | --- |
| `state:play` | action | trigger | Plays. |
| `state:volume` | volume | read, write | Loud \| soft. Level. |
| `state:position` | timeline_position | read | Where. |

## References

| Reference | Fields | Description |
| --- | --- | --- |
| `state:track` | [`track:`](track.md) | A track. |
| `state:route` | [`route:`](route.md) | A route. |
)");
}

TEST(GeneratePropertyNamespacePageTest, ListsTheNamesOfEnumeratedValues) {
  constexpr std::string_view kValues[] = {"off", "on", "auto"};
  const PropertyInfo properties[] = {
      {.name = "state:mode",
       .type = Type::kEnumerated,
       .access = kReadAccess,
       .description = "The mode.",
       .values = kValues},
  };
  const PropertyNamespaceInfo info = {"state:", "The state.", properties};

  EXPECT_THAT(GeneratePropertyNamespacePage(info),
              HasSubstr("| `state:mode` | enumerated | read | The mode. "
                        "Values: `off`, `on`, `auto`. |\n"));
}

TEST(GeneratePropertyNamespacePageTest, ListsTheViewsOfViewProperties) {
  const PropertyInfo properties[] = {
      {"view:a", Type::kAction, kTriggerAccess, "A.", ViewNeed::kList},
      {"view:b", Type::kAction, kTriggerAccess, "B.",
       ViewNeed::kChildTracksList},
      {"view:c", Type::kAction, kTriggerAccess, "C.",
       ViewNeed::kChildTracksItem},
      {"view:d", Type::kToggle, kReadWriteAccess, "D.", ViewNeed::kRoutesList},
      {"view:e", Type::kAction, kTriggerAccess, "E.", ViewNeed::kRoutesItem},
  };
  const PropertyNamespaceInfo info = {"view:", "The view.", properties};

  const std::string page = GeneratePropertyNamespacePage(info);
  EXPECT_THAT(page, HasSubstr("| Property | Type | Access | Views | "
                              "Description |\n"
                              "| --- | --- | --- | --- | --- |\n"
                              "| `view:a` | action | trigger | Lists | A. |\n"
                              "| `view:b` | action | trigger | Child tracks "
                              "lists bound to a writable reference | B. |\n"
                              "| `view:c` | action | trigger | Item views of "
                              "a child tracks list bound to a writable "
                              "reference | C. |\n"
                              "| `view:d` | toggle | read, write | Routes "
                              "lists | D. |\n"
                              "| `view:e` | action | trigger | Item views of "
                              "a routes list bound to a writable reference | "
                              "E. |\n"));
}

TEST(GeneratePropertyNamespacePageTest, ListsNoTablesForNoRows) {
  const PropertyNamespaceInfo info = {"cmd:", "Commands."};

  EXPECT_EQ(
      GeneratePropertyNamespacePage(info),
      R"(<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `cmd:` properties

Commands.
)");
}

TEST(GenerateSceneIndexPageTest, LinksEachNamespaceAndTheComponents) {
  const PropertyNamespaceInfo catalog[] = {
      {"cmd:", "Commands | actions."},
      {"state:", "The state."},
  };
  const ComponentKindInfo kinds[] = {
      {.name = "flag",
       .name_space = "user:",
       .top_level = true,
       .description = "A flag."},
      {.name = "pick",
       .name_space = "user:",
       .top_level = true,
       .on_view = true,
       .description = "A pick."},
  };

  EXPECT_EQ(
      GenerateSceneIndexPage(catalog, kinds),
      R"(<!-- Generated from the scene's property catalog and component kinds by the update_user_docs target. Don't edit. -->

# The scene

What the scene provides to a config: the built in properties of each namespace, and the kinds of component a config can declare.

## Properties

| Namespace | Description |
| --- | --- |
| [`cmd:`](cmd.md) | Commands \| actions. |
| [`state:`](state.md) | The state. |

## Components

The [kinds of component](components.md) a config can declare: `flag`, `pick`.
)");
}

TEST(GetPropertyNamespacePageNameTest, IsTheNamespaceWithoutItsColon) {
  EXPECT_EQ(GetPropertyNamespacePageName("state:"), "state.md");
  EXPECT_EQ(GetPropertyNamespacePageName("mod:"), "mod.md");
}

TEST(GenerateComponentKindsPageTest, ListsEachKindAndItsParameters) {
  constexpr std::string_view kChoices[] = {"up", "down"};
  const ParamInfo value_params[] = {
      {"name", ParamKind::kNewName, ParamUse::kRequired, "Its name."},
      {"values", ParamKind::kNewNameList, ParamUse::kRequired, "Its values."},
      {"initial", ParamKind::kValueName, ParamUse::kOptional, "First | one."},
  };
  const ParamInfo pick_params[] = {
      {"source", ParamKind::kTrackReference, ParamUse::kTopLevelOnly,
       "Its source."},
      {"modifier", ParamKind::kModifier, ParamUse::kOptional, "Held."},
      {"way", ParamKind::kChoice, ParamUse::kRequired, "Which way.", kChoices},
  };
  const ComponentKindInfo kinds[] = {
      {.name = "flag",
       .name_space = "user:",
       .top_level = true,
       .description = "A flag."},
      {.name = "value",
       .name_space = "user:",
       .top_level = true,
       .description = "A value.",
       .params = value_params},
      {.name = "pick",
       .name_space = "user:",
       .top_level = true,
       .on_view = true,
       .description = "A pick.",
       .params = pick_params},
      {.name = "anchor",
       .name_space = "user:",
       .on_view = true,
       .description = "An anchor."},
  };

  EXPECT_EQ(
      GenerateComponentKindsPage(kinds),
      R"(<!-- Generated from the scene's component kinds by the update_user_docs target. Don't edit. -->

# Components

The kinds of component a config can declare. Every parameter is written as text, and its Value is what text it takes.

## `flag`

A flag.

Declared at the top level.

It takes no parameters.

## `value`

A value.

Declared at the top level.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `name` | A new name | Required | Its name. |
| `values` | A list of new names | Required | Its values. |
| `initial` | One of the names in `values` | Optional | First \| one. |

## `pick`

A pick.

Declared at the top level, or on a view.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `source` | A track reference: [built in](state.md#references), or declared before it | Required at the top level, and not given on a view | Its source. |
| `modifier` | A [modifier](mod.md): built in, or declared | Optional | Held. |
| `way` | One of `up`, `down` | Required | Which way. |

## `anchor`

An anchor.

Declared on a view.

It takes no parameters.
)");
}

}  // namespace
}  // namespace jpr
