// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// What the functions on the API list do to REAPER's state, which the fake
// REAPER models. The tests that need the fake are in fake_reaper_test.cc.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/guid.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/timeline.h"
#include "jpr/common/track_state.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// GetTrackState()'s flags for a track soloed in place.
constexpr int kSoloedInPlace = kTrackStateSolo | kTrackStateSoloInPlace;

// Returns a track's name, as GetTrackState() returns it.
std::string GetName(MediaTrack* track) {
  int flags = 0;
  const char* name = GetTrackState(track, &flags);
  return name != nullptr ? name : "(null)";
}

// Returns the first track called `name`, or null if there is none.
MediaTrack* FindTrack(std::string_view name) {
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    if (GetName(GetTrack(nullptr, i)) == name) {
      return GetTrack(nullptr, i);
    }
  }
  ADD_FAILURE() << "No track is called " << name;
  return nullptr;
}

// Returns true if `track`'s GetTrackState() flags have `flag`.
bool HasFlag(MediaTrack* track, int flag) {
  return (GetTrackStateFlags(track) & flag) != 0;
}

// Returns `track`'s volume or pan, as GetTrackUIVolPan() reads them.
double GetVolume(MediaTrack* track) {
  double volume = 0.0;
  double pan = 0.0;
  GetTrackUIVolPan(track, &volume, &pan);
  return volume;
}
double GetPan(MediaTrack* track) {
  double volume = 0.0;
  double pan = 0.0;
  GetTrackUIVolPan(track, &volume, &pan);
  return pan;
}

// Returns the text guidToString() writes for `guid`.
std::string GetGuidText(const GUID* guid) {
  std::array<char, 64> text = {};
  guidToString(guid, text.data());
  return text.data();
}

//------------------------------------------------------------------------------
// Tracks
//------------------------------------------------------------------------------

using TrackContractTest = ContractTest;

// Drums, with Kick (with Kick In) and Snare in it, then Bass.
void BuildFolders(FakeProject& project) {
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* kick = project.AddTrack("Kick", drums);
  project.AddTrack("Kick In", kick);
  project.AddTrack("Snare", drums);
  project.AddTrack("Bass");
}

TEST_F(TrackContractTest, TracksAreInOrderWithTheirFolders) {
  OpenProject(BuildFolders);
  ASSERT_EQ(CountTracks(nullptr), 5);
  std::vector<std::string> names;
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    names.push_back(GetName(GetTrack(nullptr, i)));
  }
  EXPECT_THAT(names, ElementsAre("Drums", "Kick", "Kick In", "Snare", "Bass"));
  EXPECT_EQ(GetTrack(nullptr, 5), nullptr);
  EXPECT_EQ(GetTrack(nullptr, -1), nullptr);

  EXPECT_EQ(GetParentTrack(FindTrack("Kick In")), FindTrack("Kick"));
  EXPECT_EQ(GetParentTrack(FindTrack("Kick")), FindTrack("Drums"));
  EXPECT_EQ(GetParentTrack(FindTrack("Snare")), FindTrack("Drums"));
  EXPECT_EQ(GetParentTrack(FindTrack("Drums")), nullptr);
  EXPECT_EQ(GetParentTrack(FindTrack("Bass")), nullptr);
  EXPECT_EQ(GetParentTrack(GetMasterTrack(nullptr)), nullptr);
}

TEST_F(TrackContractTest, TracksAreNumberedFromOneAndTheMasterIsMinusOne) {
  OpenProject(BuildFolders);
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    EXPECT_EQ(GetMediaTrackInfo_Value(GetTrack(nullptr, i), "IP_TRACKNUMBER"),
              i + 1.0);
  }
  EXPECT_EQ(GetMediaTrackInfo_Value(GetMasterTrack(nullptr), "IP_TRACKNUMBER"),
            -1.0);
}

TEST_F(TrackContractTest, TheMasterIsNoneOfTheTracks) {
  OpenProject(BuildFolders);
  MediaTrack* master = GetMasterTrack(nullptr);
  ASSERT_NE(master, nullptr);
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    EXPECT_NE(GetTrack(nullptr, i), master);
  }
  EXPECT_EQ(GetName(master), "MASTER");
}

TEST_F(TrackContractTest, TrackStateHasTheTracksFlags) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Plain");
    project.AddTrack("Selected")->selected = true;
    project.AddTrack("Muted")->mute = true;
    project.AddTrack("Soloed")->solo = true;
    FakeTrack* in_place = project.AddTrack("Soloed in place");
    in_place->solo = true;
    in_place->solo_in_place = true;
    project.AddTrack("Armed")->rec_arm = true;
    FakeTrack* folder = project.AddTrack("Folder");
    project.AddTrack("In folder", folder);
    project.AddTrack("Hidden in TCP")->show_in_tcp = false;
    project.AddTrack("Hidden in mixer")->show_in_mixer = false;
    FakeTrack* master = project.GetMasterTrack();
    master->selected = true;
    master->mute = true;
  });
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Plain")), 0);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Selected")), kTrackStateSelected);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Muted")), kTrackStateMute);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Soloed")), kTrackStateSolo);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Soloed in place")), kSoloedInPlace);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Armed")), kTrackStateRecArm);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Folder")), kTrackStateFolder);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("In folder")), 0);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Hidden in TCP")),
            kTrackStateHiddenInTcp);
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Hidden in mixer")),
            kTrackStateHiddenInMixer);
  EXPECT_EQ(GetTrackStateFlags(GetMasterTrack(nullptr)),
            kTrackStateSelected | kTrackStateMute);
}

TEST_F(TrackContractTest, TheMasterHiddenInTheTrackPanelIsOnlyInItsState) {
  OpenProject([](FakeProject& project) {
    project.GetMasterTrack()->show_in_tcp = false;
  });
  MediaTrack* master = GetMasterTrack(nullptr);
  EXPECT_EQ(GetTrackStateFlags(master), kTrackStateHiddenInTcp);
  EXPECT_EQ(GetMediaTrackInfo_Value(master, "B_SHOWINTCP"), 1.0);
}

TEST_F(TrackContractTest, TracksHaveTheirNames) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Kick");
    project.AddTrack("");
  });
  MediaTrack* kick = GetTrack(nullptr, 0);
  MediaTrack* unnamed = GetTrack(nullptr, 1);
  EXPECT_EQ(GetName(unnamed), "");

  std::array<char, 64> name = {};
  EXPECT_TRUE(GetSetMediaTrackInfo_String(kick, "P_NAME", name.data(), false));
  EXPECT_STREQ(name.data(), "Kick");

  std::string new_name = "Snare";
  EXPECT_TRUE(
      GetSetMediaTrackInfo_String(kick, "P_NAME", new_name.data(), true));
  EXPECT_EQ(GetName(kick), "Snare");
  name = {};
  EXPECT_TRUE(GetSetMediaTrackInfo_String(kick, "P_NAME", name.data(), false));
  EXPECT_STREQ(name.data(), "Snare");
}

TEST_F(TrackContractTest, TheMastersNameCantBeReadOrSet) {
  OpenProject([](FakeProject& project) {});
  MediaTrack* master = GetMasterTrack(nullptr);
  std::string name = "Unchanged";
  EXPECT_FALSE(
      GetSetMediaTrackInfo_String(master, "P_NAME", name.data(), false));
  EXPECT_STREQ(name.c_str(), "");

  std::string new_name = "Main";
  EXPECT_FALSE(
      GetSetMediaTrackInfo_String(master, "P_NAME", new_name.data(), true));
  EXPECT_EQ(GetName(master), "MASTER");
}

