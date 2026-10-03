// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/surface_notifier.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

// These tests act only through REAPER's API, on a surface that records every
// call it gets, so they can run in REAPER as they are (see "Check the fakes in
// REAPER" in docs/backlog.md). Only building the project a test starts from
// uses the fake, in one function per project, which an RPP file would replace.
// The few tests that need the fake otherwise say so.

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
// A surface that records its calls
//------------------------------------------------------------------------------

// GetTrackState()'s flags.
constexpr int kTrackStateSelected = 2;
constexpr int kTrackStateMute = 8;
constexpr int kTrackStateSolo = 16;
constexpr int kTrackStateRecArm = 64;

// Every call the recording surface got, but its runs.
std::vector<std::string> g_calls;

// What the recording surface does when it runs.
std::function<void()> g_on_run = [] {};

// Returns the name `track` is recorded by: its name, or "master".
std::string Name(MediaTrack* track) {
  if (track == GetMasterTrack(nullptr)) {
    return "master";
  }
  int flags = 0;
  return GetTrackState(track, &flags);
}

const char* Bool(bool value) { return value ? "true" : "false"; }

// Records each call as text: tracks by name, and values as they read.
class RecordingSurface final : public IReaperControlSurface {
 public:
  const char* GetTypeString() override { return "RECORDING"; }
  const char* GetDescString() override { return "Recording surface"; }
  const char* GetConfigString() override { return ""; }

  void Run() override { g_on_run(); }

  void SetTrackListChange() override {
    g_calls.push_back("SetTrackListChange()");
  }
  void SetSurfaceVolume(MediaTrack* track, double volume) override {
    Record("SetSurfaceVolume", track, volume);
  }
  void SetSurfacePan(MediaTrack* track, double pan) override {
    Record("SetSurfacePan", track, pan);
  }
  void SetSurfaceMute(MediaTrack* track, bool mute) override {
    Record("SetSurfaceMute", track, Bool(mute));
  }
  void SetSurfaceSelected(MediaTrack* track, bool selected) override {
    Record("SetSurfaceSelected", track, Bool(selected));
  }
  void SetSurfaceSolo(MediaTrack* track, bool solo) override {
    Record("SetSurfaceSolo", track, Bool(solo));
  }
  void SetSurfaceRecArm(MediaTrack* track, bool rec_arm) override {
    Record("SetSurfaceRecArm", track, Bool(rec_arm));
  }
  void SetPlayState(bool play, bool pause, bool rec) override {
    g_calls.push_back(absl::StrCat("SetPlayState(", Bool(play), ", ",
                                   Bool(pause), ", ", Bool(rec), ")"));
  }
  void SetRepeatState(bool repeat) override {
    g_calls.push_back(absl::StrCat("SetRepeatState(", Bool(repeat), ")"));
  }
  void SetTrackTitle(MediaTrack* track, const char* title) override {
    Record("SetTrackTitle", track, title);
  }
  bool GetTouchState(MediaTrack* track, int is_pan) override {
    Record("GetTouchState", track, is_pan);
    return false;
  }
  void SetAutoMode(int mode) override {
    g_calls.push_back(absl::StrCat("SetAutoMode(", mode, ")"));
  }
  void ResetCachedVolPanStates() override {
    g_calls.push_back("ResetCachedVolPanStates()");
  }
  void OnTrackSelection(MediaTrack* track) override {
    g_calls.push_back(absl::StrCat("OnTrackSelection(", Name(track), ")"));
  }
  bool IsKeyDown(int key) override {
    g_calls.push_back(absl::StrCat("IsKeyDown(", key, ")"));
    return false;
  }
  int Extended(int call, void* param1, void* param2, void* param3) override {
    switch (call) {
      case CSURF_EXT_SETLASTTOUCHEDTRACK:
        g_calls.push_back(absl::StrCat("Extended(SETLASTTOUCHEDTRACK, ",
                                       Name(static_cast<MediaTrack*>(param1)),
                                       ")"));
        break;
      case CSURF_EXT_SETPAN_EX:
        g_calls.push_back(absl::StrCat("Extended(SETPAN_EX, ",
                                       Name(static_cast<MediaTrack*>(param1)),
                                       ", ", *static_cast<double*>(param2),
                                       ", ", *static_cast<int*>(param3), ")"));
        break;
      case CSURF_EXT_SETBPMANDPLAYRATE:
        g_calls.push_back(absl::StrCat("Extended(SETBPMANDPLAYRATE, ",
                                       Optional(param1), ", ", Optional(param2),
                                       ")"));
        break;
      case CSURF_EXT_SETMIXERSCROLL:
      case CSURF_EXT_SETINPUTMONITOR:
        // About state the fake doesn't hold (see SurfaceNotifier).
        break;
      default:
        g_calls.push_back(
            absl::StrFormat("Extended(0x%08x)", static_cast<unsigned>(call)));
        break;
    }
    return 0;
  }

