// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/user_docs.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace jpr {
namespace {

using ::testing::UnorderedElementsAreArray;

// Returns the file's contents without carriage returns, or "" if it can't be
// read.
std::string ReadPage(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::string text{std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>()};
  return absl::StrReplaceAll(text, {{"\r", ""}});
}

TEST(UserDocsTest, CheckedInPagesAreUpToDate) {
  for (const UserDocPage& page : GenerateUserDocPages()) {
    const std::filesystem::path path =
        std::filesystem::path(JPR_USER_DOCS_DIR) / page.path;
    EXPECT_EQ(ReadPage(path), page.text)
        << path.generic_string()
        << " is out of date: build the update_user_docs target.";
  }
}

TEST(UserDocsTest, CheckedInPagesAreOnlyTheGeneratedOnes) {
  std::vector<std::string> paths;
  for (const UserDocPage& page : GenerateUserDocPages()) {
    paths.push_back(page.path);
  }
  EXPECT_THAT(ListPageFiles(JPR_USER_DOCS_DIR),
              UnorderedElementsAreArray(paths))
      << JPR_USER_DOCS_DIR
      << " has pages that aren't generated: build the update_user_docs "
         "target.";
}

}  // namespace
}  // namespace jpr
