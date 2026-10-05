// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_anchor_property.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "jpr/common/anchor.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"

namespace jpr {
namespace {

// The anchor is added to a view bound to "user:track", which a test points at
// a track.
class TrackAnchorPropertyTest : public SceneTest {
 protected:
  TrackAnchorPropertyTest()
      : tracks_(reaper_.GetProject().AddTracks(2)),
        reference_(scene_.AddTrackReference("user:track")),
        modifier_(scene_.AddModifierProperty("mod:anchor")) {
    View* root = scene_.GetRootView();
    root->Enable();
    View* view = root->AddChildView(
        "View", {.subject = View::ReferenceSubject{"user:track"}});
    view->Enable();
    anchor_ = view->AddUserProperty(std::make_unique<TrackAnchorProperty>(
        "user:anchor",
        TrackAnchorProperty::Config{.action = TrackBoolProperty::kMute,
                                    .modifier = modifier_}));
    AddSurface();
  }

  // Returns the track the action's anchor is held on, or null.
  Track* GetAnchorTrack(TrackBoolProperty action = TrackBoolProperty::kMute) {
    return scene_.GetTrackActions().GetAnchor(action).Get();
  }

  std::vector<FakeTrack*> tracks_;
  TrackReference* const reference_;
  const Modifiers modifier_;

  TrackAnchorProperty* anchor_ = nullptr;
};

TEST_F(TrackAnchorPropertyTest, HoldsTheAnchorOnItsViewsTrack) {
  reference_->Set(GetCachedTrack(tracks_[1]));
  anchor_->SetBool(true);
  EXPECT_EQ(GetAnchorTrack(), GetCachedTrack(tracks_[1]));
  EXPECT_TRUE(AreModifiersOn(modifier_));
  EXPECT_EQ(GetAnchorTrack(TrackBoolProperty::kSolo), nullptr);

  // It always reads as off.
  EXPECT_FALSE(anchor_->GetBool());

  anchor_->SetBool(false);
  EXPECT_EQ(GetAnchorTrack(), nullptr);
  EXPECT_FALSE(AreModifiersOn(modifier_));
}

TEST_F(TrackAnchorPropertyTest, AViewWithNoTrackHoldsNothing) {
  anchor_->SetBool(true);
  EXPECT_EQ(GetAnchorTrack(), nullptr);
  EXPECT_FALSE(AreModifiersOn(modifier_));
}

TEST_F(TrackAnchorPropertyTest, OutsideAViewItDoesNothing) {
  TrackAnchorProperty* anchor =
      scene_.AddUserProperty(std::make_unique<TrackAnchorProperty>(
          "user:scene_anchor",
          TrackAnchorProperty::Config{.action = TrackBoolProperty::kMute}));
  anchor->SetBool(true);
  EXPECT_EQ(GetAnchorTrack(), nullptr);
}

}  // namespace
}  // namespace jpr