TEST_F(TrackContractTest, TracksHaveTheirColors) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("None");
    project.AddTrack("Orange")->color = 0x014080FF;
    project.AddTrack("Black")->color = 0x01000000;
    project.GetMasterTrack()->color = 0x01FF0000;
  });
  EXPECT_EQ(GetTrackColor(FindTrack("None")), 0);
  EXPECT_EQ(GetTrackColor(FindTrack("Orange")), 0x014080FF);
  EXPECT_EQ(GetTrackColor(FindTrack("Black")), 0x01000000);

  // The master's reads as none, even when it has one.
  EXPECT_EQ(GetTrackColor(GetMasterTrack(nullptr)), 0);
}

TEST_F(TrackContractTest, TracksHaveTheirVolumeAndPan) {
  OpenProject([](FakeProject& project) {
    FakeTrack* track = project.AddTrack("Track");
    track->volume = 0.5;
    track->pan = -0.25;
    FakeTrack* master = project.GetMasterTrack();
    master->volume = 2.0;
    master->pan = 0.75;
  });
  double volume = 0.0;
  double pan = 0.0;
  EXPECT_TRUE(GetTrackUIVolPan(GetTrack(nullptr, 0), &volume, &pan));
  EXPECT_EQ(volume, 0.5);
  EXPECT_EQ(pan, -0.25);
  EXPECT_TRUE(GetTrackUIVolPan(GetMasterTrack(nullptr), &volume, &pan));
  EXPECT_EQ(volume, 2.0);
  EXPECT_EQ(pan, 0.75);
}

TEST_F(TrackContractTest, TracksHaveTheirVisibilityAndAutomationMode) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Shown");
    project.AddTrack("Hidden in TCP")->show_in_tcp = false;
    project.AddTrack("Hidden in mixer")->show_in_mixer = false;
    for (int mode = 0; mode <= 4; ++mode) {
      project.AddTrack(absl::StrCat("Mode ", mode))->auto_mode = mode;
    }
  });
  MediaTrack* shown = FindTrack("Shown");
  EXPECT_EQ(GetMediaTrackInfo_Value(shown, "B_SHOWINTCP"), 1.0);
  EXPECT_EQ(GetMediaTrackInfo_Value(shown, "B_SHOWINMIXER"), 1.0);
  EXPECT_EQ(GetMediaTrackInfo_Value(FindTrack("Hidden in TCP"), "B_SHOWINTCP"),
            0.0);
  EXPECT_EQ(
      GetMediaTrackInfo_Value(FindTrack("Hidden in mixer"), "B_SHOWINMIXER"),
      0.0);
  for (int mode = 0; mode <= 4; ++mode) {
    EXPECT_EQ(GetMediaTrackInfo_Value(FindTrack(absl::StrCat("Mode ", mode)),
                                      "I_AUTOMODE"),
              mode);
  }
}

TEST_F(TrackContractTest, EachTrackHasItsOwnGuid) {
  OpenProject(BuildFolders);
  std::vector<std::string> guids = {
      GetGuidText(GetTrackGUID(GetMasterTrack(nullptr)))};
  for (int i = 0; i < CountTracks(nullptr); ++i) {
    MediaTrack* track = GetTrack(nullptr, i);
    const std::string guid = GetGuidText(GetTrackGUID(track));
    EXPECT_EQ(guid, FormatGuid(*GetTrackGUID(track)));
    EXPECT_EQ(GetGuidText(GetTrackGUID(track)), guid);
    for (const std::string& other : guids) {
      EXPECT_NE(guid, other);
    }
    guids.push_back(guid);
  }
}

//------------------------------------------------------------------------------
// Track changes
//------------------------------------------------------------------------------

class TrackChangeContractTest : public ContractTest {
 protected:
  // Sets `track`'s mute, solo, or rec arm, by its GetTrackState() flag, with
  // SetTrackUIMute() and the like. Then ends the entry point, so a test can
  // change one track after another.
  int Set(int flag, MediaTrack* track, int value, int group_flags) {
    int result = -1;
    if (flag == kTrackStateMute) {
      result = SetTrackUIMute(track, value, group_flags);
    } else if (flag == kTrackStateSolo) {
      result = SetTrackUISolo(track, value, group_flags);
    } else {
      result = SetTrackUIRecArm(track, value, group_flags);
    }
    EndEntryPoint();
    return result;
  }
};

TEST_F(TrackChangeContractTest, MuteAndRecArmAreSetClearedAndToggled) {
  OpenProject([](FakeProject& project) { project.AddTrack("Track"); });
  MediaTrack* track = GetTrack(nullptr, 0);
  for (int flag : {kTrackStateMute, kTrackStateRecArm}) {
    SCOPED_TRACE(flag);
    EXPECT_EQ(Set(flag, track, 1, kPreventGroupingAndGanging), 1);
    EXPECT_EQ(GetTrackStateFlags(track), flag);
    EXPECT_EQ(Set(flag, track, -1, kPreventGroupingAndGanging), 0);
    EXPECT_EQ(GetTrackStateFlags(track), 0);
    EXPECT_EQ(Set(flag, track, -1, kPreventGroupingAndGanging), 1);
    EXPECT_EQ(GetTrackStateFlags(track), flag);
    EXPECT_EQ(Set(flag, track, 0, kPreventGroupingAndGanging), 0);
    EXPECT_EQ(GetTrackStateFlags(track), 0);

    // Any value above 0 sets it.
    EXPECT_EQ(Set(flag, track, 2, kPreventGroupingAndGanging), 1);
    EXPECT_EQ(GetTrackStateFlags(track), flag);
    Set(flag, track, 0, kPreventGroupingAndGanging);
  }
}

