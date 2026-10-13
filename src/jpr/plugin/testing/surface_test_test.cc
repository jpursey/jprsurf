// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/testing/surface_test.h"

#include "absl/log/log.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_track.h"

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

// The rules themselves are tested with the counter (see CallCounterTest).
TEST_F(SurfaceTestTest, RepeatingASetterFailsTheTest) {
  MediaTrack* track = ToMediaTrack(AddTracks(1)[0]);
  PreventUIRefresh(1);
  SetTrackUIMute(track, 1, 0);
  SetTrackUIMute(track, 0, 0);
  PreventUIRefresh(-1);
  EXPECT_NONFATAL_FAILURE(reaper_.EndEntryPoint(), "repeated SetTrackUIMute");
}

}  // namespace
}  // namespace jpr
