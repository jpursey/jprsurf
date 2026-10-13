// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/call_counter.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/large_project.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/default_config/default_config_test.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;

//==============================================================================
// CallCountTest
//
// The REAPER calls the default config makes on the large project (see
// AddLargeProject()), as exact counts by cost (see CallCounts), so a change
// that makes more calls, or costlier ones, fails, and so does one that makes
// fewer, until its bounds are lowered with it.
//
// Each test counts only what it does: the counts of starting up are taken
// first. A gesture's counts include the runs that settle it, as the surface
// test helpers make them, and the steady state tests show what those cost.
//==============================================================================

class CallCountTest : public DefaultConfigTest {
 protected:
  static constexpr int kBus = 0;     // T1, with 31 receives.
  static constexpr int kFolder = 4;  // T5, with 9 tracks.

  CallCountTest() {
    AddLargeProject(&reaper_.GetProject());
    AddSurface();
    startup_ = calls_.TakeCounts();
  }

  // Runs the surface for a second, and returns its counts.
  CallCounts CountASecond() {
    calls_.TakeCounts();
    surface_->RunFor(absl::Seconds(1));
    return calls_.TakeCounts();
  }

  CallCounts startup_;
};

TEST_F(CallCountTest, StartingUp) {
  EXPECT_EQ(startup_, (CallCounts{.cheap = 3108, .moderate = 188}));
}

//------------------------------------------------------------------------------
// Steady state
//
// A second of runs, which includes one poll of the track filters: 256 cheap
// reads, two for each track. A count off by that much is the poll moving in or
// out of the second, rather than a change to what each run does.
//------------------------------------------------------------------------------

TEST_F(CallCountTest, TrackModeAtTheTop) {
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 3436, .moderate = 30}));
}

TEST_F(CallCountTest, TrackModeInAFolder) {
  DoublePressSelect(kFolder);
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 2386, .moderate = 30}));
}

// The timecode shows the play position, which moves on each run, as it does
// in REAPER.
TEST_F(CallCountTest, TrackModeWhilePlaying) {
  FakeProject& project = reaper_.GetProject();
  project.SetPlayState(1);
  RunUntilShown();
  calls_.TakeCounts();
  for (int i = 0; i < FakeReaper::kRunsPerSecond; ++i) {
    project.SetPlayPosition(project.GetPlayPosition() +
                            absl::ToDoubleSeconds(FakeReaper::GetRunTime()));
    surface_->Run();
  }
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 3496, .moderate = 60}));
}

TEST_F(CallCountTest, SendReceiveModeOnABus) {
  EnterSendMode(kBus);
  EXPECT_EQ(CountASecond(), (CallCounts{.cheap = 4306, .moderate = 30}));
}

//------------------------------------------------------------------------------
// Infrequent events
//------------------------------------------------------------------------------

TEST_F(CallCountTest, TrackListRefresh) {
  surface_->SetTrackListChange();
  RunUntilShown();
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 2660, .moderate = 131}));
}

TEST_F(CallCountTest, BankChange) {
  Tap(xtouch_, Button::kBankRight);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 850, .moderate = 8}));
}

TEST_F(CallCountTest, EnteringSendReceiveMode) {
  TapSelect(kBus);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAssignSend);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 1031, .moderate = 23}));
}

TEST_F(CallCountTest, ReturningToTrackMode) {
  EnterSendMode(kBus);
  calls_.TakeCounts();
  Tap(xtouch_, Button::kAssignTrack);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 899, .moderate = 24}));
}

}  // namespace
}  // namespace jpr