TEST_F(TrackChangeContractTest, SoloIsInPlaceByDefault) {
  OpenProject([](FakeProject& project) { project.AddTrack("Track"); });
  MediaTrack* track = GetTrack(nullptr, 0);
  EXPECT_EQ(Set(kTrackStateSolo, track, 1, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kSoloedInPlace);
  EXPECT_EQ(Set(kTrackStateSolo, track, -1, kPreventGroupingAndGanging), 0);
  EXPECT_EQ(GetTrackStateFlags(track), 0);
  EXPECT_EQ(Set(kTrackStateSolo, track, -1, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kSoloedInPlace);

  // 2 solos it not in place, and 4 in place.
  EXPECT_EQ(Set(kTrackStateSolo, track, 2, kPreventGroupingAndGanging), 1);
  EXPECT_EQ(GetTrackStateFlags(track), kTrackStateSolo);
  EXPECT_EQ(Set(kTrackStateSolo, track, 4, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kSoloedInPlace);
  EXPECT_EQ(Set(kTrackStateSolo, track, 0, kPreventGroupingAndGanging), 0);
  EXPECT_EQ(GetTrackStateFlags(track), 0);
}

TEST_F(TrackChangeContractTest, TheMasterCanBeMutedAndSoloedButNotArmed) {
  OpenProject([](FakeProject& project) {});
  MediaTrack* master = GetMasterTrack(nullptr);
  EXPECT_EQ(Set(kTrackStateMute, master, 1, kPreventGroupingAndGanging), 1);
  EXPECT_EQ(GetTrackStateFlags(master), kTrackStateMute);
  Set(kTrackStateMute, master, 0, kPreventGroupingAndGanging);

  // Its solo reads as in place, but never is, and isn't any track's.
  EXPECT_EQ(Set(kTrackStateSolo, master, 1, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(master), kTrackStateSolo);
  EXPECT_FALSE(AnyTrackSolo(nullptr));
  Set(kTrackStateSolo, master, 0, kPreventGroupingAndGanging);

  EXPECT_EQ(Set(kTrackStateRecArm, master, 1, kPreventGroupingAndGanging), -1);
  EXPECT_EQ(GetTrackStateFlags(master), 0);
}

TEST_F(TrackChangeContractTest, AnyTrackSoloIsWhetherATrackIsSoloed) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Drums");
    project.AddTrack("Bass")->solo = true;
  });
  MediaTrack* drums = GetTrack(nullptr, 0);
  MediaTrack* bass = GetTrack(nullptr, 1);
  EXPECT_TRUE(AnyTrackSolo(nullptr));
  Set(kTrackStateSolo, bass, 0, kPreventGroupingAndGanging);
  EXPECT_FALSE(AnyTrackSolo(nullptr));
  Set(kTrackStateSolo, drums, 1, kPreventGroupingAndGanging);
  EXPECT_TRUE(AnyTrackSolo(nullptr));
}

TEST_F(TrackChangeContractTest, VolumeAndPanAreSet) {
  OpenProject([](FakeProject& project) { project.AddTrack("Track"); });
  for (MediaTrack* track : {GetTrack(nullptr, 0), GetMasterTrack(nullptr)}) {
    for (double volume : {0.5, 0.0, 4.0}) {
      EXPECT_EQ(CSurf_OnVolumeChangeEx(track, volume, false, false), volume);
      EXPECT_EQ(GetVolume(track), volume);
    }
    for (double pan : {-0.25, -1.0, 1.0}) {
      EXPECT_EQ(CSurf_OnPanChangeEx(track, pan, false, false), pan);
      EXPECT_EQ(GetPan(track), pan);
    }

    // A pan past an end is clamped to it.
    EXPECT_EQ(CSurf_OnPanChangeEx(track, -2.0, false, false), -1.0);
    EXPECT_EQ(GetPan(track), -1.0);
  }
}

// A and B are in a group, C isn't, and D is in another. B's volume and pan are
// 0.5.
void BuildGroups(FakeProject& project) {
  project.AddTrack("A")->group = 1;
  FakeTrack* b = project.AddTrack("B");
  b->group = 1;
  b->volume = 0.5;
  b->pan = 0.5;
  project.AddTrack("C");
  project.AddTrack("D")->group = 2;
}

TEST_F(TrackChangeContractTest, GroupedMuteAndSoloSetTheGroup) {
  OpenProject(BuildGroups);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  MediaTrack* c = FindTrack("C");
  MediaTrack* d = FindTrack("D");
  for (int flag : {kTrackStateMute, kTrackStateSolo}) {
    SCOPED_TRACE(flag);
    Set(flag, a, 1, kPreventSelectionGanging);
    EXPECT_TRUE(HasFlag(a, flag));
    EXPECT_TRUE(HasFlag(b, flag));
    EXPECT_FALSE(HasFlag(c, flag));
    EXPECT_FALSE(HasFlag(d, flag));

    // &1 prevents grouping.
    Set(flag, b, 0, kPreventGroupingAndGanging);
    EXPECT_TRUE(HasFlag(a, flag));
    EXPECT_FALSE(HasFlag(b, flag));

    // A toggle sets the group to the track's new value.
    Set(flag, a, -1, kPreventSelectionGanging);
    EXPECT_FALSE(HasFlag(a, flag));
    EXPECT_FALSE(HasFlag(b, flag));

    // Even when the track's doesn't change.
    Set(flag, b, 1, kPreventGroupingAndGanging);
    Set(flag, a, 0, kPreventSelectionGanging);
    EXPECT_FALSE(HasFlag(b, flag));
  }

  // The group is soloed the same way.
  Set(kTrackStateSolo, a, 2, kPreventSelectionGanging);
  EXPECT_EQ(GetTrackStateFlags(b), kTrackStateSolo);
  Set(kTrackStateSolo, a, 4, kPreventSelectionGanging);
  EXPECT_EQ(GetTrackStateFlags(b), kSoloedInPlace);
}

TEST_F(TrackChangeContractTest, GroupedRecArmTogglesTheGroup) {
  OpenProject(BuildGroups);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  EXPECT_EQ(Set(kTrackStateRecArm, a, 1, kPreventSelectionGanging), 1);
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));
  EXPECT_FALSE(HasFlag(FindTrack("C"), kTrackStateRecArm));
  EXPECT_FALSE(HasFlag(FindTrack("D"), kTrackStateRecArm));

  // Unarming A toggles B, which was unarmed.
  Set(kTrackStateRecArm, b, 0, kPreventGroupingAndGanging);
  EXPECT_EQ(Set(kTrackStateRecArm, a, 0, kPreventSelectionGanging), 0);
  EXPECT_FALSE(HasFlag(a, kTrackStateRecArm));
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));

  // A rec arm that doesn't change the track changes nothing.
  EXPECT_EQ(Set(kTrackStateRecArm, a, 0, kPreventSelectionGanging), 0);
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));
}

TEST_F(TrackChangeContractTest, GroupedVolumeAndPanMoveTheGroupAsMuch) {
  OpenProject(BuildGroups);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");

  // Volume by its ratio, and pan by its difference.
  CSurf_OnVolumeChangeEx(a, 0.5, false, true);
  EXPECT_DOUBLE_EQ(GetVolume(b), 0.25);
  CSurf_OnVolumeChangeEx(a, 4.0, false, true);
  EXPECT_DOUBLE_EQ(GetVolume(b), 2.0);
  CSurf_OnPanChangeEx(a, -0.25, false, true);
  EXPECT_DOUBLE_EQ(GetPan(b), 0.25);
  EXPECT_EQ(GetVolume(FindTrack("C")), 1.0);
  EXPECT_EQ(GetVolume(FindTrack("D")), 1.0);

  // Without allow_gang, only the track changes.
  CSurf_OnVolumeChangeEx(a, 1.0, false, false);
  EXPECT_DOUBLE_EQ(GetVolume(b), 2.0);
  CSurf_OnPanChangeEx(a, 0.0, false, false);
  EXPECT_DOUBLE_EQ(GetPan(b), 0.25);
}

// The master, A, and B are selected, and C isn't. B's volume and pan are 0.5.
void BuildSelection(FakeProject& project) {
  project.GetMasterTrack()->selected = true;
  project.AddTrack("A")->selected = true;
  FakeTrack* b = project.AddTrack("B");
  b->selected = true;
  b->volume = 0.5;
  b->pan = 0.5;
  project.AddTrack("C");
}

TEST_F(TrackChangeContractTest, GangedMuteAndSoloSetEverySelectedTrack) {
  OpenProject(BuildSelection);
  MediaTrack* master = GetMasterTrack(nullptr);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  MediaTrack* c = FindTrack("C");
  for (int flag : {kTrackStateMute, kTrackStateSolo}) {
    SCOPED_TRACE(flag);
    Set(flag, a, 1, kPreventTrackGrouping);
    EXPECT_TRUE(HasFlag(master, flag));
    EXPECT_TRUE(HasFlag(b, flag));
    EXPECT_FALSE(HasFlag(c, flag));

    // &2 prevents ganging.
    Set(flag, a, 0, kPreventGroupingAndGanging);
    EXPECT_TRUE(HasFlag(b, flag));

    // Ganged tracks are set to the track's value, even when the track's
    // doesn't change.
    Set(flag, a, 0, kPreventTrackGrouping);
    EXPECT_FALSE(HasFlag(master, flag));
    EXPECT_FALSE(HasFlag(b, flag));

    // A track that isn't selected changes only itself.
    Set(flag, c, 1, kPreventTrackGrouping);
    EXPECT_FALSE(HasFlag(a, flag));
    Set(flag, c, 0, kPreventTrackGrouping);
  }
}

