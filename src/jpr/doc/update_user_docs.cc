// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// Writes every generated page of the user docs (see GenerateUserDocPages()) to
// the directory it takes as its only argument, and removes any other .md file
// in its directories, such as the page of a device type renamed.
// The update_user_docs target runs it, writing to docs/user.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "jpr/doc/user_docs.h"

namespace jpr {
namespace {

// Writes each page to the directory, creating the directories they are in.
// Returns false if one can't be written.
bool WritePages(const std::filesystem::path& dir,
                const std::vector<UserDocPage>& pages) {
  for (const UserDocPage& page : pages) {
    const std::filesystem::path path = dir / page.path;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      std::fprintf(stderr, "Could not create %s: %s\n",
                   path.parent_path().generic_string().c_str(),
                   error.message().c_str());
      return false;
    }
    std::ofstream file(path, std::ios::binary);
    file << page.text;
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

// Removes the .md files in the directories of docs/user that are none of the
// pages. Returns false if one can't be removed.
bool RemoveOtherPages(const std::filesystem::path& dir,
                      const std::vector<UserDocPage>& pages) {
  absl::flat_hash_set<std::string_view> paths;
  for (const UserDocPage& page : pages) {
    paths.insert(page.path);
  }
  for (const std::string& file : ListPageFiles(dir)) {
    if (paths.contains(file)) {
      continue;
    }
    const std::filesystem::path path = dir / file;
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
  const std::vector<jpr::UserDocPage> pages = jpr::GenerateUserDocPages();
  return jpr::WritePages(dir, pages) && jpr::RemoveOtherPages(dir, pages) ? 0
                                                                          : 1;
}
