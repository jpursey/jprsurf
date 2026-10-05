// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_reaper.h"

#include <array>
#include <functional>
#include <memory>
#include <string>

#include "absl/time/time.h"
#include "gb/base/function_hook.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/test_control_surface.h"
#include "jpr/common/track_cache.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// A control surface that counts its runs, and calls `on_run` in each.
class TestSurface final : public IReaperControlSurface {
 public:
  const char* GetTypeString() override { return "TEST"; }
  const char* GetDescString() override { return "Test surface"; }
  const char* GetConfigString() override { return ""; }
  void Run() override {
    ++run_count;
    on_run();
  }

  int run_count = 0;
  std::function<void()> on_run = [] {};
};

// The TestSurface created last.
TestSurface* g_test_surface = nullptr;

IReaperControlSurface* CreateTestSurface(const char* type_string,
                                         const char* config_string,
                                         int* err_stats) {
  g_test_surface = new TestSurface;
  return g_test_surface;
}

reaper_csurf_reg_t g_test_surface_reg = {"TEST", "Test surface",
                                         &CreateTestSurface, nullptr};

// Registers the test surface's type with `reaper`, and adds one.
std::unique_ptr<TestControlSurface> AddTestSurface(FakeReaper& reaper) {
  reaper.GetPluginInfo().Register("csurf", &g_test_surface_reg);
  return reaper.AddSurface();
}

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

// Stands in for time_precise(), returning a fixed time.
struct FixedTimeHook {
  static constexpr double kTime = 42.0;

  template <typename Function>
  double Call(Function original) {
    return kTime;
  }
};

TEST(FakeReaperTest, LoadingTheApiFromThePluginInfoKeepsHooks) {
  FakeReaper reaper;
  gb::FunctionHook<&time_precise, FixedTimeHook> hook;

  ASSERT_TRUE(LoadReaperApi(reaper.GetPluginInfo().GetFunc));
  EXPECT_EQ(time_precise(), FixedTimeHook::kTime);
}

TEST(FakeReaperTest, RegisteringAnythingButASurfaceFailsTheTest) {
  FakeReaper reaper;
  EXPECT_NONFATAL_FAILURE(
      reaper.GetPluginInfo().Register("hookcommand", nullptr), "hookcommand");
}

TEST(FakeReaperTest, AddingASurfaceWithNoTypeFailsTheTest) {
  FakeReaper reaper;
  std::unique_ptr<TestControlSurface> surface;
  EXPECT_NONFATAL_FAILURE(surface = reaper.AddSurface(),
                          "No control surface type");
  EXPECT_EQ(surface, nullptr);
}

TEST(FakeReaperTest, ASecondSurfaceFailsTheTest) {
  FakeReaper reaper;
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(reaper);
  ASSERT_NE(surface, nullptr);
  std::unique_ptr<TestControlSurface> second;
  EXPECT_NONFATAL_FAILURE(second = reaper.AddSurface(),
                          "second control surface");
  EXPECT_EQ(second, nullptr);

  // Once the first is removed, another can be added.
  surface.reset();
  surface = reaper.AddSurface();
  EXPECT_NE(surface, nullptr);
}

TEST(FakeReaperTest, SurfaceLeftOpenFailsTheTest) {
  std::unique_ptr<TestControlSurface> surface;
  EXPECT_NONFATAL_FAILURE(
      {
        FakeReaper reaper;
        surface = AddTestSurface(reaper);
      },
      "still open");

  // The fake destroyed the surface, so this only frees what is left.
  surface.reset();
}