 private:
  template <typename Value>
  static void Record(std::string_view function, MediaTrack* track,
                     const Value& value) {
    g_calls.push_back(
        absl::StrCat(function, "(", Name(track), ", ", value, ")"));
  }

  // Formats a double that may be null.
  static std::string Optional(void* value) {
    return value != nullptr ? absl::StrCat(*static_cast<double*>(value))
                            : "null";
  }
};

IReaperControlSurface* CreateRecordingSurface(const char* type_string,
                                              const char* config_string,
                                              int* err_stats) {
  return new RecordingSurface;
}

reaper_csurf_reg_t g_recording_surface_reg = {"RECORDING", "Recording surface",
                                              &CreateRecordingSurface, nullptr};

//------------------------------------------------------------------------------
// What tests expect, read through REAPER's API
//------------------------------------------------------------------------------

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
                      Bool(GetFlags(track) & flag), ")");
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
  return absl::StrCat("SetSurfaceSolo(master, ", Bool(AnyTrackSolo(nullptr)),
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

class SurfaceNotifierTest : public ::testing::Test {
 protected:
  SurfaceNotifierTest() {
    BuildProject(reaper_.GetProject());
    reaper_.GetPluginInfo().Register("csurf", &g_recording_surface_reg);
    surface_ = reaper_.AddSurface();
    a_ = GetTrack(nullptr, 0);
    b_ = GetTrack(nullptr, 1);
    g_calls.clear();
  }

  ~SurfaceNotifierTest() override {
    g_on_run = [] {};
  }

  // Returns the calls the surface got since the last time, and forgets them.
  std::vector<std::string> TakeCalls() { return std::exchange(g_calls, {}); }

  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;
  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(SurfaceNotifierTest, MuteSendsTheMastersSoloThenTheTracks) {
  SetTrackUIMute(a_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, false)",
                                       "SetSurfaceMute(A, true)",
                                       "SetSurfaceSolo(A, false)"));
}

TEST_F(SurfaceNotifierTest, SoloSendsAsMuteDoes) {
  SetTrackUISolo(b_, 1, 0);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSolo(master, true)",
                                       "SetSurfaceMute(B, false)",
                                       "SetSurfaceSolo(B, true)"));
}

TEST_F(SurfaceNotifierTest, MuteAndSoloInABatchSendTheTracksAtItsEnd) {
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

TEST_F(SurfaceNotifierTest, RecArmChangesTheTrackListAndSendsEveryTrack) {
  SetTrackUIRecArm(a_, 1, 0);
  std::vector<std::string> expected = {"SetTrackListChange()", MasterSolo(),
                                       "SetTrackListChange()", MasterSolo()};
  Append(expected, EveryState());
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
  EXPECT_THAT(expected, Contains("SetSurfaceRecArm(A, true)"));
}

TEST_F(SurfaceNotifierTest, RecArmInABatchSendsEveryTrackOnceAtItsEnd) {
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

TEST_F(SurfaceNotifierTest, SelectingOnlyOneTrackSendsEachTrackThatChanged) {
  SetTrackSelected(b_, true);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(B, true)"));

  // Each selection is its own run, so the fake checks each on its own.
  surface_->Run();
  SetOnlyTrackSelected(a_);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(A, true)",
                                       "SetSurfaceSelected(B, false)"));

  surface_->Run();
  SetOnlyTrackSelected(a_);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierTest, SelectionInABatchIsSentAtItsEnd) {
  PreventUIRefresh(1);
  SetTrackSelected(b_, true);
  SetTrackSelected(a_, true);
  EXPECT_THAT(TakeCalls(), IsEmpty());

  PreventUIRefresh(-1);
  EXPECT_THAT(TakeCalls(), ElementsAre("SetSurfaceSelected(A, true)",
                                       "SetSurfaceSelected(B, true)"));
}

