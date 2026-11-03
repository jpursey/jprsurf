// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/scene_pages.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "jpr/doc/markdown.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

namespace {

// Returns the views that have a view: property with the need.
std::string_view GetViewNeedText(ViewNeed need) {
  switch (need) {
    case ViewNeed::kList:
      return "Lists";
    case ViewNeed::kChildTracksList:
      return "Child tracks lists bound to a writable reference";
    case ViewNeed::kChildTracksItem:
      return "Item views of a child tracks list bound to a writable reference";
    case ViewNeed::kRoutesList:
      return "Routes lists";
    case ViewNeed::kRoutesItem:
      return "Item views of a routes list bound to a writable reference";
  }
  return "";
}

// Returns a link to the page of the namespace whose properties are the fields
// of a reference to the kind of subject, or "" if it has none.
std::string GetFieldsLink(SubjectKind kind) {
  const std::string_view name_space = GetSubjectNamespace(kind);
  if (name_space.empty()) {
    return "";
  }
  return absl::StrCat("[`", name_space, "`](",
                      GetPropertyNamespacePageName(name_space), ")");
}

// Returns what text the parameter, one of the kind's `params`, takes.
std::string GetParamKindText(const ParamInfo& param,
                             absl::Span<const ParamInfo> params) {
  switch (param.kind) {
    case ParamKind::kNewName:
      return "A new name";
    case ParamKind::kNewNameList:
      return "A list of new names";
    case ParamKind::kValueName: {
      std::vector<std::string_view> lists;
      for (const ParamInfo& list : params) {
        if (list.kind == ParamKind::kNewNameList) {
          lists.push_back(list.name);
        }
      }
      return absl::StrCat("One of the names in ", GetCodeList(lists));
    }
    case ParamKind::kTrackReference:
      return absl::StrCat("A track reference: [built in](",
                          GetPropertyNamespacePageName(kStateNamespace),
                          "#references), or declared before it");
    case ParamKind::kModifier:
      return absl::StrCat("A [modifier](",
                          GetPropertyNamespacePageName(kModNamespace),
                          "): built in, or declared");
    case ParamKind::kChoice:
      return absl::StrCat("One of ", GetCodeList(param.choices));
  }
  return "";
}

// Returns whether the parameter must be given.
std::string_view GetParamUseText(ParamUse use) {
  switch (use) {
    case ParamUse::kRequired:
      return "Required";
    case ParamUse::kOptional:
      return "Optional";
    case ParamUse::kTopLevelOnly:
      return "Required at the top level, and not given on a view";
  }
  return "";
}

// Returns where the kind may be declared, as a sentence.
std::string_view GetDeclaredText(const ComponentKindInfo& kind) {
  if (kind.top_level && kind.on_view) {
    return "Declared at the top level, or on a view.";
  }
  return kind.top_level ? "Declared at the top level." : "Declared on a view.";
}

}  // namespace

std::string GenerateSceneIndexPage(
    absl::Span<const PropertyNamespaceInfo> catalog,
    absl::Span<const ComponentKindInfo> kinds) {
  std::string page = absl::StrCat(
      GetGeneratedPageHeader(
          "the scene's property catalog and component kinds"),
      "\n# The scene\n\nWhat the scene provides to a config: the built in "
      "properties of each namespace, and the kinds of component a config can "
      "declare.\n\n## Properties\n\n| Namespace | Description |\n"
      "| --- | --- |\n");
  for (const PropertyNamespaceInfo& info : catalog) {
    absl::StrAppend(&page, "| [`", info.name, "`](",
                    GetPropertyNamespacePageName(info.name), ") | ",
                    GetCellText(info.description), " |\n");
  }
  std::vector<std::string_view> names;
  for (const ComponentKindInfo& kind : kinds) {
    names.push_back(kind.name);
  }
  absl::StrAppend(&page, "\n## Components\n\nThe [kinds of component](",
                  kComponentKindsPageName,
                  ") a config can declare: ", GetCodeList(names), ".\n");
  return page;
}

std::string GeneratePropertyNamespacePage(const PropertyNamespaceInfo& info) {
  // Only view: properties have a need, which says the views they are on.
  const bool has_views =
      absl::c_any_of(info.properties, [](const PropertyInfo& property) {
        return property.view_need.has_value();
      });
  std::string page = absl::StrCat(
      GetGeneratedPageHeader("the scene's property catalog"), "\n# `",
      info.name, "` properties\n\n", info.description, "\n");
  if (!info.properties.empty()) {
    absl::StrAppend(&page, "\n## Properties\n\n| Property | Type | Access | ",
                    has_views ? "Views | " : "", "Description |\n",
                    "| --- | --- | --- | ", has_views ? "--- | " : "",
                    "--- |\n");
  }
  for (const PropertyInfo& property : info.properties) {
    absl::StrAppend(&page, "| `", property.name, "` | ",
                    ViewProperty::GetTypeName(property.type), " | ",
                    GetFlagNames(property.access, kPropertyAccesses,
                                 &GetPropertyAccessName),
                    " | ");
    if (has_views) {
      absl::StrAppend(&page,
                      property.view_need.has_value()
                          ? GetViewNeedText(*property.view_need)
                          : "",
                      " | ");
    }
    absl::StrAppend(&page, GetCellText(property.description), " |\n");
  }
  if (!info.references.empty()) {
    absl::StrAppend(&page,
                    "\n## References\n\n| Reference | Fields | Description |\n"
                    "| --- | --- | --- |\n");
  }
  for (const ReferenceInfo& reference : info.references) {
    absl::StrAppend(&page, "| `", reference.name, "` | ",
                    GetFieldsLink(reference.kind), " | ",
                    GetCellText(reference.description), " |\n");
  }
  return page;
}

std::string GetPropertyNamespacePageName(std::string_view name_space) {
  return absl::StrCat(absl::StripSuffix(name_space, ":"), ".md");
}

std::string GenerateComponentKindsPage(
    absl::Span<const ComponentKindInfo> kinds) {
  std::string page = absl::StrCat(
      GetGeneratedPageHeader("the scene's component kinds"),
      "\n# Components\n\nThe kinds of component a config can declare. Every "
      "parameter is written as text, and its Value is what text it takes.\n");
  for (const ComponentKindInfo& kind : kinds) {
    absl::StrAppend(&page, "\n## `", kind.name, "`\n\n", kind.description,
                    "\n\n", GetDeclaredText(kind), "\n");
    if (kind.params.empty()) {
      absl::StrAppend(&page, "\nIt takes no parameters.\n");
      continue;
    }
    absl::StrAppend(&page,
                    "\n| Parameter | Value | Given | Description |\n"
                    "| --- | --- | --- | --- |\n");
    for (const ParamInfo& param : kind.params) {
      absl::StrAppend(&page, "| `", param.name, "` | ",
                      GetCellText(GetParamKindText(param, kind.params)), " | ",
                      GetParamUseText(param.use), " | ",
                      GetCellText(param.description), " |\n");
    }
  }
  return page;
}

}  // namespace jpr