TEST_F(TrackChangeContractTest, GangedRecArmSetsEverySelectedTrack) {
  OpenProject(BuildSelection);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  EXPECT_EQ(Set(kTrackStateRecArm, a, 1, kPreventTrackGrouping), 1);
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));
  EXPECT_FALSE(HasFlag(FindTrack("C"), kTrackStateRecArm));

  // A toggle sets them to the track's new value.
  Set(kTrackStateRecArm, a, 0, kPreventGroupingAndGanging);
  EXPECT_EQ(Set(kTrackStateRecArm, a, -1, kPreventTrackGrouping), 1);
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));

  // A rec arm that doesn't change the track changes nothing.
  Set(kTrackStateRecArm, a, 0, kPreventGroupingAndGanging);
  EXPECT_EQ(Set(kTrackStateRecArm, a, 0, kPreventTrackGrouping), 0);
  EXPECT_TRUE(HasFlag(b, kTrackStateRecArm));

  // The master is selected, but can't be armed.
  EXPECT_EQ(
      Set(kTrackStateRecArm, GetMasterTrack(nullptr), 1, kPreventTrackGrouping),
      -1);
  EXPECT_FALSE(HasFlag(a, kTrackStateRecArm));
}

TEST_F(TrackChangeContractTest, GangedVolumeAndPanMoveEverySelectedTrack) {
  OpenProject(BuildSelection);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  MediaTrack* c = FindTrack("C");
  CSurf_OnVolumeChangeEx(a, 0.5, false, true);
  EXPECT_DOUBLE_EQ(GetVolume(GetMasterTrack(nullptr)), 0.5);
  EXPECT_DOUBLE_EQ(GetVolume(b), 0.25);
  EXPECT_EQ(GetVolume(c), 1.0);
  CSurf_OnPanChangeEx(a, -0.25, false, true);
  EXPECT_DOUBLE_EQ(GetPan(b), 0.25);

  // A track that isn't selected changes only itself.
  CSurf_OnVolumeChangeEx(c, 0.5, false, true);
  EXPECT_DOUBLE_EQ(GetVolume(a), 0.5);
}

TEST_F(TrackChangeContractTest, GangedTracksChangeTheirGroupsButNotTheReverse) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("A")->selected = true;
    FakeTrack* b = project.AddTrack("B");
    b->selected = true;
    b->group = 1;
    project.AddTrack("C")->group = 1;
  });
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  MediaTrack* c = FindTrack("C");
  for (int flag : {kTrackStateMute, kTrackStateSolo, kTrackStateRecArm}) {
    SCOPED_TRACE(flag);

    // A is ganged with B, which is grouped with C.
    Set(flag, a, 1, 0);
    EXPECT_TRUE(HasFlag(b, flag));
    EXPECT_TRUE(HasFlag(c, flag));
    Set(flag, a, 0, 0);
    EXPECT_FALSE(HasFlag(c, flag));

    // C is grouped with B, but isn't selected.
    Set(flag, c, 1, 0);
    EXPECT_TRUE(HasFlag(b, flag));
    EXPECT_FALSE(HasFlag(a, flag));
    Set(flag, c, 0, 0);
  }
}

TEST_F(TrackChangeContractTest, ChangesInABatchReadBackAtOnce) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Drums");
    project.AddTrack("Bass");
  });
  MediaTrack* drums = GetTrack(nullptr, 0);
  MediaTrack* bass = GetTrack(nullptr, 1);
  PreventUIRefresh(1);
  SetTrackUIMute(drums, 1, kPreventGroupingAndGanging);
  SetTrackSelected(bass, true);
  EXPECT_EQ(GetTrackStateFlags(drums), kTrackStateMute);
  EXPECT_EQ(GetTrackStateFlags(bass), kTrackStateSelected);
  PreventUIRefresh(-1);
  EXPECT_EQ(GetTrackStateFlags(drums), kTrackStateMute);
  EXPECT_EQ(GetTrackStateFlags(bass), kTrackStateSelected);
}

//------------------------------------------------------------------------------
// Selection
//------------------------------------------------------------------------------

using SelectionContractTest = ContractTest;

TEST_F(SelectionContractTest, SelectedTracksAreInOrderWithTheMasterIfWanted) {
  OpenProject([](FakeProject& project) {
    BuildSelection(project);
    project.AddTrack("D")->selected = true;
  });
  MediaTrack* master = GetMasterTrack(nullptr);
  MediaTrack* a = FindTrack("A");
  MediaTrack* b = FindTrack("B");
  MediaTrack* d = FindTrack("D");
  EXPECT_EQ(CountSelectedTracks(nullptr), 3);
  EXPECT_EQ(CountSelectedTracks2(nullptr, /*wantmaster=*/false), 3);
  EXPECT_EQ(GetSelectedTrack(nullptr, 0), a);
  EXPECT_EQ(GetSelectedTrack(nullptr, 1), b);
  EXPECT_EQ(GetSelectedTrack(nullptr, 2), d);
  EXPECT_EQ(GetSelectedTrack(nullptr, 3), nullptr);
  EXPECT_EQ(GetSelectedTrack(nullptr, -1), nullptr);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 0, /*wantmaster=*/false), a);

  EXPECT_EQ(CountSelectedTracks2(nullptr, /*wantmaster=*/true), 4);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 0, true), master);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 1, true), a);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 3, true), d);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 4, true), nullptr);
}

TEST_F(SelectionContractTest, SetTrackSelectedChangesOnlyTheTrack) {
  OpenProject(BuildSelection);
  MediaTrack* master = GetMasterTrack(nullptr);
  MediaTrack* a = FindTrack("A");
  MediaTrack* c = FindTrack("C");
  SetTrackSelected(c, true);
  EndEntryPoint();
  EXPECT_EQ(CountSelectedTracks(nullptr), 3);
  SetTrackSelected(a, false);
  EndEntryPoint();
  EXPECT_EQ(GetTrackStateFlags(a), 0);
  EXPECT_EQ(CountSelectedTracks(nullptr), 2);
  SetTrackSelected(master, false);
  EndEntryPoint();
  EXPECT_EQ(GetTrackStateFlags(master), 0);
  EXPECT_EQ(CountSelectedTracks2(nullptr, /*wantmaster=*/true), 2);
  SetTrackSelected(master, true);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 0, /*wantmaster=*/true), master);
}

TEST_F(SelectionContractTest, SetOnlyTrackSelectedUnselectsEveryOtherTrack) {
  OpenProject(BuildSelection);
  MediaTrack* master = GetMasterTrack(nullptr);
  MediaTrack* c = FindTrack("C");
  SetOnlyTrackSelected(c);
  EndEntryPoint();
  EXPECT_EQ(CountSelectedTracks2(nullptr, /*wantmaster=*/true), 1);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 0, /*wantmaster=*/true), c);

  SetOnlyTrackSelected(master);
  EXPECT_EQ(CountSelectedTracks(nullptr), 0);
  EXPECT_EQ(CountSelectedTracks2(nullptr, /*wantmaster=*/true), 1);
  EXPECT_EQ(GetSelectedTrack2(nullptr, 0, /*wantmaster=*/true), master);
}

//------------------------------------------------------------------------------
// Routes
//------------------------------------------------------------------------------

// Drums, Bus, and Reverb. Drums has a hardware output, at a volume of 0.75 and
// a pan of 0.25, then a send to Reverb, at 0.5 and -0.25, then a send to Bus.
// Bus has a muted send to Reverb. The master has a hardware output.
void BuildRoutes(FakeProject& project) {
  FakeTrack* drums = project.AddTrack("Drums");
  FakeTrack* bus = project.AddTrack("Bus");
  FakeTrack* reverb = project.AddTrack("Reverb");
  FakeRoute* output = project.AddHardwareOutput(drums);
  output->volume = 0.75;
  output->pan = 0.25;
  FakeRoute* to_reverb = project.AddSend(drums, reverb);
  to_reverb->volume = 0.5;
  to_reverb->pan = -0.25;
  project.AddSend(drums, bus);
  project.AddSend(bus, reverb)->mute = true;
  project.AddHardwareOutput(project.GetMasterTrack());
}

