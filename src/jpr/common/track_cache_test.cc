// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track_cache.h"

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/guid.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

class TrackCacheTest : public ::testing::Test {
 protected:
  TrackCache& Cache() { return TrackCache::Get(); }

  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
};

TEST_F(TrackCacheTest, ListsTracksInOrderWithTheirFolders) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  FakeTrack* kick = project_.AddTrack("Kick", drums);
  FakeTrack* snare = project_.AddTrack("Snare", drums);
  Cache().Refresh();

  Track* master = Cache().GetMasterTrack();
  ASSERT_NE(master, nullptr);
  EXPECT_EQ(master->GetTrackId(), ToMediaTrack(project_.GetMasterTrack()));
  EXPECT_THAT(Cache().GetTracks(),
              ElementsAre(GetCachedTrack(drums), GetCachedTrack(kick),
                          GetCachedTrack(snare), GetCachedTrack(bass)));
  EXPECT_THAT(master->GetChildTracks(),
              ElementsAre(GetCachedTrack(drums), GetCachedTrack(bass)));
  EXPECT_THAT(GetCachedTrack(drums)->GetChildTracks(),
              ElementsAre(GetCachedTrack(kick), GetCachedTrack(snare)));
  EXPECT_EQ(GetCachedTrack(kick)->GetParentTrack(), GetCachedTrack(drums));
  EXPECT_EQ(GetCachedTrack(bass)->GetParentTrack(), master);
  EXPECT_EQ(GetCachedTrack(snare)->GetIndex(TrackFilter::kAll), 1);
  EXPECT_EQ(GetCachedTrack(bass)->GetGlobalIndex(TrackFilter::kAll), 3);
}

TEST_F(TrackCacheTest, FiltersHiddenTracks) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  Cache().Refresh();
  const int64_t version = Cache().GetTrackListVersion();
  EXPECT_FALSE(Cache().RefreshFilters());

  drums->show_in_mixer = false;
  EXPECT_TRUE(Cache().RefreshFilters());
  EXPECT_NE(Cache().GetTrackListVersion(), version);
  EXPECT_FALSE(GetCachedTrack(drums)->IsIncluded(TrackFilter::kMcp));
  EXPECT_TRUE(GetCachedTrack(drums)->IsIncluded(TrackFilter::kTcp));
  EXPECT_EQ(GetCachedTrack(drums)->GetIndex(TrackFilter::kMcp), std::nullopt);
  EXPECT_EQ(GetCachedTrack(bass)->GetIndex(TrackFilter::kMcp), 0);
  EXPECT_EQ(Cache().GetMasterTrack()->GetChildTrackCount(TrackFilter::kMcp), 1);
  EXPECT_EQ(Cache().GetMasterTrack()->GetChildTrackCount(TrackFilter::kTcp), 2);
}

TEST_F(TrackCacheTest, KeepsATrackByGuidWhileItIsDeleted) {
  FakeTrack* drums = project_.AddTrack("Drums");
  const Guid guid(project_.GetGuid(drums));
  Cache().Refresh();
  Track* track = Cache().GetTrack(guid);
  ASSERT_NE(track, nullptr);

  project_.DeleteTrack(drums);
  Cache().Refresh();
  EXPECT_EQ(Cache().GetTrack(guid), track);
  EXPECT_FALSE(track->Exists());
  EXPECT_THAT(Cache().GetTracks(), IsEmpty());

  // Undo brings the track back with the same GUID.
  FakeTrack* restored = project_.RestoreTrack(drums);
  Cache().Refresh();
  EXPECT_EQ(Cache().GetTrack(guid), track);
  EXPECT_TRUE(track->Exists());
  EXPECT_EQ(track->GetTrackId(), ToMediaTrack(restored));
}

TEST_F(TrackCacheTest, DeletedFolderTracksMoveUp) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* kick = project_.AddTrack("Kick", drums);
  project_.DeleteTrack(drums);
  Cache().Refresh();
  EXPECT_EQ(GetCachedTrack(kick)->GetParentTrack(), Cache().GetMasterTrack());
}

