// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// What the functions on the API list do to REAPER's state, which the fake
// REAPER models. The tests that need the fake are in fake_reaper_test.cc.

#include <array>
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
