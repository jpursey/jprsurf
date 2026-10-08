// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/recording_surface.h"

#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"

// What the surface records is checked by the contract tests that use it (see
// surface_notifier_contract_test.cc).

namespace jpr {
namespace {

TEST(RecordingSurfaceTest, OnlyOneExistsAtATime) {
  EXPECT_EQ(RecordingSurface::Get(), nullptr);
  {
    RecordingSurface surface;
    EXPECT_EQ(RecordingSurface::Get(), &surface);
    EXPECT_NONFATAL_FAILURE(RecordingSurface second, "A second");
    EXPECT_EQ(RecordingSurface::Get(), &surface);
  }
  EXPECT_EQ(RecordingSurface::Get(), nullptr);
}

TEST(RecordingSurfaceTest, RunCanReplaceWhatItDoes) {
  RecordingSurface surface;
  int first_runs = 0;
  int second_runs = 0;
  surface.SetOnRun([&] {
    ++first_runs;
    surface.SetOnRun([&] { ++second_runs; });
  });
  surface.Run();
  surface.Run();
  EXPECT_EQ(first_runs, 1);
  EXPECT_EQ(second_runs, 1);

  surface.SetOnRun([&] {
    ++first_runs;
    surface.SetOnRun(nullptr);
  });
  surface.Run();
  surface.Run();
  EXPECT_EQ(first_runs, 2);
  EXPECT_EQ(second_runs, 1);
}

}  // namespace
}  // namespace jpr
