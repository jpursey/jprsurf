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
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/recording_surface.h"
#include "jpr/common/track_state.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

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

// Tracks A and B, with a hardware output from A, then a send from A to B.
void BuildProjectWithRoutes(FakeProject& project) {
  FakeTrack* a = project.AddTrack("A");
  FakeTrack* b = project.AddTrack("B");
  project.AddHardwareOutput(a);
  project.AddSend(a, b);
}

//------------------------------------------------------------------------------
// What tests expect, read through REAPER's API
//------------------------------------------------------------------------------

std::string Name(MediaTrack* track) {
  return RecordingSurface::GetTrackText(track);
}

void Append(std::vector<std::string>& calls,
            const std::vector<std::string>& more) {
  calls.insert(calls.end(), more.begin(), more.end());
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
  return absl::StrCat(
      function, "(", Name(track), ", ",
      RecordingSurface::GetBoolText(GetTrackStateFlags(track) & flag), ")");
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

// A track's mute, and its solo unless it is the master.
std::vector<std::string> MuteAndSolo(MediaTrack* track) {
  if (track == GetMasterTrack(nullptr)) {
    return {Mute(track)};
  }
  return {Mute(track), Solo(track)};
}

// The end of a track's state: its title, rec arm, and selection.
std::vector<std::string> TitleRecArmAndSelected(MediaTrack* track) {
  int flags = 0;
  return {absl::StrCat("SetTrackTitle(", Name(track), ", ",
                       GetTrackState(track, &flags), ")"),
          RecArm(track), Selected(track)};
}

// A track's whole state, as after a track list change, leaving out its mute
// and solo if `unsent`, as they aren't sent yet.
std::vector<std::string> State(MediaTrack* track, bool unsent = false) {
  std::vector<std::string> calls = VolumeAndPan(track);
  if (!unsent) {
    Append(calls, MuteAndSolo(track));
  }
  Append(calls, TitleRecArmAndSelected(track));
  return calls;
}

// Every track's whole state, leaving out the mute and solo of `unsent`.
std::vector<std::string> EveryState(MediaTrack* unsent = nullptr) {
  std::vector<std::string> calls;
  for (MediaTrack* track : GetTracks()) {
    Append(calls, State(track, track == unsent));
  }
  return calls;
}

// A change to a track's rec arm.
std::vector<std::string> RecArmChange(MediaTrack* track) {
  std::vector<std::string> calls = {RecArm(track)};
  Append(calls, MuteAndSolo(track));
  Append(calls, VolumeAndPan(track));
  return calls;
}

// What a rec arm of `track` outside a batch sends: its change, the track list
// change with every track's state, leaving out the mute and solo of `unsent`,
// the master's solo, and then the change of each of `changed`.
std::vector<std::string> RecArmOutsideABatch(
    MediaTrack* track, const std::vector<MediaTrack*>& changed,
    MediaTrack* unsent = nullptr) {
  std::vector<std::string> calls = RecArmChange(track);
  calls.push_back("SetTrackListChange()");
  Append(calls, EveryState(unsent));
  calls.push_back(MasterSolo());
  for (MediaTrack* changed_track : changed) {
    Append(calls, RecArmChange(changed_track));
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
    master_ = GetMasterTrack(nullptr);
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
  }

  MediaTrack* master_ = nullptr;
  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(SurfaceNotifierContractTest, MuteSendsOnlyTheMastersSolo) {
  SetTrackUIMute(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)"));
}

TEST_F(SurfaceNotifierContractTest, SoloSendsOnlyTheMastersSolo) {
  SetTrackUISolo(b_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, true)"));
}

TEST_F(SurfaceNotifierContractTest, MuteThatChangesNothingSendsTheMastersSolo) {
  SetTrackUIMute(a_, 0, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)"));
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

TEST_F(SurfaceNotifierContractTest, MuteOutsideABatchIsSentAtABatchsEnd) {
  SetTrackUIMute(a_, 1, 0);
  TakeCalls();
  EndEntryPoint();

  PreventUIRefresh(1);
  SetTrackUISolo(b_, 1, 0);
  PreventUIRefresh(-1);
  EXPECT_THAT(
      TakeCalls(),
      ElementsAre("SetSurfaceSolo(master, true)", "SetSurfaceMute(A, true)",
                  "SetSurfaceSolo(A, false)", "SetSurfaceMute(B, false)",
                  "SetSurfaceSolo(B, true)"));
}

TEST_F(SurfaceNotifierContractTest, MuteChangedBackIsNeverSent) {
  SetTrackUIMute(a_, 1, 0);
  TakeCalls();

  PreventUIRefresh(1);
  SetTrackUIMute(a_, 0, 0);
  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)"));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmSendsTheTrackThenTheTrackListChangeThenTheTrackAgain) {
  SetTrackUIRecArm(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(RecArmOutsideABatch(a_, {a_})));
}

TEST_F(SurfaceNotifierContractTest, EveryStateLeavesOutAMuteNotSentYet) {
  SetTrackUIMute(b_, 1, 0);
  TakeCalls();
  EndEntryPoint();

  SetTrackUIRecArm(a_, 1, 0);
  EXPECT_THAT(TakeCalls(),
              ElementsAreArray(RecArmOutsideABatch(a_, {a_}, /*unsent=*/b_)));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmOfASelectedTrackIsSentAsAChangeToEveryTrack) {
  SetTrackSelected(a_, true);
  TakeCalls();
  EndEntryPoint();

  SetTrackUIRecArm(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(RecArmOutsideABatch(a_, {a_, b_})));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmOfASelectedTrackWithoutGangingIsSentAsAChangeToIt) {
  SetTrackSelected(a_, true);
  TakeCalls();
  EndEntryPoint();

  SetTrackUIRecArm(a_, 1, kPreventSelectionGanging);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(RecArmOutsideABatch(a_, {a_})));
}

TEST_F(SurfaceNotifierContractTest, RecArmThatChangesNothingSendsNothing) {
  SetTrackUIRecArm(a_, 0, 0);
  EXPECT_THAT(TakeCalls(), IsEmpty());

  PreventUIRefresh(1);
  SetTrackUIRecArm(a_, 0, 0);
  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierContractTest,
       RecArmInABatchSendsTheTrackListChangeAtItsEnd) {
  PreventUIRefresh(1);
  SetTrackUIRecArm(a_, 1, 0);
  SetTrackUIRecArm(b_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetTrackListChange()", MasterSolo(),
                                       "SetTrackListChange()", MasterSolo()));

  // Each track whose rec arm changed is sent as it is outside a batch.
  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, State(master_));
  for (MediaTrack* track : {a_, b_}) {
    Append(expected, TitleRecArmAndSelected(track));
    Append(expected, RecArmChange(track));
  }
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

