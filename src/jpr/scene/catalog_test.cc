// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/catalog.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/track.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_list.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;

using Type = ViewProperty::Type;

// Returns the namespace in the catalog with the name, or an empty one (failing
// the test) if there is none.
const PropertyNamespaceInfo& GetNamespace(std::string_view name) {
  for (const PropertyNamespaceInfo& info : GetPropertyCatalog()) {
    if (info.name == name) {
      return info;
    }
  }
  ADD_FAILURE() << "No namespace " << name;
  static const PropertyNamespaceInfo kNoNamespace;
  return kNoNamespace;
}

// Expects each row's property, which `get` returns, to have the row's type.
template <typename GetProperty>
void ExpectTypes(absl::Span<const PropertyInfo> rows, GetProperty get) {
  EXPECT_FALSE(rows.empty());
  for (const PropertyInfo& row : rows) {
    const ViewProperty* property = get(row);
    if (property == nullptr) {
      ADD_FAILURE() << "No property " << row.name;
      continue;
    }
    EXPECT_EQ(property->GetType(), row.type) << row.name;
  }
}

using CatalogTest = SceneTest;

TEST_F(CatalogTest, AccessesHaveTheirNames) {
  EXPECT_EQ(GetPropertyAccessName(PropertyAccess::kRead), "read");
  EXPECT_EQ(GetPropertyAccessName(PropertyAccess::kWrite), "write");
  EXPECT_EQ(GetPropertyAccessName(PropertyAccess::kTrigger), "trigger");
}

TEST_F(CatalogTest, NamespacesAreTheBuiltInOnes) {
  std::vector<std::string_view> names;
  for (const PropertyNamespaceInfo& info : GetPropertyCatalog()) {
    names.push_back(info.name);
    EXPECT_FALSE(info.description.empty()) << info.name;
  }
  EXPECT_THAT(names,
              ElementsAre(kCmdNamespace, kStateNamespace, kModNamespace,
                          kTrackNamespace, kRouteNamespace, kViewNamespace));
}

TEST_F(CatalogTest, RowsAreNamedAndDescribed) {
  absl::flat_hash_set<std::string_view> names;
  for (const PropertyNamespaceInfo& info : GetPropertyCatalog()) {
    for (const PropertyInfo& property : info.properties) {
      EXPECT_TRUE(property.name.starts_with(info.name)) << property.name;
      EXPECT_TRUE(names.insert(property.name).second) << property.name;
      EXPECT_FALSE(property.description.empty()) << property.name;
      EXPECT_EQ(property.view_need.has_value(), info.name == kViewNamespace)
          << property.name;

      // Only an action is triggered, and anything else can be read.
      if (property.type == Type::kAction) {
        EXPECT_EQ(property.access, kTriggerAccess) << property.name;
      } else {
        EXPECT_TRUE(property.access.IsSet(PropertyAccess::kRead))
            << property.name;
        EXPECT_FALSE(property.access.IsSet(PropertyAccess::kTrigger))
            << property.name;
      }
    }
    for (const ReferenceInfo& reference : info.references) {
      EXPECT_TRUE(reference.name.starts_with(info.name)) << reference.name;
      EXPECT_TRUE(names.insert(reference.name).second) << reference.name;
      EXPECT_FALSE(reference.description.empty()) << reference.name;
      EXPECT_NE(reference.kind, SubjectKind::kNone) << reference.name;
    }
  }
}

TEST_F(CatalogTest, StateAndModifierPropertiesHaveTheirTypes) {
  for (std::string_view name_space : {kStateNamespace, kModNamespace}) {
    ExpectTypes(GetNamespace(name_space).properties,
                [this](const PropertyInfo& row) {
                  return GetScene()->GetProperty(row.name);
                });
  }
}

TEST_F(CatalogTest, StateReferencesAreTheScenes) {
  absl::Span<const ReferenceInfo> rows =
      GetNamespace(kStateNamespace).references;
  EXPECT_FALSE(rows.empty());
  for (const ReferenceInfo& row : rows) {
    const ViewReference* reference = GetScene()->GetReference(row.name);
    ASSERT_NE(reference, nullptr) << row.name;
    EXPECT_EQ(reference->GetKind(), row.kind) << row.name;
  }
}

TEST_F(CatalogTest, TrackPropertiesHaveTheirTypes) {
  // A track's properties are the fields of a track reference.
  ExpectTypes(
      GetNamespace(kTrackNamespace).properties,
      [this](const PropertyInfo& row) {
        return GetScene()->GetProperty(absl::StrCat(
            Scene::kMasterTrack, ".", row.name.substr(kTrackNamespace.size())));
      });
}

TEST_F(CatalogTest, RoutePropertiesHaveTheirTypes) {
  const PropertyNamespaceInfo& info = GetNamespace(kRouteNamespace);
  RouteProperties route(&GetScene()->GetTrackActions());
  ExpectTypes(info.properties, [&route](const PropertyInfo& row) {
    return route.GetProperty(row.name);
  });

  // The route's references have the fields of their kind of subject.
  EXPECT_FALSE(info.references.empty());
  for (const ReferenceInfo& row : info.references) {
    ASSERT_EQ(row.kind, SubjectKind::kTrack) << row.name;
    EXPECT_NE(route.GetProperty(absl::StrCat(row.name, ".name")), nullptr)
        << row.name;
  }
}

