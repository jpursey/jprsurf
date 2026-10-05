// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_reference.h"

#include <cstdint>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {
namespace {

// The reference reads the track cache, which the test refreshes itself, as a
// control surface would. The scene's own references, and adding references to
// the scene with their rules, are tested with the scene (see scene_test.cc).
class TrackReferenceTest : public ::testing::Test {
 protected:
  TrackReferenceTest() : tracks_(project_.AddTracks(3)) {
    TrackCache::Get().Refresh();
  }

  // Returns the cache's track for the fake's track.
  static Track* GetTrack(FakeTrack* track) {
    return TrackCache::Get().GetTrack(ToMediaTrack(track));
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> tracks_;
  TrackActions actions_{TrackFilter::kMcp};
  TrackReference reference_{"user:track", &actions_};
  bool changed_ = false;
};

TEST_F(TrackReferenceTest, RefersToNothingAtFirst) {
  EXPECT_EQ(reference_.GetName(), "user:track");
  EXPECT_EQ(reference_.GetKind(), SubjectKind::kTrack);
  EXPECT_EQ(reference_.GetTrack(), nullptr);
  EXPECT_FALSE(reference_.GetField("exists")->GetBool());
}

TEST_F(TrackReferenceTest, VersionChangesWithTheTrack) {
  const int64_t first = reference_.GetVersion();
  reference_.Set(GetTrack(tracks_[0]));
  const int64_t second = reference_.GetVersion();
  EXPECT_NE(second, first);
  EXPECT_EQ(reference_.GetTrack(), GetTrack(tracks_[0]));

  reference_.Set(GetTrack(tracks_[0]));
  EXPECT_EQ(reference_.GetVersion(), second);

  reference_.Set(nullptr);
  EXPECT_NE(reference_.GetVersion(), second);
  EXPECT_EQ(reference_.GetTrack(), nullptr);
}

TEST_F(TrackReferenceTest, FieldsFollowTheReference) {
  ViewProperty* name = reference_.GetField("name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->GetName(), "track:name");
  EXPECT_EQ(reference_.GetField("nothing"), nullptr);
  name->RegisterFlag(&changed_);

  reference_.Set(GetTrack(tracks_[1]));
  EXPECT_TRUE(changed_);
  EXPECT_EQ(name->GetText(), "T2");
  name->UnregisterFlag(&changed_);
}

TEST_F(TrackReferenceTest, ADeletedTrackIsNothing) {
  reference_.Set(GetTrack(tracks_[0]));
  project_.DeleteTrack(tracks_[0]);
  TrackCache::Get().Refresh();
  reference_.Update();
  EXPECT_EQ(reference_.GetTrack(), nullptr);
}

TEST_F(TrackReferenceTest, UpdateRefreshesOnlyWatchedFields) {
  reference_.Set(GetTrack(tracks_[0]));
  ViewProperty* mute = reference_.GetField("mute");
  tracks_[0]->mute = true;
  reference_.Update();
  EXPECT_FALSE(mute->GetBool());

  mute->RegisterFlag(&changed_);
  reference_.Update();
  EXPECT_TRUE(changed_);
  EXPECT_TRUE(mute->GetBool());
  mute->UnregisterFlag(&changed_);
}

TEST_F(TrackReferenceTest, UpdateRefreshesTheMeterOnlyWhileItIsWatched) {
  reference_.Set(GetTrack(tracks_[0]));
  ViewProperty* mute = reference_.GetField("mute");
  ViewProperty* meter = reference_.GetField("meter");
  mute->RegisterFlag(&changed_);
  tracks_[0]->peak = {0.5, 0.5};
  reference_.Update();
  EXPECT_EQ(meter->GetNormalized(), 0.0);

  bool meter_changed = false;
  meter->RegisterFlag(&meter_changed);
  reference_.Update();
  EXPECT_TRUE(meter_changed);
  EXPECT_EQ(meter->GetNormalized(), 0.5);
  meter->UnregisterFlag(&meter_changed);
  mute->UnregisterFlag(&changed_);
}

TEST_F(TrackReferenceTest, FallsBackToTheFallbacksTrack) {
  TrackReference fallback("user:fallback", &actions_);
  TrackReference reference("user:reference", &actions_, &fallback);
  fallback.Set(GetTrack(tracks_[2]));

  reference.Set(nullptr);
  EXPECT_EQ(reference.GetTrack(), GetTrack(tracks_[2]));
  reference.Set(GetTrack(tracks_[0]));
  EXPECT_EQ(reference.GetTrack(), GetTrack(tracks_[0]));
}

TEST_F(TrackReferenceTest, FollowsOnlyTracksOnTheSurface) {
  TrackReference followed("user:followed", &actions_);
  TrackReference reference("user:reference", &actions_, nullptr, &followed);
  followed.Set(GetTrack(tracks_[1]));
  reference.Update();
  EXPECT_EQ(reference.GetTrack(), GetTrack(tracks_[1]));

  followed.Set(TrackCache::Get().GetMasterTrack());
  reference.Update();
  EXPECT_EQ(reference.GetTrack(), GetTrack(tracks_[1]));

  followed.Set(nullptr);
  reference.Update();
  EXPECT_EQ(reference.GetTrack(), GetTrack(tracks_[1]));
}

}  // namespace
}  // namespace jpr
