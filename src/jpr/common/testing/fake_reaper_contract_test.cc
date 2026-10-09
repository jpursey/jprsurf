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
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/guid.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/contract_test.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track_state.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

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
  EXPECT_EQ(GetTrackStateFlags(FindTrack("Soloed in place")),
            kTrackStateSolo | kTrackStateSoloInPlace);
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
  constexpr int kInPlace = kTrackStateSolo | kTrackStateSoloInPlace;
  EXPECT_EQ(Set(kTrackStateSolo, track, 1, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kInPlace);
  EXPECT_EQ(Set(kTrackStateSolo, track, -1, kPreventGroupingAndGanging), 0);
  EXPECT_EQ(GetTrackStateFlags(track), 0);
  EXPECT_EQ(Set(kTrackStateSolo, track, -1, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kInPlace);

  // 2 solos it not in place, and 4 in place.
  EXPECT_EQ(Set(kTrackStateSolo, track, 2, kPreventGroupingAndGanging), 1);
  EXPECT_EQ(GetTrackStateFlags(track), kTrackStateSolo);
  EXPECT_EQ(Set(kTrackStateSolo, track, 4, kPreventGroupingAndGanging), 2);
  EXPECT_EQ(GetTrackStateFlags(track), kInPlace);
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
  EXPECT_EQ(GetTrackStateFlags(b), kTrackStateSolo | kTrackStateSoloInPlace);
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
