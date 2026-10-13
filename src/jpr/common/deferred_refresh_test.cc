// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/deferred_refresh.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"

namespace jpr {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Not;

constexpr int kAction = 40044;
constexpr int kOtherAction = 40045;

class DeferredRefreshTest : public ::testing::Test {
 protected:
  DeferredRefreshTest() {
    reaper_.GetProject().AddTrack("A");
    reaper_.GetPluginInfo().Register("csurf",
                                     RecordingSurface::GetRegistration());
    surface_ = reaper_.AddSurface();
    a_ = GetTrack(nullptr, 0);
  }

  // Returns the calls the surface got since the last time, and forgets them.
  std::vector<std::string> TakeCalls() {
    return RecordingSurface::Get()->TakeCalls();
  }

  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;
  MediaTrack* a_ = nullptr;
};

TEST_F(DeferredRefreshTest, DefersTheRefreshWhileItExists) {
  {
    DeferredRefresh deferred_refresh;
    EXPECT_TRUE(reaper_.IsUIRefreshPrevented());
    SetTrackUIMute(a_, 1, 0);
    EXPECT_THAT(TakeCalls(), Not(Contains("SetSurfaceMute(A, true)")));
  }
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
  EXPECT_THAT(TakeCalls(), Contains("SetSurfaceMute(A, true)"));
}

TEST_F(DeferredRefreshTest, NestedOnesEndWithTheOutermost) {
  {
    DeferredRefresh outer;
    {
      DeferredRefresh inner;
      SetTrackUIMute(a_, 1, 0);
    }
    EXPECT_TRUE(reaper_.IsUIRefreshPrevented());
    EXPECT_THAT(TakeCalls(), Not(Contains("SetSurfaceMute(A, true)")));
  }
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
  EXPECT_THAT(TakeCalls(), Contains("SetSurfaceMute(A, true)"));
}

TEST_F(DeferredRefreshTest, RunActionOutsideOneRunsIt) {
  std::optional<bool> prevented;
  reaper_.AddCommand({.id = kAction, .on_run = [&] {
                        prevented = reaper_.IsUIRefreshPrevented();
                      }});
  RunAction(kAction);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(kAction));
  EXPECT_EQ(prevented, false);
}

TEST_F(DeferredRefreshTest, RunActionInsideOnePaysTheRefreshAndResumesAfter) {
  std::optional<bool> prevented;
  std::vector<std::string> calls_before_action;
  reaper_.AddCommand({.id = kAction, .on_run = [&] {
                        prevented = reaper_.IsUIRefreshPrevented();
                        calls_before_action = TakeCalls();
                      }});
  {
    DeferredRefresh outer;
    DeferredRefresh inner;
    SetTrackUIMute(a_, 1, 0);
    RunAction(kAction);
    EXPECT_EQ(prevented, false);
    EXPECT_THAT(calls_before_action, Contains("SetSurfaceMute(A, true)"));

    // Deferring resumes, as deep as it was.
    EXPECT_TRUE(reaper_.IsUIRefreshPrevented());
    SetTrackUISolo(a_, 1, 0);
    EXPECT_THAT(TakeCalls(), Not(Contains("SetSurfaceSolo(A, true)")));
  }
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
  EXPECT_THAT(TakeCalls(), Contains("SetSurfaceSolo(A, true)"));
}

// REAPER may call back to the surface from inside an action, which may defer
// a refresh, or run an action, of its own.
TEST_F(DeferredRefreshTest, ActionsStartOutsideAnyDeferredRefresh) {
  reaper_.AddCommand({.id = kAction, .on_run = [&] {
                        DeferredRefresh deferred_refresh;
                        RunAction(kOtherAction);
                      }});
  {
    DeferredRefresh deferred_refresh;
    RunAction(kAction);
    EXPECT_TRUE(reaper_.IsUIRefreshPrevented());
  }
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(kAction, kOtherAction));
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
}

}  // namespace
}  // namespace jpr
