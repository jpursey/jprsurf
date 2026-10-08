// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// What REAPER calls on a surface from inside its functions, which
// SurfaceNotifier makes under the fake. The tests that need the fake are in
// surface_notifier_test.cc.

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/testing/recording_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

//------------------------------------------------------------------------------
// The projects tests start from
//------------------------------------------------------------------------------

// Tracks A and B, at REAPER's defaults.
void BuildProject(FakeProject& project) {
  project.AddTrack("A");
  project.AddTrack("B");
}

// Tracks A and B, with a send from A to B.
void BuildProjectWithSend(FakeProject& project) {
  FakeTrack* a = project.AddTrack("A");
  FakeTrack* b = project.AddTrack("B");
  project.AddSend(a, b);
}

//------------------------------------------------------------------------------
// What tests expect, read through REAPER's API
//------------------------------------------------------------------------------

// GetTrackState()'s flags.
constexpr int kTrackStateSelected = 2;
constexpr int kTrackStateMute = 8;
constexpr int kTrackStateSolo = 16;
constexpr int kTrackStateRecArm = 64;

std::string Name(MediaTrack* track) {
  return RecordingSurface::GetTrackText(track);
}

// Returns a track's state, as GetTrackState()'s flags.
int GetFlags(MediaTrack* track) {
  int flags = 0;
  GetTrackState(track, &flags);
  return flags;
}

// The calls that send a track's volume and pan.
std::vector<std::string> VolumeAndPan(MediaTrack* track) {
  double volume = 0.0;
  double pan = 0.0;
  GetTrackUIVolPan(track, &volume, &pan);
  const std::string name = Name(track);
  return {
      absl::StrCat("SetSurfaceVolume(", name, ", ", volume, ")"),
      absl::StrCat("SetSurfacePan(", name, ", ", pan, ")"),
      absl::StrCat("Extended(SETPAN_EX, ", name, ", ", pan, ", 3)"),
  };
}

// The call that sends one of a track's GetTrackState() flags.
std::string Flag(std::string_view function, MediaTrack* track, int flag) {
  return absl::StrCat(function, "(", Name(track), ", ",
                      RecordingSurface::GetBoolText(GetFlags(track) & flag),
                      ")");
}

std::string Mute(MediaTrack* track) {
  return Flag("SetSurfaceMute", track, kTrackStateMute);
}
std::string Solo(MediaTrack* track) {
  return Flag("SetSurfaceSolo", track, kTrackStateSolo);
}
std::string RecArm(MediaTrack* track) {
  return Flag("SetSurfaceRecArm", track, kTrackStateRecArm);
}
std::string Selected(MediaTrack* track) {
  return Flag("SetSurfaceSelected", track, kTrackStateSelected);
}

std::string MasterSolo() {
  return absl::StrCat("SetSurfaceSolo(master, ",
                      RecordingSurface::GetBoolText(AnyTrackSolo(nullptr)),
                      ")");
}

// Returns the master track, then every track in order.
std::vector<MediaTrack*> GetTracks() {
  std::vector<MediaTrack*> tracks = {GetMasterTrack(nullptr)};
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    tracks.push_back(GetTrack(nullptr, i));
  }
  return tracks;
}

void Append(std::vector<std::string>& calls,
            const std::vector<std::string>& more) {
  calls.insert(calls.end(), more.begin(), more.end());
}

// Every track's whole state, as after a track list change.
std::vector<std::string> EveryState() {
  std::vector<std::string> calls;
  for (MediaTrack* track : GetTracks()) {
    Append(calls, VolumeAndPan(track));
    calls.push_back(Mute(track));
    if (track != GetMasterTrack(nullptr)) {
      calls.push_back(Solo(track));
    }
    int flags = 0;
    calls.push_back(absl::StrCat("SetTrackTitle(", Name(track), ", ",
                                 GetTrackState(track, &flags), ")"));
    calls.push_back(RecArm(track));
    calls.push_back(Selected(track));
  }
  return calls;
}

// Every track's volume, pan, and selection, as after an automation change.
std::vector<std::string> AutomationChange() {
  std::vector<std::string> calls;
  for (MediaTrack* track : GetTracks()) {
    Append(calls, VolumeAndPan(track));
    calls.push_back(Selected(track));
  }
  return calls;
}

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

class SurfaceNotifierContractTest : public ContractTest {
 protected:
  SurfaceNotifierContractTest() {
    OpenProject(BuildProject);
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
  }

  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(SurfaceNotifierContractTest, MuteSendsTheMastersSoloThenTheTracks) {
  SetTrackUIMute(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)",
                                       "SetSurfaceMute(A, true)",
                                       "SetSurfaceSolo(A, false)"));
}

TEST_F(SurfaceNotifierContractTest, SoloSendsAsMuteDoes) {
  SetTrackUISolo(b_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, true)",
                                       "SetSurfaceMute(B, false)",
                                       "SetSurfaceSolo(B, true)"));
}