TEST(FakeReaperTest, SurfaceRunsAdvanceTheClock) {
  FakeReaper reaper;
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(reaper);
  ASSERT_NE(surface, nullptr);
  const double start_time = reaper.GetTime();
  EXPECT_EQ(::time_precise(), start_time);

  surface->Run();
  EXPECT_EQ(g_test_surface->run_count, 1);
  EXPECT_DOUBLE_EQ(::time_precise(), start_time + 1.0 / 30);

  surface->RunFor(absl::Seconds(1));
  EXPECT_EQ(g_test_surface->run_count, 31);
  EXPECT_DOUBLE_EQ(::time_precise(), start_time + 31.0 / 30);

  surface->RunFor(absl::Milliseconds(10));
  EXPECT_EQ(g_test_surface->run_count, 32);

  reaper.AdvanceTime(absl::Milliseconds(500));
  EXPECT_DOUBLE_EQ(::time_precise(), start_time + 32.0 / 30 + 0.5);
  EXPECT_EQ(g_test_surface->run_count, 32);
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
  FakeProject& old_project = reaper.GetProject();
  FakeTrack* old_master = old_project.GetMasterTrack();

  FakeProject& project = reaper.NewProject();
  EXPECT_EQ(&reaper.GetProject(), &project);
  EXPECT_EQ(::GetMasterTrack(nullptr), ToMediaTrack(project.GetMasterTrack()));
  EXPECT_NE(project.GetMasterTrack(), old_master);
  EXPECT_FALSE(project.GetGuid(project.GetMasterTrack()) ==
               old_project.GetGuid(old_master));

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

TEST(FakeReaperTest, AddsTracksToTheEndOfTheirFolder) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bass = project.AddTrack("Bass");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  FakeTrack* inside = project.AddTrack("Inside", kick);
  FakeTrack* snare = project.AddTrack("Snare", drums);

  ASSERT_EQ(::CountTracks(nullptr), 5);
  EXPECT_EQ(::GetTrack(nullptr, 0), ToMediaTrack(drums));
  EXPECT_EQ(::GetTrack(nullptr, 1), ToMediaTrack(kick));
  EXPECT_EQ(::GetTrack(nullptr, 2), ToMediaTrack(inside));
  EXPECT_EQ(::GetTrack(nullptr, 3), ToMediaTrack(snare));
  EXPECT_EQ(::GetTrack(nullptr, 4), ToMediaTrack(bass));
  EXPECT_EQ(::GetTrack(nullptr, 5), nullptr);
  EXPECT_EQ(::GetParentTrack(ToMediaTrack(inside)), ToMediaTrack(kick));
  EXPECT_EQ(::GetParentTrack(ToMediaTrack(kick)), ToMediaTrack(drums));
  EXPECT_EQ(::GetParentTrack(ToMediaTrack(drums)), nullptr);
}

TEST(FakeReaperTest, FindsTracksByName) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);

  EXPECT_EQ(project.FindTrackByName("Kick"), kick);
  EXPECT_EQ(project.FindTrackByName("Drums"), drums);
  EXPECT_EQ(project.FindTrackByName("Snare"), nullptr);
}

TEST(FakeReaperTest, ShowingAFolderInTheMixerSetsItsTracks) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  FakeTrack* inside = project.AddTrack("Kick In", kick);
  FakeTrack* bass = project.AddTrack("Bass");
  kick->show_in_mixer = false;

  project.ShowInMixer(drums, true);
  EXPECT_TRUE(kick->show_in_mixer);

  project.ShowInMixer(drums, false);
  for (FakeTrack* track : {drums, kick, inside}) {
    EXPECT_FALSE(track->show_in_mixer) << track->name;
    EXPECT_EQ(::GetMediaTrackInfo_Value(ToMediaTrack(track), "B_SHOWINMIXER"),
              0.0)
        << track->name;
  }
  EXPECT_TRUE(bass->show_in_mixer);
}

TEST(FakeReaperTest, AddingATrackToAMissingFolderFailsTheTest) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  project.DeleteTrack(drums);

  FakeTrack* kick = nullptr;
  EXPECT_NONFATAL_FAILURE(kick = project.AddTrack("Kick", drums),
                          "isn't a track in the project");
  EXPECT_EQ(kick, nullptr);
  EXPECT_EQ(project.GetTrackCount(), 0);
}

TEST(FakeReaperTest, DeletedTrackFailsTheTest) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);

  project.DeleteTrack(drums);
  EXPECT_EQ(::CountTracks(nullptr), 1);
  EXPECT_EQ(::GetParentTrack(ToMediaTrack(kick)), nullptr);
  EXPECT_NONFATAL_FAILURE(::GetTrackColor(ToMediaTrack(drums)),
                          "is a deleted track");
}

