// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "gb/file/memory_file_protocol.h"
#include "gb/profile/profile_timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/file_system.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::StartsWith;

class ReaperProfilerTest : public ::testing::Test {
 protected:
  ReaperProfilerTest() {
    GetFileSystem()->Register(std::make_unique<gb::MemoryFileProtocol>(),
                              "profile");
  }

  FakeReaper reaper_;
};

TEST_F(ReaperProfilerTest, TheProfileShowsTheExtensionsNameAndBuild) {
  {
    ReaperProfiler profiler({.path = "profile:/test.txt",
                             .name = "Test",
                             .build = "abc1234 (modified)"});
  }

  std::string profile;
  ASSERT_TRUE(GetFileSystem()->ReadFile("profile:/test.txt", &profile));
  std::vector<std::string_view> header = absl::StrSplit(profile, '\n');
  ASSERT_GE(static_cast<int>(header.size()), 4);
  header.resize(4);
  EXPECT_THAT(
      header,
      ElementsAre("Test profile", "Build:    abc1234 (modified)",
                  StartsWith("Date:     "), StartsWith("Per run:  Test ")));
}

// A REAPER call in a run is REAPER's time, and the rest of the run is the
// extension's.
TEST_F(ReaperProfilerTest, SplitsEachRunsTimeBetweenTheExtensionAndReaper) {
  ReaperProfiler profiler({.path = "profile:/test.txt", .name = "Test"});
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

TEST_F(ReaperProfilerTest, FormatsRunTimesInMicrosecondsUnderAMillisecond) {
  EXPECT_EQ(FormatRunTime(absl::Nanoseconds(1530)), "1.5us");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(999)), "999.0us");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(12300)), "12.3ms");
}

TEST_F(ReaperProfilerTest, FormatsRunTimesUnderAMicrosecondInNanoseconds) {
  EXPECT_EQ(FormatRunTime(absl::Nanoseconds(7)), "7ns");
  EXPECT_EQ(FormatRunTime(absl::Nanoseconds(999)), "999ns");
  EXPECT_EQ(FormatRunTime(absl::ZeroDuration()), "0ns");
}

TEST_F(ReaperProfilerTest, FormatsNegativeRunTimesInTheUnitOfTheirSize) {
  EXPECT_EQ(FormatRunTime(absl::Nanoseconds(-40)), "-40ns");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(-344)), "-344.0us");
  EXPECT_EQ(FormatRunTime(absl::Microseconds(-1500)), "-1.5ms");
}

}  // namespace
}  // namespace jpr
