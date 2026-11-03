// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/user_docs.h"

#include <filesystem>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
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

std::vector<std::string> ListPageFiles(const std::filesystem::path& dir) {
  std::vector<std::string> files;
  for (const std::filesystem::directory_entry& page_dir :
       std::filesystem::directory_iterator(dir)) {
    if (!page_dir.is_directory()) {
      continue;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(page_dir.path())) {
      if (entry.path().extension() == ".md") {
        files.push_back(absl::StrCat(page_dir.path().filename().string(), "/",
                                     entry.path().filename().string()));
      }
    }
  }
  return files;
}

}  // namespace jpr
