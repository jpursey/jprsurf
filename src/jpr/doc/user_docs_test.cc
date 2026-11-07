// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/user_docs.h"

#include <cstdio>
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

using ::testing::ElementsAre;
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
  const std::vector<UserDocPage> pages = GenerateUserDocPages();
  std::vector<std::string> paths;
  for (const UserDocPage& page : pages) {
    paths.push_back(page.path);
  }
  EXPECT_THAT(ListPageFiles(JPR_USER_DOCS_DIR, pages),
              UnorderedElementsAreArray(paths))
      << JPR_USER_DOCS_DIR
      << " has pages that aren't generated: build the update_user_docs "
         "target.";
}

// Only the directories the pages are in hold generated pages, so a
// hand-written page in docs/user, or in another directory of it, isn't listed,
// even with a page in docs/user itself.
TEST(UserDocsTest, PageFilesAreOnlyInThePagesDirectories) {
  const std::filesystem::path dir =
      std::filesystem::path(::testing::TempDir()) / "user_docs_test";
  std::filesystem::remove_all(dir);
  for (const char* file : {"guide.md", "devices/Unit.md", "devices/Unit.png",
                           "configs/example.md"}) {
    const std::filesystem::path path = dir / file;
    std::filesystem::create_directories(path.parent_path());
    std::FILE* stream = std::fopen(path.string().c_str(), "wb");
    ASSERT_NE(stream, nullptr) << path.generic_string();
    std::fclose(stream);
  }

  const UserDocPage pages[] = {
      {"devices/Other.md", ""}, {"scene/README.md", ""}, {"Top.md", ""}};
  EXPECT_THAT(ListPageFiles(dir, pages), ElementsAre("devices/Unit.md"));
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace jpr
