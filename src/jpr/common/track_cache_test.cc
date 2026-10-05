// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track_cache.h"

#include <optional>

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
  EXPECT_FALSE(Cache().RefreshVisibility());

  drums->show_in_mixer = false;
  EXPECT_TRUE(Cache().RefreshVisibility());
  EXPECT_NE(Cache().GetTrackListVersion(), version);
  EXPECT_FALSE(GetCachedTrack(drums)->IsVisible(TrackFilter::kMcp));
  EXPECT_TRUE(GetCachedTrack(drums)->IsVisible(TrackFilter::kTcp));
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

}  // namespace
}  // namespace jpr
