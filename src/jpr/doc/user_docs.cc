// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/user_docs.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "jpr/device/device_types.h"
#include "jpr/doc/device_type_page.h"
#include "jpr/doc/scene_pages.h"
#include "jpr/scene/catalog.h"

namespace jpr {

std::vector<UserDocPage> GenerateUserDocPages() {
  std::vector<UserDocPage> pages;
  for (const DeviceType& type : GetDeviceTypes()) {
    pages.push_back({absl::StrCat("devices/", GetDeviceTypePageName(type)),
                     GenerateDeviceTypePage(type)});
  }
  pages.push_back({absl::StrCat("devices/", kDeviceTypesIndexPageName),
                   GenerateDeviceTypesIndexPage(GetDeviceTypes())});
  pages.push_back(
      {absl::StrCat("scene/", kSceneIndexPageName),
       GenerateSceneIndexPage(GetPropertyCatalog(), GetComponentKinds())});
  for (const PropertyNamespaceInfo& info : GetPropertyCatalog()) {
    pages.push_back(
        {absl::StrCat("scene/", GetPropertyNamespacePageName(info.name)),
         GeneratePropertyNamespacePage(info)});
  }
  pages.push_back({absl::StrCat("scene/", kComponentKindsPageName),
                   GenerateComponentKindsPage(GetComponentKinds())});
  return pages;
}

std::vector<std::string> ListPageFiles(const std::filesystem::path& dir,
                                       absl::Span<const UserDocPage> pages) {
  absl::flat_hash_set<std::string> page_dirs;
  for (const UserDocPage& page : pages) {
    // docs/user itself holds hand-written pages, so it is never listed.
    std::string page_dir =
        std::filesystem::path(page.path).parent_path().string();
    if (!page_dir.empty()) {
      page_dirs.insert(std::move(page_dir));
    }
  }
  std::vector<std::string> files;
  for (const std::string& page_dir : page_dirs) {
    // A directory that doesn't exist yet has no files.
    std::error_code error;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(dir / page_dir, error)) {
      if (entry.path().extension() == ".md") {
        files.push_back(
            absl::StrCat(page_dir, "/", entry.path().filename().string()));
      }
    }
  }
  return files;
}

}  // namespace jpr