TEST(FakeReaperTest, TrackGuidsStayPut) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  const GUID* guid = ::GetTrackGUID(ToMediaTrack(drums));

  // Enough tracks to grow the project's records many times over.
  for (int i = 0; i < 100; ++i) {
    project.AddTrack("Track");
  }
  EXPECT_EQ(::GetTrackGUID(ToMediaTrack(drums)), guid);
  EXPECT_TRUE(*guid == project.GetGuid(drums));
  EXPECT_FALSE(*guid == project.GetGuid(project.GetTrack(1)));
}

TEST(FakeReaperTest, RestoringATrackKeepsItsGuidAndValues) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  project.AddTrack("Bass");
  kick->mute = true;
  project.AddSend(kick, drums);
  const GUID guid = project.GetGuid(kick);

  project.DeleteTrack(kick);
  FakeTrack* restored = project.RestoreTrack(kick);
  ASSERT_NE(restored, nullptr);
  EXPECT_NE(restored, kick);
  EXPECT_TRUE(project.HasTrack(restored));
  EXPECT_TRUE(project.GetGuid(restored) == guid);
  EXPECT_EQ(restored->name, "Kick");
  EXPECT_TRUE(restored->mute);
  EXPECT_EQ(project.GetParentTrack(restored), drums);
  EXPECT_EQ(project.GetTrack(1), restored);
  EXPECT_THAT(project.GetSends(restored), IsEmpty());

  // Its GUID can only be in the project once.
  EXPECT_NONFATAL_FAILURE(project.RestoreTrack(kick), "already restored");
  EXPECT_NONFATAL_FAILURE(project.RestoreTrack(drums), "isn't a deleted track");
}

TEST(FakeReaperTest, RestoringATrackFindsItsRestoredFolder) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  project.AddTrack("Bass");

  // Undo restores them in the reverse order they were deleted.
  project.DeleteTrack(kick);
  project.DeleteTrack(drums);
  FakeTrack* restored_drums = project.RestoreTrack(drums);
  FakeTrack* restored_kick = project.RestoreTrack(kick);
  EXPECT_EQ(project.GetParentTrack(restored_kick), restored_drums);
  EXPECT_EQ(project.GetTrack(2), restored_kick);
}

TEST(FakeReaperTest, RestoringATrackWhoseFolderIsGoneAddsItAtTheEnd) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  FakeTrack* bass = project.AddTrack("Bass");

  project.DeleteTrack(kick);
  project.DeleteTrack(drums);
  FakeTrack* restored = project.RestoreTrack(kick);
  EXPECT_EQ(project.GetParentTrack(restored), nullptr);
  EXPECT_EQ(project.GetTrack(0), bass);
  EXPECT_EQ(project.GetTrack(1), restored);
}

TEST(FakeReaperTest, TrackSetters) {
  FakeReaper reaper;
  FakeTrack* track = reaper.GetProject().AddTrack("Drums");
  MediaTrack* track_id = ToMediaTrack(track);

  EXPECT_EQ(::SetTrackUIMute(track_id, 1, 0), 1);
  EXPECT_TRUE(track->mute);
  EXPECT_EQ(::SetTrackUIMute(track_id, -1, 0), 0);
  EXPECT_FALSE(track->mute);
  EXPECT_EQ(::SetTrackUISolo(track_id, 2, 0), 1);
  EXPECT_TRUE(track->solo);
  EXPECT_EQ(::SetTrackUIRecArm(track_id, 0, 0), 0);
  EXPECT_FALSE(track->rec_arm);
  EXPECT_EQ(::CSurf_OnVolumeChangeEx(track_id, 0.5, false, false), 0.5);
  EXPECT_EQ(track->volume, 0.5);
  EXPECT_EQ(::CSurf_OnPanChangeEx(track_id, -0.5, false, false), -0.5);
  EXPECT_EQ(track->pan, -0.5);

  std::string name = "Kit";
  EXPECT_TRUE(::GetSetMediaTrackInfo_String(track_id, "P_NAME", name.data(),
                                            /*setNewValue=*/true));
  EXPECT_EQ(track->name, "Kit");

  track->auto_mode = 3;
  track->show_in_tcp = false;
  EXPECT_EQ(::GetMediaTrackInfo_Value(track_id, "I_AUTOMODE"), 3.0);
  EXPECT_EQ(::GetMediaTrackInfo_Value(track_id, "B_SHOWINTCP"), 0.0);
  EXPECT_EQ(::GetMediaTrackInfo_Value(track_id, "B_SHOWINMIXER"), 1.0);
  EXPECT_NONFATAL_FAILURE(::GetMediaTrackInfo_Value(track_id, "D_VOL"),
                          "D_VOL");
}

