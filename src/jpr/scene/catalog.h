// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include "absl/types/span.h"
#include "gb/base/flags.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

//==============================================================================
// The catalog
//
// What the scene provides to a config: its built in properties and references
// by namespace, and the kinds of component a config can declare. The
// properties and references are the scene's own tables, which create them, so
// nothing the scene provides can be missing from the catalog. Checking a
// config and the user docs read it.
//==============================================================================

// What a mapping can do with a property.
enum class PropertyAccess {
  kRead,     // Show it on a control's outputs.
  kWrite,    // Set it from a control's inputs.
  kTrigger,  // Run it from a control's inputs, for an action.
};
using PropertyAccessFlags = gb::Flags<PropertyAccess>;

// Every access, in order.
inline constexpr PropertyAccess kPropertyAccesses[] = {
    PropertyAccess::kRead, PropertyAccess::kWrite, PropertyAccess::kTrigger};

inline constexpr PropertyAccessFlags kReadAccess = PropertyAccess::kRead;
inline constexpr PropertyAccessFlags kReadWriteAccess = {
    PropertyAccess::kRead, PropertyAccess::kWrite};
inline constexpr PropertyAccessFlags kTriggerAccess = PropertyAccess::kTrigger;

// Returns the access's name, as errors and docs name it: "read", "write", or
// "trigger".
std::string_view GetPropertyAccessName(PropertyAccess access);

// What a view: property needs of the view it is used in. A view has the
// properties of each need it meets.
enum class ViewNeed {
  kList,             // A list.
  kChildTracksList,  // A child tracks list, bound to a writable reference.
  kChildTracksItem,  // A list item view of such a list.
  kRoutesList,       // A routes list.
  kRoutesItem,       // A list item view of a routes list, bound to a writable
                     // reference.
};

// One built in property.
struct PropertyInfo {
  std::string_view name;  // Such as "state:can_redo".
  ViewProperty::Type type = ViewProperty::Type::kAction;
  PropertyAccessFlags access;
  std::string_view description;
  std::optional<ViewNeed> view_need;  // For a view: property only.
};

// One built in reference, whose fields are the properties of its kind of
// subject, such as track: for a track.
struct ReferenceInfo {
  std::string_view name;  // Such as "state:master_track".
  SubjectKind kind = SubjectKind::kNone;
  std::string_view description;
};

// The built in properties and references of a namespace.
struct PropertyNamespaceInfo {
  std::string_view name;  // Such as "state:".
  std::string_view description;
  absl::Span<const PropertyInfo> properties;
  absl::Span<const ReferenceInfo> references;
};

// Returns every namespace a config can name a built in property in: cmd:,
// state:, mod:, track:, route:, and view:. cmd: has no properties listed, as
// only REAPER knows which commands exist.
absl::Span<const PropertyNamespaceInfo> GetPropertyCatalog();

// What a component parameter's text is. Every parameter is written as text, so
// this is what a config may write in it.
enum class ParamKind {
  kNewName,         // A name the config makes up, such as a value's.
  kNewNameList,     // A list of names the config makes up.
  kValueName,       // One of the names of the component's kNewNameList.
  kTrackReference,  // A track reference: built in, or declared before it.
  kModifier,        // A modifier: built in, or declared.
  kChoice,          // One of the parameter's choices.
};

// Whether a component parameter must be given.
enum class ParamUse {
  kRequired,
  kOptional,
  kTopLevelOnly,  // Required at the top level, and not given on a view.
};

// One parameter of a kind of component.
struct ParamInfo {
  std::string_view name;
  ParamKind kind = ParamKind::kNewName;
  ParamUse use = ParamUse::kRequired;
  std::string_view description;  // What it does, rather than its form.
  absl::Span<const std::string_view> choices;  // For kChoice only.
};

// One kind of component a config can declare (see Components in
// docs/config_model.md), each with a name and its kind's parameters.
struct ComponentKindInfo {
  std::string_view name;        // Such as "enumerated_value".
  std::string_view name_space;  // Of the name it declares, such as "user:".
  bool top_level = false;       // Whether it may be declared at the top level,
  bool on_view = false;         // and on a view.
  std::string_view description;
  absl::Span<const ParamInfo> params;
};

// Returns every kind of component.
absl::Span<const ComponentKindInfo> GetComponentKinds();

//==============================================================================
// Building the tables
//==============================================================================

// Returns the info of each row of a table whose rows each have a PropertyInfo
// `info`, as a table's part of the catalog.
template <typename Row, int N>
constexpr std::array<PropertyInfo, N> GetPropertyInfo(const Row (&rows)[N]) {
  std::array<PropertyInfo, N> info = {};
  for (int i = 0; i < N; ++i) {
    info[i] = rows[i].info;
  }
  return info;
}

// Returns the row with the name of a table whose rows each have a PropertyInfo
// `info`, or null if there is none.
template <typename Row, int N>
constexpr const Row* FindRow(const Row (&rows)[N], std::string_view name) {
  for (const Row& row : rows) {
    if (row.info.name == name) {
      return &row;
    }
  }
  return nullptr;
}

// Returns true if every property has a name in the namespace with no '.', and
// no two have the same name.
constexpr bool AreValidProperties(absl::Span<const PropertyInfo> properties,
                                  std::string_view name_space) {
  for (auto it = properties.begin(); it != properties.end(); ++it) {
    if (!it->name.starts_with(name_space) ||
        it->name.find('.') != std::string_view::npos ||
        std::ranges::find(it + 1, properties.end(), it->name,
                          &PropertyInfo::name) != properties.end()) {
      return false;
    }
  }
  return true;
}

}  // namespace jpr
