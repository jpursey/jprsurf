// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/control_surface.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "gb/file/memory_file_protocol.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/file_system.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/surface_notifier.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/common/track_cache.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

// A listener that records the runs its surface passes it.
class TestListener final : public ControlSurfaceListener {
 public:
  TestListener();
  ~TestListener() override;

  void OnRun(const RunTime& time) override {
    run_times_secs.push_back(time.precise_secs);
    if (on_run) {
      on_run();
    }
  }

  std::vector<double> run_times_secs;

  // Called on each run, after it is recorded, if set.
  absl::AnyInvocable<void()> on_run;
};

// The listener that exists, if any.
TestListener* g_listener = nullptr;

TestListener::TestListener() { g_listener = this; }

TestListener::~TestListener() { g_listener = nullptr; }

std::unique_ptr<ControlSurfaceListener> CreateTestListener(
    std::string_view config) {
  return std::make_unique<TestListener>();
}

ControlSurface::Type MakeType(std::string profile_path = {}) {
  return {.type_string = "TEST",
          .description = "Test surface",
          .create_listener = &CreateTestListener,
          .profile = {.path = std::move(profile_path)}};
}

class ControlSurfaceTest : public ::testing::Test {
 protected:
  // Registers the test type, and adds a surface of it.
  std::unique_ptr<TestControlSurface> AddTestSurface(
      std::string profile_path = {}) {
    EXPECT_TRUE(ControlSurface::Register(reaper_.GetPluginInfo(),
                                         MakeType(std::move(profile_path))));
    std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
    EXPECT_NE(surface, nullptr);
    return surface;
  }

  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
};

TEST_F(ControlSurfaceTest, RegistersOneType) {
  EXPECT_TRUE(ControlSurface::Register(reaper_.GetPluginInfo(), MakeType()));
  EXPECT_FALSE(ControlSurface::Register(reaper_.GetPluginInfo(), MakeType()));
}

TEST_F(ControlSurfaceTest, CreatesAndDestroysItsListener) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  EXPECT_NE(g_listener, nullptr);

  surface.reset();
  EXPECT_EQ(g_listener, nullptr);
}

TEST_F(ControlSurfaceTest, RefusesASecondSurface) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();

  EXPECT_EQ(reaper_.AddSurface(), nullptr);
  EXPECT_THAT(reaper_.GetConsoleText(), HasSubstr("is already running"));

  // The first surface is untouched.
  ASSERT_NE(g_listener, nullptr);
  surface->Run();
  EXPECT_EQ(g_listener->run_times_secs.size(), 1);
}

TEST_F(ControlSurfaceTest, RunsWithREAPERsTime) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  ASSERT_NE(g_listener, nullptr);

  surface->Run();
  const double first_time_secs = reaper_.GetTimeSecs();
  surface->Run();
  EXPECT_THAT(g_listener->run_times_secs,
              ElementsAre(first_time_secs, reaper_.GetTimeSecs()));
}

TEST_F(ControlSurfaceTest, RunsTheListenerInADeferredRefresh) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  ASSERT_NE(g_listener, nullptr);
  bool prevented = false;
  g_listener->on_run = [&] { prevented = reaper_.IsUIRefreshPrevented(); };

  surface->Run();
  EXPECT_TRUE(prevented);
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
}

// REAPER calls back for a deferred change as the run's deferred refresh ends.
TEST_F(ControlSurfaceTest, FollowsTheRunsChangesAfterTheListener) {
  reaper_.GetProject().AddTrack("A");
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  ASSERT_NE(g_listener, nullptr);
  surface->Run();
  const int64_t version = TrackCache::Get().GetSelectionVersion();
  int64_t version_in_run = -1;
  g_listener->on_run = [&] {
    SetTrackSelected(GetTrack(nullptr, 0), true);
    version_in_run = TrackCache::Get().GetSelectionVersion();
  };

  surface->Run();
  EXPECT_EQ(version_in_run, version);
  EXPECT_GT(TrackCache::Get().GetSelectionVersion(), version);
}

TEST_F(ControlSurfaceTest, RefreshesTrackCacheOnTheRunAfterTheTrackList) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  surface->Run();
  EXPECT_EQ(TrackCache::Get().GetMasterTrack(), nullptr);

  surface->SetTrackListChange();
  EXPECT_EQ(TrackCache::Get().GetMasterTrack(), nullptr);

  surface->Run();
  Track* master = TrackCache::Get().GetMasterTrack();
  ASSERT_NE(master, nullptr);
  EXPECT_EQ(master->GetName(), reaper_.GetProject().GetMasterTrack()->name);
}

// TrackCache keeps a project's tracks by GUID while another project tab is
// current, and has them again on switching back.
TEST_F(ControlSurfaceTest, KeepsTracksAcrossProjectTabs) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  surface->SetTrackListChange();
  surface->Run();
  Track* first_master = TrackCache::Get().GetMasterTrack();
  ASSERT_NE(first_master, nullptr);

  // REAPER reports a track list change on switching tabs.
  reaper_.AddProject();
  surface->SetTrackListChange();
  surface->Run();
  EXPECT_NE(TrackCache::Get().GetMasterTrack(), first_master);

  reaper_.SwitchProjectTo(0);
  surface->SetTrackListChange();
  surface->Run();
  EXPECT_EQ(TrackCache::Get().GetMasterTrack(), first_master);
  EXPECT_TRUE(first_master->Exists());
}

TEST_F(ControlSurfaceTest, WritesItsProfileWhenTheTypeHasAPath) {
  ASSERT_TRUE(GetFileSystem()->Register(
      std::make_unique<gb::MemoryFileProtocol>(), "profile"));
  std::unique_ptr<TestControlSurface> surface =
      AddTestSurface("profile:/test.txt");
  surface->Run();
  EXPECT_FALSE(GetFileSystem()->IsValidFile("profile:/test.txt"));

  surface.reset();
  EXPECT_TRUE(GetFileSystem()->IsValidFile("profile:/test.txt"));
}

}  // namespace
}  // namespace jpr