TEST(FakeReaperTest, Selection) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* master = project.GetMasterTrack();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bass = project.AddTrack("Bass");
  master->selected = true;
  bass->selected = true;

  EXPECT_EQ(::CountSelectedTracks(nullptr), 1);
  EXPECT_EQ(::GetSelectedTrack(nullptr, 0), ToMediaTrack(bass));
  EXPECT_EQ(::CountSelectedTracks2(nullptr, /*wantmaster=*/true), 2);
  EXPECT_EQ(::GetSelectedTrack2(nullptr, 0, true), ToMediaTrack(master));
  EXPECT_EQ(::GetSelectedTrack2(nullptr, 1, true), ToMediaTrack(bass));
  EXPECT_EQ(::GetSelectedTrack2(nullptr, 2, true), nullptr);
  EXPECT_THAT(project.GetSelectedTracks(/*include_master=*/false),
              ElementsAre(bass));
  EXPECT_THAT(project.GetSelectedTracks(/*include_master=*/true),
              ElementsAre(master, bass));

  ::SetTrackSelected(ToMediaTrack(drums), true);
  EXPECT_TRUE(drums->selected);
  ::SetOnlyTrackSelected(ToMediaTrack(drums));
  EXPECT_TRUE(drums->selected);
  EXPECT_FALSE(bass->selected);
  EXPECT_FALSE(master->selected);
}

TEST(FakeReaperTest, Routes) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bus = project.AddTrack("Bus");
  FakeTrack* reverb = project.AddTrack("Reverb");
  MediaTrack* drums_id = ToMediaTrack(drums);
  MediaTrack* reverb_id = ToMediaTrack(reverb);
  FakeRoute* output = project.AddHardwareOutput(drums);
  FakeRoute* to_bus = project.AddSend(drums, bus);
  FakeRoute* to_reverb = project.AddSend(drums, reverb);
  FakeRoute* from_bus = project.AddSend(bus, reverb);
  to_reverb->volume = 0.5;
  to_reverb->pan = -0.25;
  from_bus->mute = true;

  // By category: receives, sends, and hardware outputs.
  EXPECT_EQ(::GetTrackNumSends(drums_id, -1), 0);
  EXPECT_EQ(::GetTrackNumSends(drums_id, 0), 2);
  EXPECT_EQ(::GetTrackNumSends(drums_id, 1), 1);
  EXPECT_EQ(::GetTrackNumSends(reverb_id, -1), 2);
  EXPECT_EQ(::GetSetTrackSendInfo(drums_id, 0, 1, "P_DESTTRACK", nullptr),
            reverb_id);
  EXPECT_EQ(::GetSetTrackSendInfo(drums_id, 0, 1, "P_SRCTRACK", nullptr),
            drums_id);
  EXPECT_EQ(::GetSetTrackSendInfo(reverb_id, -1, 1, "P_SRCTRACK", nullptr),
            ToMediaTrack(bus));
  EXPECT_EQ(::GetSetTrackSendInfo(drums_id, 0, 2, "P_DESTTRACK", nullptr),
            nullptr);
  EXPECT_NONFATAL_FAILURE(
      ::GetSetTrackSendInfo(drums_id, 0, 0, "D_VOL", nullptr), "D_VOL");

  // In the UI functions, sends come after hardware outputs, and receives are
  // -1 - index in the send functions.
  double volume = 0.0;
  double pan = 0.0;
  bool mute = false;
  EXPECT_TRUE(::GetTrackSendUIVolPan(drums_id, 2, &volume, &pan));
  EXPECT_EQ(volume, 0.5);
  EXPECT_EQ(pan, -0.25);
  EXPECT_FALSE(::GetTrackSendUIVolPan(drums_id, 3, &volume, &pan));
  EXPECT_TRUE(::GetTrackReceiveUIMute(reverb_id, 1, &mute));
  EXPECT_TRUE(mute);
  EXPECT_TRUE(::GetTrackSendUIMute(reverb_id, -2, &mute));
  EXPECT_TRUE(mute);

  EXPECT_TRUE(::SetTrackSendUIVol(drums_id, 0, 0.75, 0));
  EXPECT_EQ(output->volume, 0.75);
  EXPECT_TRUE(::SetTrackSendUIPan(drums_id, 1, 0.5, 0));
  EXPECT_EQ(to_bus->pan, 0.5);
  EXPECT_TRUE(::ToggleTrackSendUIMute(reverb_id, -2));
  EXPECT_FALSE(from_bus->mute);
  EXPECT_FALSE(::ToggleTrackSendUIMute(reverb_id, -3));
}

