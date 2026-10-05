// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/route_properties.h"

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

namespace jpr {
namespace {

// T1 sends to T2 and T3. The properties read the track cache, which the test
// refreshes itself, as a control surface would.
class RoutePropertiesTest : public ::testing::Test {
 protected:
  RoutePropertiesTest()
      : tracks_(project_.AddTracks(3)),
        to_t2_(project_.AddSend(tracks_[0], tracks_[1])),
        to_t3_(project_.AddSend(tracks_[0], tracks_[2])) {
    to_t2_->volume = 0.5;
    to_t2_->pan = -0.25;
    to_t2_->mute = true;
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
  FakeRoute* const to_t2_;
  FakeRoute* const to_t3_;
  TrackActions actions_{TrackFilter::kMcp};
  bool changed_ = false;
};

TEST_F(RoutePropertiesTest, ReadsASend) {
  RouteProperties send(&actions_, GetTrack(tracks_[0]), TrackRouteType::kSend,
                       0);
  EXPECT_TRUE(send.GetProperty(RouteProperties::kExists)->GetBool());
  EXPECT_EQ(send.GetProperty(RouteProperties::kVolume)->GetVolume(), 0.5);
  EXPECT_EQ(send.GetProperty(RouteProperties::kPan)->GetPan(), -0.25);
  EXPECT_TRUE(send.GetProperty(RouteProperties::kMute)->GetBool());
  EXPECT_EQ(send.GetProperty("route:other_track.name")->GetText(), "T2");
}

TEST_F(RoutePropertiesTest, ReadsAReceive) {
  RouteProperties receive(&actions_, GetTrack(tracks_[1]),
                          TrackRouteType::kReceive, 0);
  EXPECT_TRUE(receive.GetProperty(RouteProperties::kExists)->GetBool());
  EXPECT_EQ(receive.GetProperty(RouteProperties::kVolume)->GetVolume(), 0.5);
  EXPECT_EQ(receive.GetProperty(RouteProperties::kPan)->GetPan(), -0.25);
  EXPECT_TRUE(receive.GetProperty(RouteProperties::kMute)->GetBool());
  EXPECT_EQ(receive.GetProperty("route:other_track.name")->GetText(), "T1");
}

TEST_F(RoutePropertiesTest, RoutePastTheEndDoesntExist) {
  RouteProperties send(&actions_, GetTrack(tracks_[0]), TrackRouteType::kSend,
                       2);
  EXPECT_EQ(send.GetRoute(), nullptr);
  EXPECT_FALSE(send.GetProperty(RouteProperties::kExists)->GetBool());
  EXPECT_EQ(send.GetProperty(RouteProperties::kVolume)->GetVolume(), 0.0);
  EXPECT_EQ(send.GetProperty(RouteProperties::kPan)->GetPan(), 0.0);
  EXPECT_FALSE(send.GetProperty(RouteProperties::kMute)->GetBool());
  EXPECT_FALSE(send.GetProperty("route:other_track.exists")->GetBool());
}

TEST_F(RoutePropertiesTest, PropertiesAreCreatedOnce) {
  RouteProperties send(&actions_, GetTrack(tracks_[0]), TrackRouteType::kSend,
                       0);
  ViewProperty* volume = send.GetProperty(RouteProperties::kVolume);
  ASSERT_NE(volume, nullptr);
  EXPECT_EQ(volume->GetName(), RouteProperties::kVolume);
  EXPECT_EQ(send.GetProperty(RouteProperties::kVolume), volume);
  EXPECT_EQ(send.GetProperty("route:nothing"), nullptr);
  EXPECT_EQ(send.GetProperty("route:nothing.name"), nullptr);
  EXPECT_EQ(send.GetProperty("route:other_track.nothing"), nullptr);
}

TEST_F(RoutePropertiesTest, WritesSetTheRoute) {
  RouteProperties receive(&actions_, GetTrack(tracks_[2]),
                          TrackRouteType::kReceive, 0);
  receive.GetProperty(RouteProperties::kVolume)->SetVolume(0.25);
  receive.GetProperty(RouteProperties::kPan)->SetPan(0.5);
  receive.GetProperty(RouteProperties::kMute)->SetBool(true);

  EXPECT_EQ(to_t3_->volume, 0.25);
  EXPECT_EQ(to_t3_->pan, 0.5);
  EXPECT_TRUE(to_t3_->mute);
  EXPECT_EQ(to_t2_->volume, 0.5);
}

TEST_F(RoutePropertiesTest, RefreshedValuesNotify) {
  Track* track = GetTrack(tracks_[0]);
  RouteProperties send(&actions_, track, TrackRouteType::kSend, 1);
  ViewProperty* volume = send.GetProperty(RouteProperties::kVolume);
  volume->RegisterFlag(&changed_);

  to_t3_->volume = 0.75;
  EXPECT_EQ(volume->GetVolume(), 1.0);
  track->RefreshRoutes();
  EXPECT_TRUE(changed_);
  EXPECT_EQ(volume->GetVolume(), 0.75);
  volume->UnregisterFlag(&changed_);
}

TEST_F(RoutePropertiesTest, SettingTheRouteMovesEveryProperty) {
  RouteProperties route(&actions_, GetTrack(tracks_[0]), TrackRouteType::kSend,
                        0);
  ViewProperty* volume = route.GetProperty(RouteProperties::kVolume);
  ViewProperty* other_name = route.GetProperty("route:other_track.name");
  volume->RegisterFlag(&changed_);

  route.SetRoute(GetTrack(tracks_[0]), TrackRouteType::kSend, 1);
  EXPECT_TRUE(changed_);
  EXPECT_EQ(route.GetIndex(), 1);
  EXPECT_EQ(volume->GetVolume(), 1.0);
  EXPECT_EQ(other_name->GetText(), "T3");

  route.SetRoute(GetTrack(tracks_[2]), TrackRouteType::kReceive, 0);
  EXPECT_EQ(route.GetTrack(), GetTrack(tracks_[2]));
  EXPECT_EQ(route.GetType(), TrackRouteType::kReceive);
  EXPECT_EQ(other_name->GetText(), "T1");
  volume->UnregisterFlag(&changed_);
}

TEST_F(RoutePropertiesTest, ChangingTheRoutesNotifies) {
  RouteProperties send(&actions_, GetTrack(tracks_[0]), TrackRouteType::kSend,
                       2);
  ViewProperty* exists = send.GetProperty(RouteProperties::kExists);
  exists->RegisterFlag(&changed_);

  project_.AddSend(tracks_[0], tracks_[1]);
  TrackCache::Get().Refresh();
  EXPECT_TRUE(changed_);
  EXPECT_TRUE(exists->GetBool());
  EXPECT_EQ(send.GetProperty("route:other_track.name")->GetText(), "T2");

  changed_ = false;
  project_.DeleteTrack(tracks_[0]);
  TrackCache::Get().Refresh();
  EXPECT_TRUE(changed_);
  EXPECT_FALSE(exists->GetBool());
  exists->UnregisterFlag(&changed_);
}

}  // namespace
}  // namespace jpr
