// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "gb/profile/profile_timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Gt;
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

// A REAPER call in a run is REAPER's time, and the rest of the run is the
// extension's.
TEST(ReaperProfilerTest, SplitsEachRunsTimeBetweenTheExtensionAndReaper) {
  FakeReaper reaper;
  ReaperProfiler profiler({.path = std::filesystem::path(::testing::TempDir()) /
                                   "reaper_profiler_split_test.txt",
                           .name = "Test"});
  ReaperProfiler::RunSplit split = profiler.GetRunSplit();
  EXPECT_EQ(split.runs, 0);
  EXPECT_EQ(split.extension, absl::ZeroDuration());
  EXPECT_EQ(split.reaper, absl::ZeroDuration());

  for (int i = 0; i < 2; ++i) {
    gb::ProfileFrame<"ReaperProfilerTest::Run"> frame;
    CountTracks(nullptr);
  }
  split = profiler.GetRunSplit();
  EXPECT_EQ(split.runs, 2);
  EXPECT_THAT(split.extension, Gt(absl::ZeroDuration()));
  EXPECT_THAT(split.reaper, Gt(absl::ZeroDuration()));

  profiler.Reset();
  EXPECT_EQ(profiler.GetRunSplit().runs, 0);
}

TEST(ReaperProfilerTest, FormatsRunTimesInMicrosecondsUnderAMillisecond) {
  EXPECT_EQ(FormatRunTime(absl::Nanoseconds(1530)), "1.5us");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(999)), "999.0us");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(12300)), "12.3ms");
}

}  // namespace
}  // namespace jpr