TEST(FakeReaperTest, EachEndOfARouteListsIt) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bus = project.AddTrack("Bus");
  FakeTrack* reverb = project.AddTrack("Reverb");
  FakeRoute* output = project.AddHardwareOutput(drums);
  FakeRoute* to_bus = project.AddSend(drums, bus);
  FakeRoute* to_reverb = project.AddSend(drums, reverb);
  FakeRoute* bus_to_reverb = project.AddSend(bus, reverb);

  EXPECT_THAT(project.GetSends(drums), ElementsAre(to_bus, to_reverb));
  EXPECT_THAT(project.GetReceives(drums), IsEmpty());
  EXPECT_THAT(project.GetHardwareOutputs(drums), ElementsAre(output));
  EXPECT_THAT(project.GetSends(bus), ElementsAre(bus_to_reverb));
  EXPECT_THAT(project.GetReceives(bus), ElementsAre(to_bus));
  EXPECT_THAT(project.GetReceives(reverb),
              ElementsAre(to_reverb, bus_to_reverb));
  EXPECT_THAT(project.GetHardwareOutputs(reverb), IsEmpty());
}

TEST(FakeReaperTest, DeletingARouteRemovesItFromEachEnd) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bus = project.AddTrack("Bus");
  FakeRoute* output = project.AddHardwareOutput(drums);
  FakeRoute* first = project.AddSend(drums, bus);
  FakeRoute* second = project.AddSend(drums, bus);

  project.DeleteRoute(first);
  EXPECT_THAT(project.GetSends(drums), ElementsAre(second));
  EXPECT_THAT(project.GetReceives(bus), ElementsAre(second));
  project.DeleteRoute(output);
  EXPECT_THAT(project.GetHardwareOutputs(drums), IsEmpty());

  EXPECT_NONFATAL_FAILURE(project.DeleteRoute(first), "isn't in the project");
}

TEST(FakeReaperTest, DeletingATrackDeletesItsRoutes) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bus = project.AddTrack("Bus");
  FakeTrack* reverb = project.AddTrack("Reverb");
  project.AddSend(drums, bus);
  FakeRoute* to_reverb = project.AddSend(drums, reverb);
  project.AddSend(bus, reverb);
  project.AddHardwareOutput(bus);

  project.DeleteTrack(bus);
  EXPECT_THAT(project.GetSends(drums), ElementsAre(to_reverb));
  EXPECT_THAT(project.GetReceives(reverb), ElementsAre(to_reverb));
  EXPECT_THAT(project.GetSends(bus), IsEmpty());
  EXPECT_THAT(project.GetReceives(bus), IsEmpty());
  EXPECT_THAT(project.GetHardwareOutputs(bus), IsEmpty());
}

