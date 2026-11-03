// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace jpr {

// One generated page of the user docs.
struct UserDocPage {
  std::string path;  // In docs/user, such as "devices/XTouch.md".
  std::string text;
};

// Returns every generated page: in devices/, each device type's page and
// their index (see device_type_page.h), and in scene/, the scene's index, each
// property namespace's page, and the component kinds' (see scene_pages.h).
// Every .md file in a directory of docs/user, as opposed to in docs/user
// itself, is one of them.
std::vector<UserDocPage> GenerateUserDocPages();

// Returns the path, as a page's, of each .md file in a directory of the user
// docs at `dir`, such as "devices/XTouch.md".
std::vector<std::string> ListPageFiles(const std::filesystem::path& dir);

}  // namespace jpr