class RouteContractTest : public ContractTest {
 protected:
  RouteContractTest() {
    OpenProject(BuildRoutes);
    master_ = GetMasterTrack(nullptr);
    drums_ = FindTrack("Drums");
    bus_ = FindTrack("Bus");
    reverb_ = FindTrack("Reverb");
  }

  MediaTrack* master_ = nullptr;
  MediaTrack* drums_ = nullptr;
  MediaTrack* bus_ = nullptr;
  MediaTrack* reverb_ = nullptr;
};

// Returns the volume and pan `get` (GetTrackSendUIVolPan() or
// GetTrackReceiveUIVolPan()) reads for `track`'s route at `index`, or nothing
// if it returns false.
std::vector<double> GetVolPan(decltype(GetTrackSendUIVolPan) get,
                              MediaTrack* track, int index) {
  double volume = 0.0;
  double pan = 0.0;
  if (!get(track, index, &volume, &pan)) {
    return {};
  }
  return {volume, pan};
}

// Returns the mute `get` (GetTrackSendUIMute() or GetTrackReceiveUIMute())
// reads for `track`'s route at `index`, or nothing if it returns false.
std::optional<bool> GetMute(decltype(GetTrackSendUIMute) get, MediaTrack* track,
                            int index) {
  bool mute = false;
  if (!get(track, index, &mute)) {
    return std::nullopt;
  }
  return mute;
}

// Returns GetSetTrackSendInfo()'s `parameter` for `track`'s route at `index`
// in `category`.
MediaTrack* GetRouteTrack(MediaTrack* track, int category, int index,
                          const char* parameter) {
  return static_cast<MediaTrack*>(
      GetSetTrackSendInfo(track, category, index, parameter, nullptr));
}

TEST_F(RouteContractTest, RoutesAreCountedByCategory) {
  EXPECT_EQ(GetTrackNumSends(drums_, kReceiveCategory), 0);
  EXPECT_EQ(GetTrackNumSends(drums_, kSendCategory), 2);
  EXPECT_EQ(GetTrackNumSends(drums_, kHardwareOutputCategory), 1);
  EXPECT_EQ(GetTrackNumSends(bus_, kReceiveCategory), 1);
  EXPECT_EQ(GetTrackNumSends(bus_, kSendCategory), 1);
  EXPECT_EQ(GetTrackNumSends(bus_, kHardwareOutputCategory), 0);
  EXPECT_EQ(GetTrackNumSends(reverb_, kReceiveCategory), 2);
  EXPECT_EQ(GetTrackNumSends(reverb_, kSendCategory), 0);
  EXPECT_EQ(GetTrackNumSends(master_, kReceiveCategory), 0);
  EXPECT_EQ(GetTrackNumSends(master_, kSendCategory), 0);
  EXPECT_EQ(GetTrackNumSends(master_, kHardwareOutputCategory), 1);
}

// Sends are in the order of the tracks they go to, not the order they were
// added, as REAPER keeps each at its destination, as a receive.
TEST_F(RouteContractTest, SendsAndReceivesHaveTheirEnds) {
  EXPECT_EQ(GetRouteTrack(drums_, kSendCategory, 0, "P_DESTTRACK"), bus_);
  EXPECT_EQ(GetRouteTrack(drums_, kSendCategory, 1, "P_DESTTRACK"), reverb_);
  EXPECT_EQ(GetRouteTrack(drums_, kSendCategory, 0, "P_SRCTRACK"), drums_);
  EXPECT_EQ(GetRouteTrack(drums_, kSendCategory, 2, "P_DESTTRACK"), nullptr);
  EXPECT_EQ(GetRouteTrack(reverb_, kReceiveCategory, 0, "P_SRCTRACK"), drums_);
  EXPECT_EQ(GetRouteTrack(reverb_, kReceiveCategory, 1, "P_SRCTRACK"), bus_);
  EXPECT_EQ(GetRouteTrack(reverb_, kReceiveCategory, 1, "P_DESTTRACK"),
            reverb_);
  EXPECT_EQ(GetRouteTrack(reverb_, kReceiveCategory, 2, "P_SRCTRACK"), nullptr);
}

TEST_F(RouteContractTest, HardwareOutputsHaveOnlyTheirSource) {
  EXPECT_EQ(GetRouteTrack(drums_, kHardwareOutputCategory, 0, "P_SRCTRACK"),
            drums_);
  EXPECT_EQ(GetRouteTrack(drums_, kHardwareOutputCategory, 0, "P_DESTTRACK"),
            nullptr);
}

TEST_F(RouteContractTest, SendUiIndexesAreHardwareOutputsThenSends) {
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 0),
              ElementsAre(0.75, 0.25));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 1),
              ElementsAre(1.0, 0.0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 2),
              ElementsAre(0.5, -0.25));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 3), IsEmpty());
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, master_, 0),
              ElementsAre(1.0, 0.0));
  EXPECT_EQ(GetMute(GetTrackSendUIMute, bus_, 0), true);
  EXPECT_EQ(GetMute(GetTrackSendUIMute, drums_, 1), false);
  EXPECT_EQ(GetMute(GetTrackSendUIMute, drums_, 3), std::nullopt);
}

TEST_F(RouteContractTest, SendUiIndexesBelowZeroAreReceives) {
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, reverb_, -1),
              ElementsAre(0.5, -0.25));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, reverb_, -2),
              ElementsAre(1.0, 0.0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, reverb_, -3), IsEmpty());
  EXPECT_EQ(GetMute(GetTrackSendUIMute, reverb_, -2), true);
  EXPECT_EQ(GetMute(GetTrackSendUIMute, reverb_, -3), std::nullopt);
}

TEST_F(RouteContractTest, ReceiveUiIndexesAreReceives) {
  EXPECT_THAT(GetVolPan(GetTrackReceiveUIVolPan, reverb_, 0),
              ElementsAre(0.5, -0.25));
  EXPECT_THAT(GetVolPan(GetTrackReceiveUIVolPan, reverb_, 1),
              ElementsAre(1.0, 0.0));
  EXPECT_THAT(GetVolPan(GetTrackReceiveUIVolPan, reverb_, 2), IsEmpty());
  EXPECT_THAT(GetVolPan(GetTrackReceiveUIVolPan, drums_, 0), IsEmpty());
  EXPECT_EQ(GetMute(GetTrackReceiveUIMute, reverb_, 0), false);
  EXPECT_EQ(GetMute(GetTrackReceiveUIMute, reverb_, 1), true);
  EXPECT_EQ(GetMute(GetTrackReceiveUIMute, reverb_, 2), std::nullopt);
}

TEST_F(RouteContractTest, SettersChangeTheRouteAtBothEnds) {
  EXPECT_TRUE(SetTrackSendUIVol(drums_, 1, 0.25, 0));
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 1, -0.5, 0));
  EXPECT_THAT(GetVolPan(GetTrackReceiveUIVolPan, bus_, 0),
              ElementsAre(0.25, -0.5));

  // From the receive's end.
  EXPECT_TRUE(SetTrackSendUIVol(reverb_, -2, 0.5, 0));
  EXPECT_TRUE(SetTrackSendUIPan(reverb_, -2, 0.75, 0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, bus_, 0), ElementsAre(0.5, 0.75));

  EXPECT_TRUE(SetTrackSendUIVol(drums_, 0, 0.5, 0));
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 0, -1.0, 0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 0),
              ElementsAre(0.5, -1.0));
}

TEST_F(RouteContractTest, MuteTogglesTheRouteAtBothEnds) {
  EXPECT_TRUE(ToggleTrackSendUIMute(reverb_, -1));
  EXPECT_EQ(GetMute(GetTrackSendUIMute, drums_, 2), true);
  EXPECT_TRUE(ToggleTrackSendUIMute(bus_, 0));
  EXPECT_EQ(GetMute(GetTrackReceiveUIMute, reverb_, 1), false);
  EXPECT_TRUE(ToggleTrackSendUIMute(drums_, 0));
  EXPECT_EQ(GetMute(GetTrackSendUIMute, drums_, 0), true);
}