TEST_F(TrackCacheTest, ForgetsTheLastTouchedTrackWhenItIsDeleted) {
  FakeTrack* drums = project_.AddTrack("Drums");
  Cache().Refresh();
  Cache().SetLastTouchedTrack(GetCachedTrack(drums));

  project_.DeleteTrack(drums);
  Cache().Refresh();
  EXPECT_EQ(Cache().GetLastTouchedTrack(), nullptr);
}

TEST_F(TrackCacheTest, TouchedTracksAreThoseWithATouch) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  Cache().Refresh();
  Track* drums_track = GetCachedTrack(drums);
  Track* bass_track = GetCachedTrack(bass);
  EXPECT_THAT(Cache().GetTouchedTracks(), IsEmpty());

  // A track is listed once, whichever of its types are touched.
  TrackTouchHandle volume = drums_track->RegisterTouch(TrackTouchType::kVolume);
  TrackTouchHandle pan = drums_track->RegisterTouch(TrackTouchType::kPan);
  TrackTouchHandle bass_pan = bass_track->RegisterTouch(TrackTouchType::kPan);
  EXPECT_THAT(Cache().GetTouchedTracks(),
              UnorderedElementsAre(drums_track, bass_track));

  volume = {};
  EXPECT_THAT(Cache().GetTouchedTracks(),
              UnorderedElementsAre(drums_track, bass_track));
  pan = {};
  EXPECT_THAT(Cache().GetTouchedTracks(), ElementsAre(bass_track));

  // A deleted track stays touched, and listed, until its touch goes.
  project_.DeleteTrack(bass);
  Cache().Refresh();
  EXPECT_THAT(Cache().GetTouchedTracks(), ElementsAre(bass_track));
  bass_pan = {};
  EXPECT_THAT(Cache().GetTouchedTracks(), IsEmpty());
}

TEST_F(TrackCacheTest, Selection) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  FakeTrack* keys = project_.AddTrack("Keys");
  Cache().Refresh();
  EXPECT_EQ(Cache().GetOnlySelectedTrack(), nullptr);

  bass->selected = true;
  project_.GetMasterTrack()->selected = true;
  EXPECT_EQ(Cache().GetOnlySelectedTrack(), GetCachedTrack(bass));
  EXPECT_THAT(Cache().GetSelectedTracks(), ElementsAre(GetCachedTrack(bass)));

  keys->selected = true;
  EXPECT_EQ(Cache().GetOnlySelectedTrack(), nullptr);
  EXPECT_THAT(Cache().GetSelectedTracks(),
              ElementsAre(GetCachedTrack(bass), GetCachedTrack(keys)));
  EXPECT_FALSE(drums->selected);
}

TEST_F(TrackCacheTest, SelectedAutomationModes) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  drums->selected = true;
  drums->auto_mode = static_cast<int>(AutoMode::kTouch);
  bass->auto_mode = static_cast<int>(AutoMode::kWrite);
  Cache().Refresh();
  EXPECT_TRUE(Cache().HasSelectedAutoMode(AutoMode::kTouch));
  EXPECT_FALSE(Cache().HasMixedSelectedAutoModes());

  // The modes are cached until the selection may have changed.
  bass->selected = true;
  EXPECT_FALSE(Cache().HasMixedSelectedAutoModes());
  Cache().OnSelectionChanged();
  EXPECT_TRUE(Cache().HasMixedSelectedAutoModes());

  // The master is included.
  bass->selected = false;
  project_.GetMasterTrack()->selected = true;
  project_.GetMasterTrack()->auto_mode = static_cast<int>(AutoMode::kLatch);
  Cache().OnAutoModeChanged();
  EXPECT_TRUE(Cache().HasSelectedAutoMode(AutoMode::kLatch));
  EXPECT_FALSE(Cache().HasSelectedAutoMode(AutoMode::kWrite));
}