TEST(FakeReaperTest, SendsREAPERCantMakeFailTheTest) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* master = project.GetMasterTrack();
  EXPECT_NONFATAL_FAILURE(project.AddSend(drums, master), "is the master");
  EXPECT_NONFATAL_FAILURE(project.AddSend(master, drums), "is the master");
  EXPECT_NONFATAL_FAILURE(project.AddSend(drums, drums), "to itself");
  EXPECT_THAT(project.GetSends(drums), IsEmpty());

  // The master may have hardware outputs.
  EXPECT_NE(project.AddHardwareOutput(master), nullptr);
}

TEST(FakeReaperTest, RecordsUndoPoints) {
  FakeReaper reaper;
  ::Undo_OnStateChangeEx("Change", 4, -1);
  ASSERT_EQ(reaper.GetProject().GetUndoPoints().size(), 1);
  EXPECT_EQ(reaper.GetProject().GetUndoPoints()[0].name, "Change");
  EXPECT_EQ(reaper.GetProject().GetUndoPoints()[0].flags, 4);
}

TEST(FakeReaperTest, UnbalancedPreventUIRefreshFailsTheTest) {
  FakeReaper reaper;
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(reaper);
  g_test_surface->on_run = [] { ::PreventUIRefresh(1); };
  EXPECT_NONFATAL_FAILURE(surface->Run(), "unbalanced by 1");

  EXPECT_NONFATAL_FAILURE(::PreventUIRefresh(-1), "wasn't started");
}

TEST(FakeReaperTest, ChangesToSeveralTracksMustBeBatched) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  MediaTrack* drums = ToMediaTrack(project.AddTrack("Drums"));
  MediaTrack* bass = ToMediaTrack(project.AddTrack("Bass"));
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(reaper);

  // One scope for several tracks, or several changes to one track, is fine.
  g_test_surface->on_run = [&] {
    ::PreventUIRefresh(1);
    ::SetTrackUIMute(drums, 1, 0);
    ::SetTrackUISolo(bass, 1, 0);
    ::PreventUIRefresh(-1);
  };
  surface->Run();
  g_test_surface->on_run = [&] {
    ::SetTrackUIMute(drums, 0, 0);
    ::SetTrackUISolo(drums, 0, 0);
  };
  surface->Run();

  // Each change to a different track outside a scope is a change of its own.
  g_test_surface->on_run = [&] {
    ::SetTrackUIMute(drums, 1, 0);
    ::SetTrackUIMute(bass, 0, 0);
  };
  EXPECT_NONFATAL_FAILURE(surface->Run(), "on 2 tracks, in 2 separate");

  // So is each scope.
  g_test_surface->on_run = [&] {
    ::PreventUIRefresh(1);
    ::SetTrackSelected(drums, true);
    ::PreventUIRefresh(-1);
    ::PreventUIRefresh(1);
    ::SetTrackSelected(bass, true);
    ::PreventUIRefresh(-1);
  };
  EXPECT_NONFATAL_FAILURE(surface->Run(), "in one TrackBatch");
}

TEST(FakeReaperTest, TheTestsOwnChangesAreCheckedAtTheEnd) {
  EXPECT_NONFATAL_FAILURE(
      {
        FakeReaper reaper;
        FakeProject& project = reaper.GetProject();
        ::SetTrackUIMute(ToMediaTrack(project.AddTrack("Drums")), 1, 0);
        ::SetTrackUIMute(ToMediaTrack(project.AddTrack("Bass")), 1, 0);
      },
      "in one TrackBatch");
}

