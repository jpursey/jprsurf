// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/control_surface.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_reaper.h"
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
    run_times.push_back(time.precise);
  }

  std::vector<double> run_times;
};

// The listener that exists, if any.
TestListener* g_listener = nullptr;

TestListener::TestListener() { g_listener = this; }

TestListener::~TestListener() { g_listener = nullptr; }

std::unique_ptr<ControlSurfaceListener> CreateTestListener(
    std::string_view config) {
  return std::make_unique<TestListener>();
}

ControlSurface::Type MakeType(std::filesystem::path profile_path = {}) {
  return {.type_string = "TEST",
          .description = "Test surface",
          .create_listener = &CreateTestListener,
          .profile_path = std::move(profile_path)};
}

class ControlSurfaceTest : public ::testing::Test {
 protected:
  // Registers the test type, and adds a surface of it.
  std::unique_ptr<TestControlSurface> AddTestSurface(
      std::filesystem::path profile_path = {}) {
    EXPECT_TRUE(ControlSurface::Register(reaper_.GetPluginInfo(),
                                         MakeType(std::move(profile_path))));
    std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
    EXPECT_NE(surface, nullptr);
    return surface;
  }

  FakeReaper reaper_;
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
  EXPECT_EQ(g_listener->run_times.size(), 1);
}

TEST_F(ControlSurfaceTest, RunsWithREAPERsTime) {
  std::unique_ptr<TestControlSurface> surface = AddTestSurface();
  ASSERT_NE(g_listener, nullptr);

  surface->Run();
  const double first_time = reaper_.GetTime();
  surface->Run();
  EXPECT_THAT(g_listener->run_times,
              ElementsAre(first_time, reaper_.GetTime()));
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
  const std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) /
      "control_surface_test_profile.txt";
  std::filesystem::remove(path);
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(path);
  surface->Run();
  EXPECT_FALSE(std::filesystem::exists(path));

  surface.reset();
  EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace jpr