TEST_F(CatalogTest, ViewPropertiesAreOnTheViewsThatNeedThem) {
  ASSERT_NE(GetScene()->AddTrackReference("user:folder"), nullptr);
  ASSERT_NE(GetScene()->AddTrackReference("user:track"), nullptr);
  View* root = GetScene()->GetRootView();

  // Adds a view bound to the reference, with the list, and returns it.
  auto add_list = [root](std::string_view name, std::string_view reference,
                         View::ListConfig list) {
    return root->AddChildView(
        name, {.subject = View::ReferenceSubject{std::string(reference)},
               .list = std::move(list)});
  };
  View* tracks =
      add_list("tracks", "user:folder", {.items = View::ChildTracks{}});
  View* master_tracks = add_list("master_tracks", Scene::kMasterTrack,
                                 {.items = View::ChildTracks{}});
  View* routes = add_list("routes", "user:track", {.items = View::Routes{}});
  View* master_routes =
      add_list("master_routes", Scene::kMasterTrack, {.items = View::Routes{}});
  ASSERT_NE(tracks, nullptr);
  ASSERT_NE(master_tracks, nullptr);
  ASSERT_NE(routes, nullptr);
  ASSERT_NE(master_routes, nullptr);
  auto add_item = [](View* view) {
    return view->AddChildView("item", {.subject = View::ListItemSubject{}});
  };

  // Each view, and the needs it meets. A view bound to state:master_track
  // can't set it.
  struct Case {
    View* view;
    std::vector<ViewNeed> needs;
  };
  const Case cases[] = {
      {root, {}},
      {tracks, {ViewNeed::kList, ViewNeed::kChildTracksList}},
      {add_item(tracks), {ViewNeed::kChildTracksItem}},
      {master_tracks, {ViewNeed::kList}},
      {add_item(master_tracks), {}},
      {routes, {ViewNeed::kList, ViewNeed::kRoutesList}},
      {add_item(routes), {ViewNeed::kRoutesItem}},
      {master_routes, {ViewNeed::kList, ViewNeed::kRoutesList}},
      {add_item(master_routes), {}},
  };
  const PropertyNamespaceInfo& info = GetNamespace(kViewNamespace);
  for (const Case& test_case : cases) {
    ASSERT_NE(test_case.view, nullptr);
    for (const PropertyInfo& row : info.properties) {
      ViewProperty* property = test_case.view->GetProperty(row.name);
      EXPECT_EQ(property != nullptr,
                absl::c_linear_search(test_case.needs, *row.view_need))
          << test_case.view->GetName() << " " << row.name;
      if (property != nullptr) {
        EXPECT_EQ(property->GetType(), row.type) << row.name;
      }
    }
  }
}

TEST_F(CatalogTest, ComponentKindsAreDescribed) {
  std::vector<std::string_view> names;
  for (const ComponentKindInfo& kind : GetComponentKinds()) {
    names.push_back(kind.name);
    EXPECT_FALSE(kind.description.empty()) << kind.name;
    EXPECT_TRUE(kind.top_level || kind.on_view) << kind.name;
    EXPECT_EQ(kind.name_space,
              kind.name == "modifier" ? kModNamespace : kUserNamespace)
        << kind.name;
    const bool has_new_names =
        absl::c_any_of(kind.params, [](const ParamInfo& param) {
          return param.kind == ParamKind::kNewNameList;
        });
    for (const ParamInfo& param : kind.params) {
      EXPECT_FALSE(param.name.empty()) << kind.name;
      EXPECT_FALSE(param.description.empty()) << kind.name << " " << param.name;

      // Choices are given exactly for a choice, a value name names one of the
      // kind's own list, and only a kind declared in both places has a
      // parameter for one of them.
      EXPECT_EQ(param.choices.empty(), param.kind != ParamKind::kChoice)
          << kind.name << " " << param.name;
      if (param.kind == ParamKind::kValueName) {
        EXPECT_TRUE(has_new_names) << kind.name << " " << param.name;
      }
      if (param.use == ParamUse::kTopLevelOnly) {
        EXPECT_TRUE(kind.top_level && kind.on_view)
            << kind.name << " " << param.name;
      }
    }
  }
  EXPECT_THAT(names,
              ElementsAre("modifier", "enumerated_value", "track_reference",
                          "track_anchor", "track_pick"));
}

TEST_F(CatalogTest, TrackAnchorActionsAreTheTrackToggles) {
  absl::Span<const ComponentKindInfo> kinds = GetComponentKinds();
  auto anchor = absl::c_find_if(kinds, [](const ComponentKindInfo& kind) {
    return kind.name == "track_anchor";
  });
  ASSERT_NE(anchor, kinds.end());
  auto action = absl::c_find_if(anchor->params, [](const ParamInfo& param) {
    return param.name == "action";
  });
  ASSERT_NE(action, anchor->params.end());

  // Each action is named by the track: toggle it acts on, one for each track
  // action.
  EXPECT_EQ(action->choices.size(), std::size(kTrackBoolProperties));
  absl::Span<const PropertyInfo> track_properties =
      GetNamespace(kTrackNamespace).properties;
  for (std::string_view choice : action->choices) {
    auto property =
        absl::c_find_if(track_properties, [choice](const PropertyInfo& row) {
          return row.name == absl::StrCat(kTrackNamespace, choice);
        });
    ASSERT_NE(property, track_properties.end()) << choice;
    EXPECT_EQ(property->type, Type::kToggle) << choice;
  }
}

}  // namespace
}  // namespace jpr
