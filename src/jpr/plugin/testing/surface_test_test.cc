// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/testing/surface_test.h"

#include "absl/log/log.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;

// The presses, and the surface with devices, are tested with a config's
// devices (see DefaultConfigTest's tests). Naming tracks is tested with the
// fake (see FakeProject::AddTracks()).
using SurfaceTestTest = SurfaceTest;

TEST_F(SurfaceTestTest, ErrorsLoggedAreKeptToFailTheTest) {
  LOG(ERROR) << "Expected";
  EXPECT_THAT(log_error_guard_.TakeMessages(),
              ElementsAre(Field(&gb::LogRecorder::Message::text, "Expected")));
}

}  // namespace
}  // namespace jpr