TEST(FakeReaperTest, GroupedChangesChangeTheGroup) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick");
  FakeTrack* bass = project.AddTrack("Bass");
  drums->group = 1;
  kick->group = 1;
  std::unique_ptr<TestControlSurface> surface = AddTestSurface(reaper);

  // &1 prevents grouping.
  g_test_surface->on_run = [&] {
    ::SetTrackUIMute(ToMediaTrack(drums), 1, /*igngroupflags=*/1);
  };
  surface->Run();
  EXPECT_TRUE(drums->mute);
  EXPECT_FALSE(kick->mute);

  g_test_surface->on_run = [&] {
    ::SetTrackUISolo(ToMediaTrack(drums), 1, /*igngroupflags=*/0);
    ::CSurf_OnVolumeChangeEx(ToMediaTrack(kick), 0.5, false, true);
    ::CSurf_OnPanChangeEx(ToMediaTrack(kick), 0.5, false, false);
  };
  surface->Run();
  EXPECT_TRUE(drums->solo);
  EXPECT_TRUE(kick->solo);
  EXPECT_FALSE(bass->solo);
  EXPECT_EQ(drums->volume, 0.5);
  EXPECT_EQ(kick->volume, 0.5);
  EXPECT_EQ(bass->volume, 1.0);
  EXPECT_EQ(drums->pan, 0.0);
  EXPECT_EQ(kick->pan, 0.5);
}

TEST(FakeReaperTest, RecordsConsoleText) {
  FakeReaper reaper;
  ::ShowConsoleMsg("Hello, ");
  ::ShowConsoleMsg("world\n");
  EXPECT_EQ(reaper.GetConsoleText(), "Hello, world\n");
}

TEST(FakeReaperTest, ActionsRunTheirHandlers) {
  FakeReaper reaper;
  reaper.AddCommand({.id = 40364,
                     .text = "Options: Metronome",
                     .toggle_state = 0,
                     .on_run = [&reaper] {
                       reaper.SetToggleState(
                           40364, ::GetToggleCommandState(40364) ^ 1);
                     }});

  ::Main_OnCommand(40364, 0);
  ::Main_OnCommand(40044, 0);  // Not added, but recorded.
  EXPECT_THAT(reaper.GetCommandsRun(), ElementsAre(40364, 40044));
  EXPECT_EQ(::GetToggleCommandState(40364), 1);
  EXPECT_EQ(::GetToggleCommandState(40044), -1);
  EXPECT_STREQ(::kbd_getTextFromCmd(40364, nullptr), "Options: Metronome");
  EXPECT_STREQ(::kbd_getTextFromCmd(40044, nullptr), "");
}

TEST(FakeReaperTest, ActionsHandlersCanBeSetAfterTheyAreAdded) {
  FakeReaper reaper;
  reaper.AddCommand({.id = 40029, .text = "Edit: Undo"});
  reaper.SetCommandHandler(40029,
                           [&reaper] { reaper.GetProject().SetDirty(true); });

  ::Main_OnCommand(40029, 0);
  EXPECT_TRUE(::IsProjectDirty(nullptr));
  EXPECT_NONFATAL_FAILURE(reaper.SetCommandHandler(40030, [] {}), "40030");
}

TEST(FakeReaperTest, NamedActionsAreFoundByName) {
  FakeReaper reaper;
  reaper.AddCommand({.id = 55000, .text = "SWS: About", .name = "_SWS_ABOUT"});
  EXPECT_EQ(::NamedCommandLookup("_SWS_ABOUT"), 55000);
  EXPECT_EQ(::NamedCommandLookup("_SWS_MISSING"), 0);
}

TEST(FakeReaperTest, ActionsAreAddedOnce) {
  FakeReaper reaper;
  reaper.AddCommand({.id = 40044, .text = "Transport: Play/stop"});
  EXPECT_NONFATAL_FAILURE(
      reaper.AddCommand({.id = 40044, .text = "Transport: Play/stop"}),
      "already added");
  EXPECT_NONFATAL_FAILURE(reaper.SetToggleState(40364, 1), "wasn't added");
}

TEST(FakeReaperTest, Transport) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  project.SetPlayState(1);
  project.SetPlayPosition(2.5);
  project.SetCursorPosition(1.25);
  EXPECT_EQ(::GetPlayState(), 1);
  EXPECT_EQ(::GetPlayPosition(), 2.5);
  EXPECT_EQ(::GetCursorPosition(), 1.25);

  // Each project tab has its own.
  reaper.AddProject();
  EXPECT_EQ(::GetPlayState(), 0);
}

