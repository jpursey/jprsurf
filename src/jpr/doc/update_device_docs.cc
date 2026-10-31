// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// Writes every device type's page (see GenerateDeviceTypePage()), named by the
// type, such as XTouch.md, to the directory it takes as its only argument, and
// removes any other page there, such as that of a type renamed. The
// update_device_docs target runs it, writing to docs/user/devices.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "absl/container/flat_hash_set.h"
#include "jpr/device/device_types.h"
#include "jpr/doc/device_type_page.h"

namespace jpr {
namespace {

// Writes every type's page to the directory. Returns false if one can't be
// written.
bool WritePages(const std::filesystem::path& dir) {
  for (const DeviceType& type : GetDeviceTypes()) {
    const std::filesystem::path path = dir / GetDeviceTypePageName(type);
    std::ofstream file(path, std::ios::binary);
    file << GenerateDeviceTypePage(type);
    file.close();
    if (!file) {
      std::fprintf(stderr, "Could not write %s\n",
                   path.generic_string().c_str());
      return false;
    }
    std::printf("Wrote %s\n", path.generic_string().c_str());
  }
  return true;
}

// Removes the pages in the directory that are no type's. Returns false if one
// can't be removed.
bool RemoveOtherPages(const std::filesystem::path& dir) {
  absl::flat_hash_set<std::string> names;
  for (const DeviceType& type : GetDeviceTypes()) {
    names.insert(GetDeviceTypePageName(type));
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(dir)) {
    const std::filesystem::path& path = entry.path();
    if (path.extension() != ".md" || names.contains(path.filename().string())) {
      continue;
    }
    std::error_code error;
    if (!std::filesystem::remove(path, error)) {
      std::fprintf(stderr, "Could not remove %s: %s\n",
                   path.generic_string().c_str(), error.message().c_str());
      return false;
    }
    std::printf("Removed %s\n", path.generic_string().c_str());
  }
  return true;
}

}  // namespace
}  // namespace jpr

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::fprintf(stderr, "Usage: %s <output directory>\n", argv[0]);
    return 1;
  }
  const std::filesystem::path dir = argv[1];
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) {
    std::fprintf(stderr, "Could not create %s: %s\n",
                 dir.generic_string().c_str(), error.message().c_str());
    return 1;
  }
  return jpr::WritePages(dir) && jpr::RemoveOtherPages(dir) ? 0 : 1;
}
