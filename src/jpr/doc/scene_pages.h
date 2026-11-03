// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/scene/catalog.h"

namespace jpr {

// The names of the scene's index page, and of the component kinds' page. The
// scene's pages are these and each namespace's page, in docs/user/scene.
inline constexpr std::string_view kSceneIndexPageName = "README.md";
inline constexpr std::string_view kComponentKindsPageName = "components.md";

// Returns the scene's index page, in markdown: a table of the namespaces, each
// linking to its page, with its description, then a link to the component
// kinds' page, with their names.
std::string GenerateSceneIndexPage(
    absl::Span<const PropertyNamespaceInfo> catalog,
    absl::Span<const ComponentKindInfo> kinds);

// Returns the namespace's page, in markdown: its description, then a table of
// its properties, with each one's type, access, and description (and for a
// view: property, the views it is on), then a table of its references, with
// each one's fields, which link to their namespace's page.
std::string GeneratePropertyNamespacePage(const PropertyNamespaceInfo& info);

// Returns the name of the namespace's page file, such as "state.md" for
// "state:".
std::string GetPropertyNamespacePageName(std::string_view name_space);

// Returns the page of the component kinds, in markdown: a section for each
// kind, with its description, where it may be declared, and a table of its
// parameters, with what text each takes, whether it must be given, and its
// description.
std::string GenerateComponentKindsPage(
    absl::Span<const ComponentKindInfo> kinds);

}  // namespace jpr
