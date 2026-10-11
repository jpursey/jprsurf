// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::StartsWith;

TEST(ReaperProfilerTest, TheProfileShowsTheExtensionsNameAndBuild) {
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / "reaper_profiler_test.txt";
  std::filesystem::remove(path);
  FakeReaper reaper;
  {
    ReaperProfiler profiler(
        {.path = path, .name = "Test", .build = "abc1234 (modified)"});
  }

  std::ifstream file(path);
  std::vector<std::string> header;
  std::string line;
  for (int i = 0; i < 4 && std::getline(file, line); ++i) {
    header.push_back(line);
  }
  EXPECT_THAT(
      header,
      ElementsAre("Test profile", "Build:    abc1234 (modified)",
                  StartsWith("Date:     "), StartsWith("Per run:  Test ")));
}

}  // namespace
}  // namespace jpr
