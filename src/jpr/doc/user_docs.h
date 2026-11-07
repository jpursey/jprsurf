// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "absl/types/span.h"

namespace jpr {

// One generated page of the user docs.
struct UserDocPage {
  std::string path;  // In docs/user, such as "devices/XTouch.md".
  std::string text;
};

// Returns every generated page: in devices/, each device type's page and
// their index (see device_type_page.h), and in scene/, the scene's index, each
// property namespace's page, and the component kinds' (see scene_pages.h).
// Every .md file in their directories is one of them. docs/user itself, and
// its other directories, such as configs/, hold hand-written pages.
std::vector<UserDocPage> GenerateUserDocPages();

// Returns the path, as a page's, of each .md file in the user docs at `dir`
// that is in a directory one of `pages` is in, such as "devices/XTouch.md".
std::vector<std::string> ListPageFiles(const std::filesystem::path& dir,
                                       absl::Span<const UserDocPage> pages);

}  // namespace jpr