TEST_F(RouteContractTest, SettersOfARouteThatIsntThereFail) {
  EXPECT_FALSE(SetTrackSendUIVol(drums_, 3, 0.5, 0));
  EXPECT_FALSE(SetTrackSendUIPan(drums_, 3, 0.5, 0));
  EXPECT_FALSE(SetTrackSendUIVol(drums_, -1, 0.5, 0));
  EXPECT_FALSE(ToggleTrackSendUIMute(drums_, 3));
  EXPECT_FALSE(ToggleTrackSendUIMute(reverb_, -3));
}

TEST_F(RouteContractTest, AnInstantEditSetsTheValueButEndingAnEditDoesnt) {
  EXPECT_TRUE(SetTrackSendUIVol(drums_, 2, 0.25, /*isend=*/-1));
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 2, 0.5, /*isend=*/-1));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 2),
              ElementsAre(0.25, 0.5));
  EXPECT_TRUE(SetTrackSendUIVol(drums_, 2, 0.75, /*isend=*/1));
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 2, -0.5, /*isend=*/1));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 2),
              ElementsAre(0.25, 0.5));
}

TEST_F(RouteContractTest, PanPastAnEndIsntClamped) {
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 1, 2.0, 0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 1),
              ElementsAre(1.0, 2.0));
  EXPECT_TRUE(SetTrackSendUIPan(drums_, 1, -2.0, 0));
  EXPECT_THAT(GetVolPan(GetTrackSendUIVolPan, drums_, 1),
              ElementsAre(1.0, -2.0));
}

//------------------------------------------------------------------------------
// The project
//------------------------------------------------------------------------------

using ProjectContractTest = ContractTest;

TEST_F(ProjectContractTest, AProjectOpensStoppedAtItsCursor) {
  OpenProject([](FakeProject& project) { project.SetCursorPosition(2.5); });
  EXPECT_EQ(GetPlayState(), 0);
  EXPECT_EQ(GetPlayPosition(), 0.0);
  EXPECT_EQ(GetCursorPosition(), 2.5);
}

TEST_F(ProjectContractTest, AProjectOpensCleanWithNothingToRedo) {
  OpenProject([](FakeProject& project) {});
  EXPECT_EQ(IsProjectDirty(nullptr), 0);
  EXPECT_EQ(Undo_CanRedo2(nullptr), nullptr);
}

TEST_F(ProjectContractTest, SelectedItemsAreCounted) {
  OpenProject([](FakeProject& project) {
    project.AddTrack("Track");
    project.SetSelectedItemCount(2);
  });
  EXPECT_EQ(CountSelectedMediaItems(nullptr), 2);
}

TEST_F(ProjectContractTest, TheAutomationOverrideIsTheProjects) {
  OpenProject([](FakeProject& project) { project.SetAutomationOverride(4); });
  EXPECT_EQ(GetGlobalAutomationOverride(), 4);
  for (int mode : {-1, 0, 3, 5, 6}) {
    SetGlobalAutomationOverride(mode);
    EXPECT_EQ(GetGlobalAutomationOverride(), mode);
  }
}

//------------------------------------------------------------------------------
// Undo
//------------------------------------------------------------------------------

// A and B, with a send from A to B.
class UndoContractTest : public ContractTest {
 protected:
  UndoContractTest() {
    OpenProject([](FakeProject& project) {
      FakeTrack* a = project.AddTrack("A");
      project.AddSend(a, project.AddTrack("B"));
    });
    master_ = GetMasterTrack(nullptr);
    a_ = FindTrack("A");
    b_ = FindTrack("B");
  }

  // Runs Edit: Undo or Edit: Redo.
  void Undo() { Main_OnCommand(kUndoAction, 0); }
  void Redo() { Main_OnCommand(kRedoAction, 0); }

  // Returns the name of the undo point Edit: Redo would redo, or nothing if
  // there is none.
  std::optional<std::string> GetRedo() {
    const char* redo = Undo_CanRedo2(nullptr);
    if (redo == nullptr) {
      return std::nullopt;
    }
    return redo;
  }

  // Mutes or unmutes `track`, without grouping or ganging.
  void SetMute(MediaTrack* track, bool mute) {
    SetTrackUIMute(track, mute ? 1 : 0, kPreventGroupingAndGanging);
  }

  // Adds an undo point of the whole project.
  void AddUndoPoint(const char* name) {
    Undo_OnStateChangeEx(name, UNDO_STATE_TRACKCFG, -1);
  }

  // Changes A's mute, solo, rec arm, name, and volume, and the send's volume
  // and pan, without an undo point.
  void ChangeA() {
    SetMute(a_, true);
    SetTrackUISolo(a_, 1, kPreventGroupingAndGanging);
    SetTrackUIRecArm(a_, 1, kPreventGroupingAndGanging);
    std::string name = "Z";
    GetSetMediaTrackInfo_String(a_, "P_NAME", name.data(), true);
    CSurf_OnVolumeChangeEx(a_, 0.5, false, false);
    SetTrackSendUIVol(a_, 0, 0.5, 0);
    SetTrackSendUIPan(a_, 0, 0.25, 0);
  }

  // Expects what ChangeA() changes to be as it left them, if `changed`, and
  // otherwise as they started.
  void ExpectAChanged(bool changed) {
    constexpr int kFlags = kTrackStateMute | kSoloedInPlace | kTrackStateRecArm;
    EXPECT_EQ(GetTrackStateFlags(a_) & kFlags, changed ? kFlags : 0);
    EXPECT_EQ(GetName(a_), changed ? "Z" : "A");
    EXPECT_EQ(GetVolume(a_), changed ? 0.5 : 1.0);
    const std::vector<double> send_vol_pan =
        changed ? std::vector<double>{0.5, 0.25}
                : std::vector<double>{1.0, 0.0};
    EXPECT_EQ(GetSendVolPan(), send_vol_pan);
  }

  // Returns the send's volume and pan.
  std::vector<double> GetSendVolPan() {
    return GetVolPan(GetTrackSendUIVolPan, a_, 0);
  }

  MediaTrack* master_ = nullptr;
  MediaTrack* a_ = nullptr;
  MediaTrack* b_ = nullptr;
};

TEST_F(UndoContractTest, ChangesAddNoUndoPointOfTheirOwn) {
  ChangeA();
  SetTrackSelected(a_, true);
  SetGlobalAutomationOverride(4);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  // So there is nothing to undo.
  Undo();
  ExpectAChanged(true);
  EXPECT_TRUE(HasFlag(a_, kTrackStateSelected));
  EXPECT_EQ(GetGlobalAutomationOverride(), 4);
  EXPECT_EQ(GetRedo(), std::nullopt);
}

TEST_F(UndoContractTest, AnUndoPointHoldsEveryChangeSinceTheLast) {
  ChangeA();
  EndEntryPoint();
  SetMute(master_, true);
  AddUndoPoint("Change");
  EXPECT_EQ(IsProjectDirty(nullptr), 1);

  Undo();
  ExpectAChanged(false);
  EXPECT_FALSE(HasFlag(master_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), "Change");

  Redo();
  ExpectAChanged(true);
  EXPECT_TRUE(HasFlag(master_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), std::nullopt);
}

TEST_F(UndoContractTest, AnUndoPointIsOnlyAddedForAChange) {
  AddUndoPoint("Nothing");
  SetMute(a_, true);
  SetMute(a_, false);
  AddUndoPoint("Back");
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  // A track's state is UNDO_STATE_TRACKCFG's.
  SetMute(a_, true);
  Undo_OnStateChangeEx("Other", UNDO_STATE_MISCCFG, -1);
  Undo_OnStateChangeEx("None", 0, -1);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);
  Undo_OnStateChangeEx("All", UNDO_STATE_ALL, -1);
  EXPECT_EQ(IsProjectDirty(nullptr), 1);
  Undo();
  EXPECT_FALSE(HasFlag(a_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), "All");
}

