// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_pick_property.h"

#include <memory>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"

namespace jpr {
namespace {

// A pick sets "user:picked" to the track of "user:source", or of its view.
class TrackPickPropertyTest : public ::testing::Test {
 protected:
  TrackPickPropertyTest()
      : tracks_(reaper_.GetProject().AddTracks(3)),
        picked_(scene_.AddTrackReference("user:picked")),
        source_(scene_.AddTrackReference("user:source")) {
    TrackCache::Get().Refresh();
    picked_->Set(GetCachedTrack(tracks_[0]));
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  Scene scene_{"Scene"};
  std::vector<FakeTrack*> tracks_;
  TrackReference* const picked_;
  TrackReference* const source_;
};

TEST_F(TrackPickPropertyTest, PicksTheSourcesTrack) {
  TrackPickProperty pick("user:pick", *picked_, source_);
  source_->Set(GetCachedTrack(tracks_[1]));
  pick.RunAction();
  EXPECT_EQ(picked_->GetTrack(), GetCachedTrack(tracks_[1]));
}

TEST_F(TrackPickPropertyTest, ASourceWithNoTrackPicksNothing) {
  TrackPickProperty pick("user:pick", *picked_, source_);
  pick.RunAction();
  EXPECT_EQ(picked_->GetTrack(), GetCachedTrack(tracks_[0]));
}

TEST_F(TrackPickPropertyTest, WithoutASourcePicksItsViewsTrack) {
  source_->Set(GetCachedTrack(tracks_[2]));
  View* view = scene_.GetRootView()->AddChildView(
      "View", {.subject = View::ReferenceSubject{"user:source"}});
  TrackPickProperty* pick = view->AddUserProperty(
      std::make_unique<TrackPickProperty>("user:pick", *picked_));
  pick->RunAction();
  EXPECT_EQ(picked_->GetTrack(), GetCachedTrack(tracks_[2]));
}

TEST_F(TrackPickPropertyTest, WithoutASourceOrViewPicksNothing) {
  TrackPickProperty pick("user:pick", *picked_);
  pick.RunAction();
  EXPECT_EQ(picked_->GetTrack(), GetCachedTrack(tracks_[0]));
}

}  // namespace
}  // namespace jpr