TEST_F(SurfaceNotifierContractTest, RecArmChangedBackInABatchIsStillSent) {
  PreventUIRefresh(1);
  SetTrackUIRecArm(a_, 1, 0);
  SetTrackUIRecArm(a_, 0, 0);
  TakeCalls();

  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, State(master_));
  Append(expected, TitleRecArmAndSelected(a_));
  Append(expected, RecArmChange(a_));
  Append(expected, State(b_));
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmInABatchSendsAnotherTracksMuteAfterItsState) {
  PreventUIRefresh(1);
  SetTrackUIMute(a_, 1, 0);
  SetTrackUIRecArm(b_, 1, 0);
  TakeCalls();

  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, State(master_));
  Append(expected, State(a_, /*unsent=*/true));
  Append(expected, MuteAndSolo(a_));
  Append(expected, TitleRecArmAndSelected(b_));
  Append(expected, RecArmChange(b_));
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmOfASelectedTrackInABatchIsSentAsAChangeToEveryTrack) {
  PreventUIRefresh(1);
  SetTrackSelected(b_, true);
  SetTrackUIRecArm(b_, 1, 0);
  TakeCalls();

  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, State(master_));
  for (MediaTrack* track : {a_, b_}) {
    Append(expected, TitleRecArmAndSelected(track));
    Append(expected, RecArmChange(track));
  }
  expected.push_back(Selected(b_));
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