TEST_F(UndoContractTest, SelectionAndTheOverrideArentUndone) {
  SetTrackSelected(a_, true);
  SetGlobalAutomationOverride(4);
  SetMute(a_, true);
  AddUndoPoint("Change");
  EndEntryPoint();
  SetTrackSelected(a_, false);
  EndEntryPoint();
  SetTrackSelected(b_, true);
  SetGlobalAutomationOverride(2);

  Undo();
  EXPECT_EQ(GetTrackStateFlags(a_), 0);
  EXPECT_EQ(GetTrackStateFlags(b_), kTrackStateSelected);
  EXPECT_EQ(GetGlobalAutomationOverride(), 2);
}

TEST_F(UndoContractTest, UndoAndRedoStepThroughThePoints) {
  SetMute(a_, true);
  AddUndoPoint("Mute A");
  EndEntryPoint();
  SetMute(b_, true);
  AddUndoPoint("Mute B");

  Undo();
  EXPECT_TRUE(HasFlag(a_, kTrackStateMute));
  EXPECT_FALSE(HasFlag(b_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), "Mute B");
  Undo();
  EXPECT_FALSE(HasFlag(a_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), "Mute A");

  // The project opened with nothing to undo.
  Undo();
  EXPECT_EQ(GetRedo(), "Mute A");

  Redo();
  EXPECT_TRUE(HasFlag(a_, kTrackStateMute));
  EXPECT_FALSE(HasFlag(b_, kTrackStateMute));
  Redo();
  EXPECT_TRUE(HasFlag(b_, kTrackStateMute));
  EXPECT_EQ(GetRedo(), std::nullopt);
  Redo();
  EXPECT_TRUE(HasFlag(b_, kTrackStateMute));
}

TEST_F(UndoContractTest, UndoAndRedoDropChangesSinceTheLastPoint) {
  SetMute(a_, true);
  AddUndoPoint("Mute A");
  EndEntryPoint();
  SetMute(b_, true);
  Undo();
  EXPECT_FALSE(HasFlag(a_, kTrackStateMute));
  EXPECT_FALSE(HasFlag(b_, kTrackStateMute));

  // A change since doesn't drop the redo.
  EndEntryPoint();
  SetMute(b_, true);
  EXPECT_EQ(GetRedo(), "Mute A");
  Redo();
  EXPECT_TRUE(HasFlag(a_, kTrackStateMute));
  EXPECT_FALSE(HasFlag(b_, kTrackStateMute));
}

TEST_F(UndoContractTest, ANewPointDropsTheRedo) {
  SetMute(a_, true);
  AddUndoPoint("Mute A");
  Undo();
  EndEntryPoint();
  SetMute(b_, true);
  AddUndoPoint("Mute B");
  EXPECT_EQ(GetRedo(), std::nullopt);
  Redo();
  EXPECT_FALSE(HasFlag(a_, kTrackStateMute));
}

TEST_F(UndoContractTest, RouteMuteAddsAnUndoPoint) {
  // From either end.
  for (auto [track, index] : {std::pair(a_, 0), std::pair(b_, -1)}) {
    ToggleTrackSendUIMute(track, index);
    EXPECT_EQ(GetMute(GetTrackSendUIMute, a_, 0), true);
    Undo();
    EXPECT_EQ(GetMute(GetTrackSendUIMute, a_, 0), false);
    EXPECT_EQ(GetRedo(), "Toggle send mute");
  }
}

TEST_F(UndoContractTest, AnInstantRouteEditAddsAnUndoPoint) {
  // To the same value, it changes nothing.
  SetTrackSendUIVol(a_, 0, 1.0, /*isend=*/-1);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  SetTrackSendUIVol(a_, 0, 0.5, /*isend=*/-1);
  Undo();
  EXPECT_THAT(GetSendVolPan(), ElementsAre(1.0, 0.0));
  EXPECT_EQ(GetRedo(), "Adjust send volume");

  // From either end.
  SetTrackSendUIPan(b_, -1, 0.5, /*isend=*/-1);
  Undo();
  EXPECT_THAT(GetSendVolPan(), ElementsAre(1.0, 0.0));
  EXPECT_EQ(GetRedo(), "Adjust send pan");
}

TEST_F(UndoContractTest, EndingARouteEditAddsAnUndoPoint) {
  // With nothing changed, it changes nothing.
  SetTrackSendUIVol(a_, 0, 0.5, kEndEdit);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  // It holds every change since the last point, a pan too.
  SetTrackSendUIPan(a_, 0, 0.5, 0);
  SetTrackSendUIVol(a_, 0, 0.25, kEndEdit);
  EXPECT_EQ(IsProjectDirty(nullptr), 1);
  Undo();
  EXPECT_THAT(GetSendVolPan(), ElementsAre(1.0, 0.0));
  EXPECT_EQ(GetRedo(), "Adjust send volume");
}

TEST_F(UndoContractTest, SurfaceChangesAddAPointWhenTheOtherKindChanges) {
  CSurf_OnVolumeChangeEx(a_, 0.5, false, false);
  CSurf_OnVolumeChangeEx(b_, 0.5, false, false);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  // The point holds the change that adds it.
  CSurf_OnPanChangeEx(b_, 0.5, false, false);
  EXPECT_EQ(IsProjectDirty(nullptr), 1);
  CSurf_OnVolumeChangeEx(a_, 0.25, false, false);

  Undo();
  EXPECT_EQ(GetVolume(a_), 0.5);
  EXPECT_EQ(GetVolume(b_), 0.5);
  EXPECT_EQ(GetPan(b_), 0.5);
  EXPECT_EQ(GetRedo(), "Adjust track pan (via surface)");
  Undo();
  EXPECT_EQ(GetVolume(a_), 1.0);
  EXPECT_EQ(GetVolume(b_), 1.0);
  EXPECT_EQ(GetPan(b_), 0.0);
  EXPECT_EQ(GetRedo(), "Adjust track volume (via surface)");
}

TEST_F(UndoContractTest, ASurfaceChangeStaysHeldThroughOtherPointsAndUndo) {
  CSurf_OnVolumeChangeEx(a_, 0.5, false, false);
  AddUndoPoint("Point");
  CSurf_OnVolumeChangeEx(a_, 0.25, false, false);
  CSurf_OnPanChangeEx(b_, 0.5, false, false);
  Undo();
  EXPECT_EQ(GetVolume(a_), 0.5);
  EXPECT_EQ(GetPan(b_), 0.0);
  EXPECT_EQ(GetRedo(), "Adjust track volume (via surface)");

  // The pan is still held.
  CSurf_OnVolumeChangeEx(a_, 0.75, false, false);
  Undo();
  EXPECT_EQ(GetVolume(a_), 0.5);
  EXPECT_EQ(GetRedo(), "Adjust track pan (via surface)");
}

TEST_F(UndoContractTest, AutomationModeActionsAddAPointIfAModeChanges) {
  constexpr int kTouchAction = GetAutoModeAction(AutoMode::kTouch);
  Main_OnCommand(kTouchAction, 0);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);
  SetTrackSelected(a_, true);
  Main_OnCommand(GetAutoModeAction(AutoMode::kTrimRead), 0);
  EXPECT_EQ(IsProjectDirty(nullptr), 0);

  Main_OnCommand(kTouchAction, 0);
  EXPECT_EQ(IsProjectDirty(nullptr), 1);
  Undo();
  EXPECT_EQ(GetMediaTrackInfo_Value(a_, "I_AUTOMODE"), 0.0);
  EXPECT_EQ(GetRedo(), "Change track envelope automation mode");
}