TEST_F(SurfaceNotifierContractTest, MuteAndSoloInABatchSendTheTracksAtItsEnd) {
  PreventUIRefresh(1);
  PreventUIRefresh(1);
  SetTrackUIMute(b_, 1, 0);
  SetTrackUISolo(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)",
                                       "SetSurfaceSolo(master, true)"));

  // Only the outermost scope's end sends them, in track order.
  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), IsEmpty());
  PreventUIRefresh(-1);
  EXPECT_THAT(
      TakeCalls(),
      ElementsAre("SetSurfaceMute(A, false)", "SetSurfaceSolo(A, true)",
                  "SetSurfaceMute(B, true)", "SetSurfaceSolo(B, false)"));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmChangesTheTrackListAndSendsEveryTrack) {
  SetTrackUIRecArm(a_, 1, 0);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo(),
                                       "SetTrackListChange()", MasterSolo()};
  Append(expected, EveryState());
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
  EXPECT_THAT(expected, Contains("SetSurfaceRecArm(A, true)"));
}

TEST_F(SurfaceNotifierContractTest, RecArmInABatchSendsEveryTrackOnceAtItsEnd) {
  PreventUIRefresh(1);
  SetTrackUIRecArm(a_, 1, 0);
  SetTrackUIRecArm(b_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetTrackListChange()", MasterSolo(),
                                       "SetTrackListChange()", MasterSolo()));

  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, EveryState());
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

TEST_F(SurfaceNotifierContractTest,
       SelectingOnlyOneTrackSendsEachTrackThatChanged) {
  SetTrackSelected(b_, true);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(B, true)"));

  // Each selection is checked on its own under the fake.
  EndEntryPoint();
  SetOnlyTrackSelected(a_);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(A, true)",
                                       "SetSurfaceSelected(B, false)"));

  EndEntryPoint();
  SetOnlyTrackSelected(a_);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierContractTest, SelectionInABatchIsSentAtItsEnd) {
  PreventUIRefresh(1);
  SetTrackSelected(b_, true);
  SetTrackSelected(a_, true);
  EXPECT_THAT(TakeCalls(), IsEmpty());

  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(A, true)",
                                       "SetSurfaceSelected(B, true)"));
}

TEST_F(SurfaceNotifierContractTest, VolumeAndPanChangesTouchTheTrack) {
  CSurf_OnVolumeChangeEx(a_, 0.5, false, true);
  EXPECT_THAT(TakeCalls(),
              ElementsAre("IsKeyDown(16)", "Extended(SETLASTTOUCHEDTRACK, A)"));

  CSurf_OnPanChangeEx(b_, -0.25, false, true);
  EXPECT_THAT(TakeCalls(),
              ElementsAre("IsKeyDown(16)", "Extended(SETLASTTOUCHEDTRACK, B)"));
}

TEST_F(SurfaceNotifierContractTest,
       AutomationOverrideSendsVolumePanAndSelection) {
  SetGlobalAutomationOverride(3);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(AutomationChange()));
}

TEST_F(SurfaceNotifierContractTest, AutomationModeActionsSendTheModeFirst) {
  for (int mode = 0; mode <= static_cast<int>(AutoMode::kLatch); ++mode) {
    Main_OnCommand(kFirstAutoModeAction + mode, 0);
    std::vector<std::string> expected = {
        absl::StrCat("SetAutoMode(", mode, ")")};
    Append(expected, AutomationChange());
    EXPECT_THAT(TakeCalls(), ElementsAreArray(expected)) << "Mode " << mode;
  }
}

TEST_F(SurfaceNotifierContractTest, UndoAndRedoSendEverything) {
  // Values that aren't the defaults, so they show which track each is.
  CSurf_OnVolumeChangeEx(a_, 0.5, false, true);
  CSurf_OnPanChangeEx(b_, -0.25, false, true);
  SetTrackUIMute(b_, 1, 0);

  MediaTrack* master = GetMasterTrack(nullptr);
  for (int command : {40029, 40030}) {
    TakeCalls();
    Main_OnCommand(command, 0);

    std::vector<std::string> expected = {Mute(master)};
    Append(expected, VolumeAndPan(master));
    Append(expected,
           {"SetRepeatState(false)", "Extended(SETBPMANDPLAYRATE, 120, null)",
            "SetTrackListChange()"});
    Append(expected, EveryState());
    Append(expected, {MasterSolo(), Mute(master)});
    Append(expected, VolumeAndPan(master));
    for (MediaTrack* track : {a_, b_}) {
      Append(expected, {RecArm(track), Mute(track), Solo(track)});
      Append(expected, VolumeAndPan(track));
    }
    EXPECT_THAT(TakeCalls(), ElementsAreArray(expected))
        << "Action " << command;
  }
}

TEST_F(SurfaceNotifierContractTest, OtherActionsSendNothing) {
  // The ruler's time unit actions.
  for (int command : {40365, 40369, 40370}) {
    Main_OnCommand(command, 0);
  }
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierContractTest, RouteChangesSendNothing) {
  OpenProject(BuildProjectWithSend);
  MediaTrack* a = GetTrack(nullptr, 0);

  SetTrackSendUIVol(a, 0, 0.5, 0);
  SetTrackSendUIPan(a, 0, -0.25, 0);
  ToggleTrackSendUIMute(a, 0);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

}  // namespace
}  // namespace jpr