TEST_F(TrackCacheTest, NothingSelectedHasNoSelectedAutomationModes) {
  // Every track has a mode of its own, the master's being trim/read.
  project_.AddTrack("Drums")->auto_mode = static_cast<int>(AutoMode::kTouch);
  Cache().Refresh();
  for (int mode = 0; mode < kAutoModeCount; ++mode) {
    EXPECT_FALSE(Cache().HasSelectedAutoMode(static_cast<AutoMode>(mode)))
        << mode;
  }
  EXPECT_FALSE(Cache().HasMixedSelectedAutoModes());
}

//------------------------------------------------------------------------------
// GetRecArmGroup
//------------------------------------------------------------------------------

// Returns the names of `tracks`.
std::vector<std::string> GetNames(const std::vector<Track*>& tracks) {
  std::vector<std::string> names;
  for (const Track* track : tracks) {
    names.emplace_back(track->GetName());
  }
  return names;
}

TEST_F(TrackCacheTest, RecArmGroupOfAnUngroupedTrackIsTheTrack) {
  FakeTrack* drums = project_.AddTrack("Drums");
  project_.AddTrack("Bass")->selected = true;
  Cache().Refresh();
  EXPECT_THAT(GetNames(Cache().GetRecArmGroup(GetCachedTrack(drums))),
              ElementsAre("Drums"));
}

TEST_F(TrackCacheTest, RecArmGroupOfASelectedTrackHasEverySelectedTrack) {
  FakeTrack* drums = project_.AddTrack("Drums");
  project_.AddTrack("Bass")->selected = true;
  project_.AddTrack("Keys");
  drums->selected = true;
  Cache().Refresh();
  EXPECT_THAT(GetNames(Cache().GetRecArmGroup(GetCachedTrack(drums))),
              ElementsAre("Drums", "Bass"));
}

// A group reaches its followers from a track that leads it, and on through
// each that leads a group of its own, but not through one that doesn't.
TEST_F(TrackCacheTest, RecArmGroupReachesFollowersOfGroupsTheTrackLeads) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* kick = project_.AddTrack("Kick");
  FakeTrack* snare = project_.AddTrack("Snare");
  FakeTrack* bass = project_.AddTrack("Bass");
  FakeTrack* keys = project_.AddTrack("Keys");
  drums->rec_arm_groups.lead = GroupMask(1);
  kick->rec_arm_groups = {.lead = GroupMask(40), .follow = GroupMask(1)};
  snare->rec_arm_groups.follow = GroupMask(40);
  bass->rec_arm_groups = {.lead = GroupMask(2), .follow = GroupMask(1)};
  keys->rec_arm_groups.follow = GroupMask(3);
  Cache().Refresh();
  EXPECT_THAT(GetNames(Cache().GetRecArmGroup(GetCachedTrack(drums))),
              ElementsAre("Drums", "Kick", "Bass", "Snare"));

  // A follower that doesn't lead reaches no one.
  EXPECT_THAT(GetNames(Cache().GetRecArmGroup(GetCachedTrack(snare))),
              ElementsAre("Snare"));
}

TEST_F(TrackCacheTest, RecArmGroupReachesTheGroupsOfTheSelectedTracks) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  FakeTrack* keys = project_.AddTrack("Keys");
  JoinGroup(bass, 1);
  JoinGroup(keys, 1);
  drums->selected = true;
  bass->selected = true;
  Cache().Refresh();
  EXPECT_THAT(GetNames(Cache().GetRecArmGroup(GetCachedTrack(drums))),
              ElementsAre("Drums", "Bass", "Keys"));
}

TEST_F(TrackCacheTest, RecArmGroupOfADeletedTrackIsEmpty) {
  FakeTrack* fake = project_.AddTrack("Drums");
  Cache().Refresh();
  Track* track = GetCachedTrack(fake);
  project_.DeleteTrack(fake);
  Cache().Refresh();
  EXPECT_THAT(Cache().GetRecArmGroup(track), IsEmpty());
}

}  // namespace
}  // namespace jpr
