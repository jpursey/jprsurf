// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_reaper.h"

#include <array>

#include "absl/time/time.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track_cache.h"

namespace jpr {
namespace {

// A control surface that counts its runs.
class TestSurface final : public IReaperControlSurface {
 public:
  const char* GetTypeString() override { return "TEST"; }
  const char* GetDescString() override { return "Test surface"; }
  const char* GetConfigString() override { return ""; }
  void Run() override { ++run_count; }

  int run_count = 0;
};

IReaperControlSurface* CreateTestSurface(const char* type_string,
                                         const char* config_string,
                                         int* err_stats) {
  return new TestSurface;
}

reaper_csurf_reg_t g_test_surface_reg = {"TEST", "Test surface",
                                         &CreateTestSurface, nullptr};

TEST(FakeReaperTest, LoadsEveryFunctionOnTheList) {
  FakeReaper reaper;
#define JPR_EXPECT_LOADED(name) EXPECT_NE(::name, nullptr) << #name;
  JPR_REAPER_API(JPR_EXPECT_LOADED)
#undef JPR_EXPECT_LOADED
}

TEST(FakeReaperTest, UnloadsTheApiWhenDestroyed) {
  { FakeReaper reaper; }
  EXPECT_EQ(::time_precise, nullptr);
}

TEST(FakeReaperTest, FunctionNotFakedYetFailsTheTest) {
  FakeReaper reaper;
  EXPECT_NONFATAL_FAILURE(::AnyTrackSolo(nullptr),
                          "AnyTrackSolo isn't faked yet");
}

TEST(FakeReaperTest, RegisteringAnythingButASurfaceFailsTheTest) {
  FakeReaper reaper;
  EXPECT_NONFATAL_FAILURE(
      reaper.GetPluginInfo().Register("hookcommand", nullptr), "hookcommand");
}

TEST(FakeReaperTest, AddingASurfaceWithNoTypeFailsTheTest) {
  FakeReaper reaper;
  IReaperControlSurface* surface = nullptr;
  EXPECT_NONFATAL_FAILURE(surface = reaper.AddSurface(),
                          "No control surface type");
  EXPECT_EQ(surface, nullptr);
}

TEST(FakeReaperTest, RemovingAnUnknownSurfaceFailsTheTest) {
  FakeReaper reaper;
  TestSurface surface;
  EXPECT_NONFATAL_FAILURE(reaper.RemoveSurface(&surface),
                          "didn't create, or already removed");
}

TEST(FakeReaperTest, SurfaceLeftOpenFailsTheTest) {
  EXPECT_NONFATAL_FAILURE(
      {
        FakeReaper reaper;
        reaper.GetPluginInfo().Register("csurf", &g_test_surface_reg);
        reaper.AddSurface();
      },
      "still open");
}

TEST(FakeReaperTest, RunAdvancesTheClockAndRunsSurfaces) {
  FakeReaper reaper;
  reaper.GetPluginInfo().Register("csurf", &g_test_surface_reg);
  IReaperControlSurface* surface = reaper.AddSurface();
  ASSERT_NE(surface, nullptr);
  const int& run_count = static_cast<TestSurface*>(surface)->run_count;
  const double start_time = reaper.GetTime();
  EXPECT_EQ(::time_precise(), start_time);

  reaper.Run();
  EXPECT_EQ(run_count, 1);
  EXPECT_DOUBLE_EQ(::time_precise(), start_time + 1.0 / 30);

  reaper.RunFor(absl::Seconds(1));
  EXPECT_EQ(run_count, 31);
  EXPECT_DOUBLE_EQ(::time_precise(), start_time + 31.0 / 30);

  reaper.RunFor(absl::Milliseconds(10));
  EXPECT_EQ(run_count, 32);

  reaper.RemoveSurface(surface);
}

TEST(FakeReaperTest, MasterTrack) {
  FakeReaper reaper;
  FakeTrack* master = reaper.GetProject().GetMasterTrack();
  ASSERT_EQ(::GetMasterTrack(nullptr), ToMediaTrack(master));
  master->name = "Master";
  master->color = 0x01020304;
  master->volume = 0.5;
  master->pan = -0.25;
  master->mute = true;
  master->rec_arm = true;

  int flags = 0;
  EXPECT_STREQ(::GetTrackState(ToMediaTrack(master), &flags), "Master");
  EXPECT_EQ(flags, 8 | 64);
  EXPECT_EQ(::GetTrackColor(ToMediaTrack(master)), 0x01020304);
  double volume = 0.0;
  double pan = 0.0;
  EXPECT_TRUE(::GetTrackUIVolPan(ToMediaTrack(master), &volume, &pan));
  EXPECT_EQ(volume, 0.5);
  EXPECT_EQ(pan, -0.25);
}

TEST(FakeReaperTest, GuidsAreTheSameOnEveryRun) {
  for (int i = 0; i < 2; ++i) {
    FakeReaper reaper;
    std::array<char, 64> text = {};
    ::guidToString(::GetTrackGUID(::GetMasterTrack(nullptr)), text.data());
    EXPECT_STREQ(text.data(), "{00000001-0001-0000-0000-000000000000}");
  }
}

TEST(FakeReaperTest, UnknownTrackFailsTheTest) {
  FakeReaper reaper;
  FakeTrack other;
  EXPECT_NONFATAL_FAILURE(::GetTrackColor(ToMediaTrack(&other)),
                          "isn't a track in FakeReaper");
}

TEST(FakeReaperTest, NewProjectReplacesTheProject) {
  FakeReaper reaper;
  FakeTrack* old_master = reaper.GetProject().GetMasterTrack();

  FakeProject& project = reaper.NewProject();
  EXPECT_EQ(&reaper.GetProject(), &project);
  EXPECT_EQ(::GetMasterTrack(nullptr), ToMediaTrack(project.GetMasterTrack()));
  EXPECT_NE(project.GetMasterTrack(), old_master);
  EXPECT_FALSE(project.GetMasterTrack()->guid == old_master->guid);

  EXPECT_NONFATAL_FAILURE(::GetTrackColor(ToMediaTrack(old_master)),
                          "no longer open");
}

TEST(FakeReaperTest, ProjectTabsStayOpen) {
  FakeReaper reaper;
  FakeProject* first = &reaper.GetProject();
  FakeProject& second = reaper.AddProject();
  EXPECT_EQ(reaper.GetProjectCount(), 2);
  EXPECT_EQ(&reaper.GetProject(), &second);
  EXPECT_EQ(&reaper.GetProjectAt(0), first);
  EXPECT_EQ(&reaper.GetProjectAt(1), &second);
  EXPECT_EQ(::GetMasterTrack(nullptr), ToMediaTrack(second.GetMasterTrack()));

  // The first project's tracks are still there, and keep their pointers.
  first->GetMasterTrack()->color = 0x01010203;
  EXPECT_EQ(::GetTrackColor(ToMediaTrack(first->GetMasterTrack())), 0x01010203);
  EXPECT_EQ(::GetMasterTrack(ToReaProject(first)),
            ToMediaTrack(first->GetMasterTrack()));

  reaper.SwitchProjectTo(0);
  EXPECT_EQ(&reaper.GetProject(), first);
  EXPECT_EQ(::GetMasterTrack(nullptr), ToMediaTrack(first->GetMasterTrack()));
}

TEST(FakeReaperTest, NewProjectClosesOnlyTheCurrentTab) {
  FakeReaper reaper;
  FakeTrack* first_master = reaper.GetProject().GetMasterTrack();
  FakeTrack* second_master = reaper.AddProject().GetMasterTrack();

  reaper.NewProject();
  EXPECT_EQ(reaper.GetProjectCount(), 2);
  ::GetTrackColor(ToMediaTrack(first_master));
  EXPECT_NONFATAL_FAILURE(::GetTrackColor(ToMediaTrack(second_master)),
                          "no longer open");
}

TEST(FakeReaperTest, SwitchingToAMissingProjectFailsTheTest) {
  FakeReaper reaper;
  EXPECT_NONFATAL_FAILURE(reaper.SwitchProjectTo(1), "only 1 project(s) open");
}

TEST(FakeReaperTest, UnknownProjectFailsTheTest) {
  FakeReaper reaper;
  int other = 0;
  EXPECT_NONFATAL_FAILURE(
      ::GetMasterTrack(reinterpret_cast<ReaProject*>(&other)),
      "isn't an open project");
}

TEST(FakeReaperTest, RecordsConsoleText) {
  FakeReaper reaper;
  ::ShowConsoleMsg("Hello, ");
  ::ShowConsoleMsg("world\n");
  EXPECT_EQ(reaper.GetConsoleText(), "Hello, world\n");
}

// Each reset is registered beside its state (see TestReset), so this also
// shows the registrations are linked in.
TEST(FakeReaperTest, ResetsProcessStateBetweenFakes) {
  {
    FakeReaper reaper;
    TrackCache::Get().SetLastTouchedTrack(TrackCache::Get().GetStubTrack());
    SetModifiers(kModShift, true);
  }
  FakeReaper reaper;
  EXPECT_EQ(TrackCache::Get().GetLastTouchedTrack(), nullptr);
  EXPECT_EQ(GetModifiers(), 0);
}

}  // namespace
}  // namespace jpr