TEST(FakeReaperTest, ProjectState) {
  FakeReaper reaper;
  FakeProject& project = reaper.GetProject();
  EXPECT_EQ(::Undo_CanRedo2(nullptr), nullptr);
  EXPECT_EQ(::IsProjectDirty(nullptr), 0);
  EXPECT_EQ(::CountSelectedMediaItems(nullptr), 0);
  EXPECT_FALSE(::AnyTrackSolo(nullptr));

  project.SetRedo("Change volume");
  project.SetDirty(true);
  project.SetSelectedItemCount(2);
  project.AddTrack("Drums")->solo = true;
  EXPECT_STREQ(::Undo_CanRedo2(nullptr), "Change volume");
  EXPECT_EQ(::IsProjectDirty(nullptr), 1);
  EXPECT_EQ(::CountSelectedMediaItems(nullptr), 2);
  EXPECT_TRUE(::AnyTrackSolo(nullptr));
}

TEST(FakeReaperTest, AutomationOverride) {
  FakeReaper reaper;
  EXPECT_EQ(::GetGlobalAutomationOverride(), -1);
  ::SetGlobalAutomationOverride(3);
  EXPECT_EQ(reaper.GetProject().GetAutomationOverride(), 3);

  // Each project tab has its own.
  reaper.AddProject();
  EXPECT_EQ(::GetGlobalAutomationOverride(), -1);
}

TEST(FakeReaperTest, VolumeAndPanText) {
  FakeReaper reaper;
  std::array<char, 64> text = {};
  ::mkvolstr(text.data(), 1.0);
  EXPECT_STREQ(text.data(), "+0.00dB");
  ::mkvolstr(text.data(), 0.5);
  EXPECT_STREQ(text.data(), "-6.02dB");
  ::mkvolstr(text.data(), 0.1);
  EXPECT_STREQ(text.data(), "-20.0dB");
  ::mkvolstr(text.data(), 2.0);
  EXPECT_STREQ(text.data(), "+6.02dB");
  ::mkvolstr(text.data(), 0.0);
  EXPECT_STREQ(text.data(), "-inf dB");

  ::mkpanstr(text.data(), 0.0);
  EXPECT_STREQ(text.data(), "center");
  ::mkpanstr(text.data(), -0.25);
  EXPECT_STREQ(text.data(), "25%L");
  ::mkpanstr(text.data(), 1.0);
  EXPECT_STREQ(text.data(), "100%R");
}

TEST(FakeReaperTest, PositionText) {
  FakeReaper reaper;
  std::array<char, 64> text = {};
  ::format_timestr_pos(3.5, text.data(), text.size(), 2);
  EXPECT_STREQ(text.data(), "2.4.00");  // As REAPER writes it.
  ::format_timestr_pos(3725.25, text.data(), text.size(), 0);
  EXPECT_STREQ(text.data(), "1:02:05.250");
  ::format_timestr_pos(-65.5, text.data(), text.size(), 0);
  EXPECT_STREQ(text.data(), "-1:05.500");
  ::format_timestr_pos(3.5, text.data(), text.size(), 4);
  EXPECT_STREQ(text.data(), "154350");
  ::format_timestr_pos(3725.5, text.data(), text.size(), 5);
  EXPECT_STREQ(text.data(), "01:02:05:15");
  EXPECT_NONFATAL_FAILURE(
      ::format_timestr_pos(1.0, text.data(), text.size(), 3), "mode 3");
}

TEST(FakeReaperTest, GuidText) {
  FakeReaper reaper;
  GUID guid = {};
  ::stringToGuid("{00000001-0002-0003-0405-060708090A0B}", &guid);
  EXPECT_EQ(guid.Data1, 1);
  EXPECT_EQ(guid.Data2, 2);
  EXPECT_EQ(guid.Data3, 3);
  EXPECT_EQ(guid.Data4[0], 4);
  EXPECT_EQ(guid.Data4[7], 11);

  std::array<char, 64> text = {};
  ::guidToString(&guid, text.data());
  EXPECT_STREQ(text.data(), "{00000001-0002-0003-0405-060708090A0B}");

  EXPECT_NONFATAL_FAILURE(::stringToGuid("{1234}", &guid), "isn't a GUID");
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
