// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/route_reference.h"

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

// T1 sends to T2 and T3. The reference reads the track cache, which the test
// refreshes itself, as a control surface would.
class RouteReferenceTest : public ::testing::Test {
 protected:
  RouteReferenceTest() : tracks_(project_.AddTracks(3)) {
    project_.AddSend(tracks_[0], tracks_[1]);
    project_.AddSend(tracks_[0], tracks_[2]);
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
  RouteReference reference_{"", &actions_};
  bool changed_ = false;
};

TEST_F(RouteReferenceTest, RefersToNothingAtFirst) {
  EXPECT_EQ(reference_.GetKind(), SubjectKind::kRoute);
  EXPECT_EQ(reference_.GetRoute(), nullptr);
  EXPECT_FALSE(reference_.GetField("exists")->GetBool());
}

TEST_F(RouteReferenceTest, VersionChangesWithTheRoute) {
  int64_t version = reference_.GetVersion();
  reference_.Set(GetTrack(tracks_[0]), TrackRouteType::kSend, 0);
  EXPECT_NE(reference_.GetVersion(), version);
  ASSERT_NE(reference_.GetRoute(), nullptr);
  EXPECT_EQ(reference_.GetRoute()->other_track, GetTrack(tracks_[1]));

  version = reference_.GetVersion();
  reference_.Set(GetTrack(tracks_[0]), TrackRouteType::kSend, 0);
  EXPECT_EQ(reference_.GetVersion(), version);

  // A new index, track, or type is each a new route.
  reference_.Set(GetTrack(tracks_[0]), TrackRouteType::kSend, 1);
  EXPECT_NE(reference_.GetVersion(), version);
  version = reference_.GetVersion();
  reference_.Set(GetTrack(tracks_[1]), TrackRouteType::kSend, 1);
  EXPECT_NE(reference_.GetVersion(), version);
  version = reference_.GetVersion();
  reference_.Set(GetTrack(tracks_[1]), TrackRouteType::kReceive, 1);
  EXPECT_NE(reference_.GetVersion(), version);
}

TEST_F(RouteReferenceTest, FieldsFollowTheReference) {
  ViewProperty* exists = reference_.GetField("exists");
  ViewProperty* other_name = reference_.GetField("other_track.name");
  ASSERT_NE(other_name, nullptr);
  EXPECT_EQ(exists->GetName(), "route:exists");
  EXPECT_EQ(reference_.GetField("nothing"), nullptr);
  exists->RegisterFlag(&changed_);

  reference_.Set(GetTrack(tracks_[2]), TrackRouteType::kReceive, 0);
  EXPECT_TRUE(changed_);
  EXPECT_TRUE(exists->GetBool());
  EXPECT_EQ(other_name->GetText(), "T1");
  exists->UnregisterFlag(&changed_);
}

TEST_F(RouteReferenceTest, UpdateRefreshesTheOtherTracksWatchedFields) {
  reference_.Set(GetTrack(tracks_[0]), TrackRouteType::kSend, 0);
  ViewProperty* other_name = reference_.GetField("other_track.name");
  other_name->RegisterFlag(&changed_);

  tracks_[1]->name = "Bass";
  reference_.Update();
  EXPECT_TRUE(changed_);
  EXPECT_EQ(other_name->GetText(), "Bass");
  other_name->UnregisterFlag(&changed_);
}

}  // namespace
}  // namespace jpr