TEST_F(SurfaceNotifierTest, VolumeAndPanChangesTouchTheTrack) {
  CSurf_OnVolumeChangeEx(a_, 0.5, false, true);
  EXPECT_THAT(TakeCalls(),
              ElementsAre("IsKeyDown(16)", "Extended(SETLASTTOUCHEDTRACK, A)"));

  CSurf_OnPanChangeEx(b_, -0.25, false, true);
  EXPECT_THAT(TakeCalls(),
              ElementsAre("IsKeyDown(16)", "Extended(SETLASTTOUCHEDTRACK, B)"));
}

TEST_F(SurfaceNotifierTest, AutomationOverrideSendsVolumePanAndSelection) {
  SetGlobalAutomationOverride(3);
  EXPECT_THAT(TakeCalls(), ElementsAreArray(AutomationChange()));
}

TEST_F(SurfaceNotifierTest, AutomationModeActionsSendTheModeFirst) {
  for (int mode = 0; mode <= 4; ++mode) {
    Main_OnCommand(40400 + mode, 0);
    std::vector<std::string> expected = {
        absl::StrCat("SetAutoMode(", mode, ")")};
    Append(expected, AutomationChange());
    EXPECT_THAT(TakeCalls(), ElementsAreArray(expected)) << "Mode " << mode;
  }
}

TEST_F(SurfaceNotifierTest, UndoAndRedoSendEverything) {
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

TEST_F(SurfaceNotifierTest, OtherActionsSendNothing) {
  // The ruler's time unit actions.
  for (int command : {40365, 40369, 40370}) {
    Main_OnCommand(command, 0);
  }
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierTest, RouteChangesSendNothing) {
  // A project with a route, opened in place of the first.
  surface_.reset();
  BuildProjectWithSend(reaper_.NewProject());
  surface_ = reaper_.AddSurface();
  MediaTrack* a = GetTrack(nullptr, 0);
  TakeCalls();

  SetTrackSendUIVol(a, 0, 0.5, 0);
  SetTrackSendUIPan(a, 0, -0.25, 0);
  ToggleTrackSendUIMute(a, 0);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

TEST_F(SurfaceNotifierTest, CallsDuringARunArePartOfIt) {
  // A run that changes two tracks in one batch, which the fake would fail as
  // separate changes, or as an unbalanced batch, if the calls the notifier
  // makes during it ended the run.
  g_on_run = [this] {
    PreventUIRefresh(1);
    SetTrackUIMute(a_, 1, 0);
    SetTrackUIMute(b_, 1, 0);
    PreventUIRefresh(-1);
  };
  surface_->Run();
  EXPECT_THAT(
      TakeCalls(),
      ElementsAre("SetSurfaceSolo(master, false)",
                  "SetSurfaceSolo(master, false)", "SetSurfaceMute(A, true)",
                  "SetSurfaceSolo(A, false)", "SetSurfaceMute(B, true)",
                  "SetSurfaceSolo(B, false)"));
}

TEST_F(SurfaceNotifierTest, SendsNothingWithNoSurfaceOpen) {
  surface_.reset();
  SetTrackUIMute(a_, 1, 0);
  SetTrackUIRecArm(a_, 1, 0);
  Main_OnCommand(40029, 0);
  EXPECT_THAT(TakeCalls(), IsEmpty());
}

// Only in the fake: REAPER's own actions can't be given a handler.
TEST_F(SurfaceNotifierTest, ActionsSendAfterTheirHandler) {
  reaper_.AddCommand(
      {.id = 40403, .on_run = [] { g_calls.push_back("Handler"); }});
  Main_OnCommand(40403, 0);
  std::vector<std::string> expected = {"Handler", "SetAutoMode(3)"};
  Append(expected, AutomationChange());
  EXPECT_THAT(TakeCalls(), ElementsAreArray(expected));
}

// Only in the fake: REAPER's repeat state is the user's.
TEST_F(SurfaceNotifierTest, UndoSendsTheRepeatState) {
  reaper_.AddCommand({.id = 1068, .toggle_state = 1});
  Main_OnCommand(40029, 0);
  EXPECT_THAT(TakeCalls(), Contains("SetRepeatState(true)"));
}

}  // namespace
}  // namespace jpr