TEST_F(SurfaceNotifierContractTest,
       RecArmInABatchSendsAnotherTracksSelectionAgain) {
  PreventUIRefresh(1);
  SetTrackSelected(a_, true);
  SetTrackUIRecArm(b_, 1, 0);
  TakeCalls();

  PreventUIRefresh(-1);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo()};
  Append(expected, State(master_));
  Append(expected, State(a_));
  expected.push_back(Selected(a_));
  Append(expected, TitleRecArmAndSelected(b_));
  Append(expected, RecArmChange(b_));
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

  // Even when it changes nothing.
  SetGlobalAutomationOverride(3);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(AutomationChange()));
}

TEST_F(SurfaceNotifierContractTest,
       AutomationModeActionsSendTheModeThenWhatChanged) {
  SetTrackSelected(a_, true);
  TakeCalls();
  for (AutoMode mode : {AutoMode::kRead, AutoMode::kTouch, AutoMode::kWrite,
                        AutoMode::kLatch, AutoMode::kTrimRead}) {
    Main_OnCommand(kFirstAutoModeAction + static_cast<int>(mode), 0);
    std::vector<std::string> expected = {
        absl::StrCat("SetAutoMode(", static_cast<int>(mode), ")")};
    Append(expected, AutomationChange());
    EXPECT_THAT(TakeCalls(), ElementsAreArray(expected))
        << "Mode " << static_cast<int>(mode);
  }

  // A mode the selected tracks already have changes nothing.
  Main_OnCommand(kFirstAutoModeAction, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetAutoMode(0)"));
}

TEST_F(SurfaceNotifierContractTest,
       AutomationModeActionsWithNoTrackSelectedOnlySendTheMode) {
  for (int mode = 0; mode <= static_cast<int>(AutoMode::kLatch); ++mode) {
    Main_OnCommand(kFirstAutoModeAction + mode, 0);
    EXPECT_THAT(TakeCalls(),
                ElementsAre(absl::StrCat("SetAutoMode(", mode, ")")))
        << "Mode " << mode;
  }
}

TEST_F(SurfaceNotifierContractTest, UndoAndRedoSendEverything) {
  // Values that aren't the defaults, so they show which track each is.
  CSurf_OnVolumeChangeEx(a_, 0.5, false, true);
  CSurf_OnPanChangeEx(b_, -0.25, false, true);
  SetTrackUIMute(b_, 1, 0);

  for (int command : {kUndoAction, kRedoAction}) {
    TakeCalls();
    Main_OnCommand(command, 0);

    // B's mute isn't sent yet at the undo, so every track's state leaves it
    // out, and the undo then sends it.
    std::vector<std::string> expected = {Mute(master_)};
    Append(expected, VolumeAndPan(master_));
    Append(expected,
           {"SetRepeatState(false)", "Extended(SETBPMANDPLAYRATE, 120, null)",
            "SetTrackListChange()"});
    Append(expected, EveryState(command == kUndoAction ? b_ : nullptr));
    Append(expected, {MasterSolo(), Mute(master_)});
    Append(expected, VolumeAndPan(master_));
    for (MediaTrack* track : {a_, b_}) {
      Append(expected, RecArmChange(track));
    }
    EXPECT_THAT(TakeCalls(), ElementsAreArray(expected))
        << "Action " << command;
  }
}

TEST_F(SurfaceNotifierContractTest, UndoAndRedoWithNothingToDoSendNothing) {
  for (int command : {kUndoAction, kRedoAction}) {
    Main_OnCommand(command, 0);
    EXPECT_THAT(TakeCalls(), IsEmpty()) << "Action " << command;
  }
}

TEST_F(SurfaceNotifierContractTest, OtherActionsSendNothing) {
  // The ruler's time unit actions.
  for (int command : {40365, 40369, 40370}) {
    Main_OnCommand(command, 0);
  }
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

class SurfaceNotifierRouteContractTest : public ContractTest {
 protected:
  // The *TrackSendUI* functions index A's hardware output, then its send.
  static constexpr int kOutputIndex = 0;
  static constexpr int kSendIndex = 1;
  static constexpr int kReceiveIndex = -1;

  SurfaceNotifierRouteContractTest() {
    OpenProject(BuildProjectWithRoutes);
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
  }

  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(SurfaceNotifierRouteContractTest, SendChangesSendBothEnds) {
  SetTrackSendUIVol(a_, kSendIndex, 0.5, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDVOLUME, A, 1, 0.5)",
                                       "Extended(SETRECVVOLUME, B, 0, 0.5)"));

  SetTrackSendUIPan(a_, kSendIndex, -0.25, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDPAN, A, 1, -0.25)",
                                       "Extended(SETRECVPAN, B, 0, -0.25)"));

  // Even when they change nothing.
  SetTrackSendUIVol(a_, kSendIndex, 0.5, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDVOLUME, A, 1, 0.5)",
                                       "Extended(SETRECVVOLUME, B, 0, 0.5)"));
}