//------------------------------------------------------------------------------
// Text
//------------------------------------------------------------------------------

// Positions are in the open project's tempo and rates, which are REAPER's
// defaults in every project the tests open, and the one REAPER starts with.
using TextContractTest = ContractTest;

// Returns what mkvolstr() writes for `volume`.
std::string GetVolumeText(double volume) {
  std::array<char, 64> text = {};
  mkvolstr(text.data(), volume);
  return text.data();
}

// Returns what mkpanstr() writes for `pan`.
std::string GetPanText(double pan) {
  std::array<char, 64> text = {};
  mkpanstr(text.data(), pan);
  return text.data();
}

// Returns what format_timestr_pos() writes for `position` in `mode`.
std::string GetPositionText(double position, int mode) {
  std::array<char, 64> text = {};
  format_timestr_pos(position, text.data(), static_cast<int>(text.size()),
                     mode);
  return text.data();
}

TEST_F(TextContractTest, VolumesAreDecibelsToTwoDecimalsBelowTen) {
  EXPECT_EQ(GetVolumeText(1.0), "0.00dB");
  EXPECT_EQ(GetVolumeText(0.5), "-6.02dB");
  EXPECT_EQ(GetVolumeText(2.0), "+6.02dB");
  EXPECT_EQ(GetVolumeText(3.162), "+10.00dB");
  EXPECT_EQ(GetVolumeText(3.17), "+10.0dB");
  EXPECT_EQ(GetVolumeText(0.1), "-20.0dB");
  EXPECT_EQ(GetVolumeText(1000.0), "+60.0dB");
  EXPECT_EQ(GetVolumeText(0.0000001), "-140.0dB");

  // Only 0dB has no sign.
  EXPECT_EQ(GetVolumeText(0.9999), "-0.00dB");
  EXPECT_EQ(GetVolumeText(1.0001), "+0.00dB");
}

TEST_F(TextContractTest, VolumesBelowTwoToTheMinus25AreInf) {
  EXPECT_EQ(GetVolumeText(0.0), "-inf dB");
  EXPECT_EQ(GetVolumeText(0.00000001), "-inf dB");
  EXPECT_EQ(GetVolumeText(0.000000029), "-inf dB");
  EXPECT_EQ(GetVolumeText(0.00000003), "-150.0dB");
}

TEST_F(TextContractTest, PansAreTruncatedPercents) {
  EXPECT_EQ(GetPanText(0.0), "center");
  EXPECT_EQ(GetPanText(-0.25), "25%L");
  EXPECT_EQ(GetPanText(1.0), "100%R");
  EXPECT_EQ(GetPanText(0.125), "12%R");
  EXPECT_EQ(GetPanText(0.999), "99%R");
  EXPECT_EQ(GetPanText(-0.015), "1%L");
}

TEST_F(TextContractTest, PansUnderOnePercentAreToATenth) {
  EXPECT_EQ(GetPanText(0.004), "0.4%R");
  EXPECT_EQ(GetPanText(-0.006), "0.6%L");
  EXPECT_EQ(GetPanText(0.0099), "1.0%R");

  // Unless that is 0, which isn't center.
  EXPECT_EQ(GetPanText(0.0004), "0%R");
  EXPECT_EQ(GetPanText(-0.0004), "0%L");
}

TEST_F(TextContractTest, TimeIsTruncatedToTheMillisecond) {
  EXPECT_EQ(GetPositionText(0.0, kFormatTime), "0:00.000");
  EXPECT_EQ(GetPositionText(3.5, kFormatTime), "0:03.500");
  EXPECT_EQ(GetPositionText(0.0016, kFormatTime), "0:00.001");
  EXPECT_EQ(GetPositionText(3599.9999, kFormatTime), "59:59.999");
  EXPECT_EQ(GetPositionText(3725.25, kFormatTime), "1:02:05.250");

  // Before the start, as the distance from it.
  EXPECT_EQ(GetPositionText(-65.5, kFormatTime), "-1:05.500");
  EXPECT_EQ(GetPositionText(-3725.25, kFormatTime), "-1:02:05.250");
  EXPECT_EQ(GetPositionText(-0.0016, kFormatTime), "-0:00.001");
}

TEST_F(TextContractTest, BeatsAreRoundedToTheHundredth) {
  // REAPER's default 120 BPM in 4/4.
  EXPECT_EQ(GetPositionText(0.0, kFormatBeats), "1.1.00");
  EXPECT_EQ(GetPositionText(3.5, kFormatBeats), "2.4.00");
  EXPECT_EQ(GetPositionText(0.004, kFormatBeats), "1.1.01");
  EXPECT_EQ(GetPositionText(1.999, kFormatBeats), "2.1.00");
  EXPECT_EQ(GetPositionText(3725.25, kFormatBeats), "1863.3.50");

  // Before the start, measures count down from 0.
  EXPECT_EQ(GetPositionText(-0.25, kFormatBeats), "0.4.50");
  EXPECT_EQ(GetPositionText(-0.004, kFormatBeats), "0.4.99");
  EXPECT_EQ(GetPositionText(-65.5, kFormatBeats), "-32.2.00");
}

TEST_F(TextContractTest, SamplesAreRounded) {
  // At 44100 samples a second.
  EXPECT_EQ(GetPositionText(0.0, kFormatSamples), "0");
  EXPECT_EQ(GetPositionText(3.5, kFormatSamples), "154350");
  EXPECT_EQ(GetPositionText(0.00004, kFormatSamples), "2");
  EXPECT_EQ(GetPositionText(-0.00004, kFormatSamples), "-2");
  EXPECT_EQ(GetPositionText(-65.5, kFormatSamples), "-2888550");
}

TEST_F(TextContractTest, FramesAreTruncated) {
  // At 30 frames a second.
  EXPECT_EQ(GetPositionText(0.0, kFormatFrames), "00:00:00:00");
  EXPECT_EQ(GetPositionText(3.5, kFormatFrames), "00:00:03:15");
  EXPECT_EQ(GetPositionText(0.0666, kFormatFrames), "00:00:00:01");
  EXPECT_EQ(GetPositionText(59.99, kFormatFrames), "00:00:59:29");
  EXPECT_EQ(GetPositionText(3725.25, kFormatFrames), "01:02:05:07");

  // Before the start, hours count down from 0.
  EXPECT_EQ(GetPositionText(-0.05, kFormatFrames), "-1:59:59:28");
  EXPECT_EQ(GetPositionText(-65.5, kFormatFrames), "-1:58:54:15");
  EXPECT_EQ(GetPositionText(-7300.0, kFormatFrames), "-3:58:20:00");
}

//------------------------------------------------------------------------------
// GUID text
//------------------------------------------------------------------------------

using GuidContractTest = ContractTest;

TEST_F(GuidContractTest, TextReadsBackAsTheSameGuid) {
  GUID guid = {};
  stringToGuid("{00000001-0002-0003-0405-060708090A0B}", &guid);
  EXPECT_EQ(guid.Data1, 1);
  EXPECT_EQ(guid.Data2, 2);
  EXPECT_EQ(guid.Data3, 3);
  EXPECT_EQ(guid.Data4[0], 4);
  EXPECT_EQ(guid.Data4[7], 11);
  EXPECT_EQ(GetGuidText(&guid), "{00000001-0002-0003-0405-060708090A0B}");
}

TEST_F(GuidContractTest, LowerCaseTextReadsAsTheSameGuid) {
  GUID guid = {};
  stringToGuid("{abcdef01-0002-0003-0405-060708090a0b}", &guid);
  EXPECT_EQ(GetGuidText(&guid), "{ABCDEF01-0002-0003-0405-060708090A0B}");
}

}  // namespace
}  // namespace jpr