TEST_F(SurfaceNotifierRouteContractTest, ReceiveChangesSendAsSendChangesDo) {
  SetTrackSendUIVol(b_, kReceiveIndex, 0.75, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDVOLUME, A, 1, 0.75)",
                                       "Extended(SETRECVVOLUME, B, 0, 0.75)"));

  SetTrackSendUIPan(b_, kReceiveIndex, 0.5, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDPAN, A, 1, 0.5)",
                                       "Extended(SETRECVPAN, B, 0, 0.5)"));
}

TEST_F(SurfaceNotifierRouteContractTest, HardwareOutputChangesSendTheSource) {
  SetTrackSendUIVol(a_, kOutputIndex, 0.5, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDVOLUME, A, 0, 0.5)"));

  SetTrackSendUIPan(a_, kOutputIndex, 0.25, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDPAN, A, 0, 0.25)"));
}

TEST_F(SurfaceNotifierRouteContractTest, AnInstantChangeSendsAsAnyChangeDoes) {
  SetTrackSendUIVol(a_, kSendIndex, 0.5, -1);
  EXPECT_THAT(TakeCalls(), ElementsAre("Extended(SETSENDVOLUME, A, 1, 0.5)",
                                       "Extended(SETRECVVOLUME, B, 0, 0.5)"));
}

TEST_F(SurfaceNotifierRouteContractTest, EndingAnEditSendsNothing) {
  SetTrackSendUIVol(a_, kSendIndex, 0.5, 1);
  SetTrackSendUIPan(a_, kSendIndex, 0.5, 1);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierRouteContractTest, MuteChangesSendNothing) {
  ToggleTrackSendUIMute(a_, kOutputIndex);
  ToggleTrackSendUIMute(a_, kSendIndex);
  ToggleTrackSendUIMute(b_, kReceiveIndex);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

class SurfaceNotifierGroupContractTest : public ContractTest {
 protected:
  // A and B are in a group, and C isn't.
  SurfaceNotifierGroupContractTest() {
    OpenProject([](FakeProject& project) {
      project.AddTrack("A")->group = 1;
      project.AddTrack("B")->group = 1;
      project.AddTrack("C");
    });
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
    c_ = GetTrack(nullptr, 2);
  }

  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
  MediaTrack* c_ = nullptr;
};

TEST_F(SurfaceNotifierGroupContractTest,
       RecArmOfAGroupedTrackIsSentAsAChangeToEveryTrack) {
  SetTrackUIRecArm(a_, 1, kPreventSelectionGanging);
  EXPECT_THAT(TakeCalls(),
              ElementsAreArray(RecArmOutsideABatch(a_, {a_, b_, c_})));
}

TEST_F(SurfaceNotifierGroupContractTest,
       RecArmOfAGroupedTrackWithoutGroupingIsSentAsAChangeToIt) {
  SetTrackUIRecArm(a_, 1, kPreventGroupingAndGanging);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(RecArmOutsideABatch(a_, {a_})));
}

}  // namespace
}  // namespace jpr
