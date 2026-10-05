// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/anchor.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {
namespace {

using ::testing::SizeIs;

// The project is T1 and T2. The scene has a track reference, "user:track", and
// a toggle, "user:flag". The root view starts disabled.
class ViewTest : public SceneTest {
 protected:
  ViewTest()
      : tracks_(reaper_.GetProject().AddTracks(2)),
        reference_(scene_.AddTrackReference("user:track")),
        flag_(scene_.AddUserProperty(
            std::make_unique<ToggleValueProperty>("user:flag"))),
        root_(scene_.GetRootView()) {}

  // Adds a child view of the root view, and enables it.
  View* AddEnabledView(std::string_view name, const View::Config& config = {}) {
    View* view = root_->AddChildView(name, config);
    EXPECT_NE(view, nullptr) << name;
    if (view != nullptr) {
      view->Enable();
    }
    return view;
  }

  std::vector<FakeTrack*> tracks_;
  TrackReference* const reference_;
  ToggleValueProperty* const flag_;
  View* const root_;

  Anchor<Track> anchor_;
  Anchor<Track> other_anchor_;
};

//==============================================================================
// Activation
//==============================================================================

TEST_F(ViewTest, ActiveWhileEnabledAndItsParentIsActive) {
  EXPECT_FALSE(root_->AddChildView("Disabled")->IsEnabled());
  View* view = AddEnabledView("View");
  AddSurface();
  EXPECT_FALSE(view->IsActive());

  root_->Enable();
  EXPECT_TRUE(root_->IsActive());
  EXPECT_TRUE(view->IsActive());

  view->Disable();
  EXPECT_FALSE(view->IsActive());
  EXPECT_TRUE(root_->IsActive());

  view->Enable();
  root_->Disable();
  EXPECT_TRUE(view->IsEnabled());
  EXPECT_FALSE(view->IsActive());
}

TEST_F(ViewTest, AConditionIsReadWhenItsParentActivates) {
  View* view = AddEnabledView(
      "View", {.condition = ViewCondition::Config{.property = "user:flag"}});
  AddSurface();

  flag_->SetBool(true);
  root_->Enable();
  EXPECT_TRUE(view->IsActive());

  root_->Disable();
  flag_->SetBool(false);
  root_->Enable();
  EXPECT_FALSE(view->IsActive());
}

TEST_F(ViewTest, AConditionComparesWithItsValue) {
  View* view =
      AddEnabledView("View", {.condition = ViewCondition::Config{
                                  .property = "user:flag", .value = false}});
  root_->Enable();
  AddSurface();
  EXPECT_TRUE(view->IsActive());

  flag_->SetBool(true);
  surface_->Run();
  EXPECT_FALSE(view->IsActive());
}

TEST_F(ViewTest, AConditionMayUseTheViewsSubject) {
  tracks_[0]->mute = true;
  View* view = AddEnabledView(
      "View", {.condition = ViewCondition::Config{.property = std::string(
                                                      TrackProperties::kMute)},
               .subject = View::ReferenceSubject{"user:track"}});
  AddSurface();
  reference_->Set(GetCachedTrack(tracks_[0]));
  root_->Enable();
  EXPECT_TRUE(view->IsActive());

  reference_->Set(GetCachedTrack(tracks_[1]));
  surface_->Run();
  EXPECT_FALSE(view->IsActive());
}

TEST_F(ViewTest, AConditionsPropertyMustExist) {
  EXPECT_EQ(
      root_->AddChildView(
          "View",
          {.condition = ViewCondition::Config{.property = "user:nothing"}}),
      nullptr);

  // The root view has no subject, so no track: properties.
  EXPECT_EQ(
      root_->AddChildView(
          "View",
          {.condition = ViewCondition::Config{.property = std::string(
                                                  TrackProperties::kMute)}}),
      nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(2));
}

//==============================================================================
// Hierarchy
//==============================================================================

TEST_F(ViewTest, ChildViewsAreFoundByAName) {
  View* view = root_->AddChildView("View");
  ASSERT_NE(view, nullptr);
  View* child = view->AddChildView("View");
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(root_->GetChildView("View"), view);
  EXPECT_EQ(view->GetChildView("View"), child);
  EXPECT_EQ(child->GetParentView(), view);
  EXPECT_EQ(root_->GetChildView("Nothing"), nullptr);

  // A name is only used once by each parent.
  EXPECT_EQ(root_->AddChildView("View"), nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(1));
}

//==============================================================================
// Subjects
//==============================================================================

TEST_F(ViewTest, ChildViewsShareTheirParentsSubject) {
  AddSurface();
  View* view = root_->AddChildView(
      "View", {.subject = View::ReferenceSubject{"user:track"}});
  ASSERT_NE(view, nullptr);
  View* child = view->AddChildView("Child");
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(root_->GetSubject(), nullptr);
  EXPECT_EQ(view->GetSubject(), reference_);
  EXPECT_EQ(child->GetSubject(), reference_);

  // With no track, the view's track is the stub track.
  Track* stub = TrackCache::Get().GetStubTrack();
  EXPECT_EQ(root_->GetTrack(), stub);
  EXPECT_EQ(view->GetTrack(), stub);

  reference_->Set(GetCachedTrack(tracks_[1]));
  EXPECT_EQ(view->GetTrack(), GetCachedTrack(tracks_[1]));
  EXPECT_EQ(child->GetTrack(), GetCachedTrack(tracks_[1]));
}

TEST_F(ViewTest, TrackPropertiesAreTheSubjectsFields) {
  View* view = root_->AddChildView(
      "View", {.subject = View::ReferenceSubject{"user:track"}});
  ASSERT_NE(view, nullptr);
  ViewProperty* mute = scene_.GetProperty("user:track.mute");
  ASSERT_NE(mute, nullptr);
  EXPECT_EQ(view->GetProperty(TrackProperties::kMute), mute);
  EXPECT_EQ(view->GetProperty(RouteProperties::kVolume), nullptr);
  EXPECT_EQ(root_->GetProperty(TrackProperties::kMute), nullptr);
}

TEST_F(ViewTest, ASubjectMustExistAndSuitTheList) {
  EXPECT_EQ(root_->AddChildView(
                "View", {.subject = View::ReferenceSubject{"user:nothing"}}),
            nullptr);
  EXPECT_EQ(root_->AddChildView("View", {.subject = View::ListItemSubject{}}),
            nullptr);
  EXPECT_EQ(
      root_->AddChildView(
          "View", {.list = View::ListConfig{.items = View::ChildTracks{}}}),
      nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(3));
}

//==============================================================================
// User properties
//==============================================================================

TEST_F(ViewTest, UserPropertiesAreSeenFromTheirViewAndBelow) {
  View* view = root_->AddChildView("View");
  View* child = view->AddChildView("Child");
  View* sibling = root_->AddChildView("Sibling");
  ToggleValueProperty* local = view->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:local"));
  ASSERT_NE(local, nullptr);

  EXPECT_EQ(view->GetProperty("user:local"), local);
  EXPECT_EQ(child->GetProperty("user:local"), local);
  EXPECT_EQ(root_->GetProperty("user:local"), nullptr);
  EXPECT_EQ(sibling->GetProperty("user:local"), nullptr);
  EXPECT_EQ(scene_.GetProperty("user:local"), nullptr);
  EXPECT_EQ(child->GetProperty("user:flag"), flag_);
}

TEST_F(ViewTest, SiblingsHaveUserPropertiesOfTheirOwn) {
  ToggleValueProperty* first = root_->AddChildView("First")->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:local"));
  ToggleValueProperty* second = root_->AddChildView("Second")->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:local"));
  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);
}

TEST_F(ViewTest, UserPropertiesNeedANewUserName) {
  View* view = root_->AddChildView("View");
  View* child = view->AddChildView("Child");
  ASSERT_NE(view->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:local")),
            nullptr);

  // Used by an ancestor, or a descendant.
  EXPECT_EQ(child->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:local")),
            nullptr);
  EXPECT_EQ(root_->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:local")),
            nullptr);

  // The scene's rules apply too (see scene_test.cc).
  EXPECT_EQ(
      view->AddUserProperty(std::make_unique<ToggleValueProperty>("user:flag")),
      nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(3));
}

//==============================================================================
// Anchor
//==============================================================================

TEST_F(ViewTest, TheAnchorIsReleasedWhenTheSubjectChanges) {
  View* view =
      AddEnabledView("View", {.subject = View::ReferenceSubject{"user:track"}});
  root_->Enable();
  AddSurface();
  reference_->Set(GetCachedTrack(tracks_[0]));
  surface_->Run();

  view->SetAnchor(anchor_.Hold(GetCachedTrack(tracks_[0])));
  surface_->Run();
  EXPECT_TRUE(anchor_.IsHeld());

  reference_->Set(GetCachedTrack(tracks_[1]));
  surface_->Run();
  EXPECT_FALSE(anchor_.IsHeld());
}

TEST_F(ViewTest, TheAnchorIsReleasedWhenTheViewDeactivates) {
  View* view = AddEnabledView(
      "View", {.condition = ViewCondition::Config{.property = "user:flag"}});
  flag_->SetBool(true);
  root_->Enable();
  AddSurface();

  view->SetAnchor(anchor_.Hold(GetCachedTrack(tracks_[0])));
  EXPECT_TRUE(anchor_.IsHeld());
  flag_->SetBool(false);
  surface_->Run();
  EXPECT_FALSE(anchor_.IsHeld());

  // An inactive view releases a hold right away.
  view->SetAnchor(anchor_.Hold(GetCachedTrack(tracks_[0])));
  EXPECT_FALSE(anchor_.IsHeld());
}

TEST_F(ViewTest, AViewHoldsOneAnchor) {
  View* view = AddEnabledView("View");
  root_->Enable();
  AddSurface();

  view->SetAnchor(anchor_.Hold(GetCachedTrack(tracks_[0])));
  view->SetAnchor(other_anchor_.Hold(GetCachedTrack(tracks_[1])));
  EXPECT_FALSE(anchor_.IsHeld());
  EXPECT_EQ(other_anchor_.Get(), GetCachedTrack(tracks_[1]));

  // An empty hold (the anchor is already held) is ignored.
  view->SetAnchor(other_anchor_.Hold(GetCachedTrack(tracks_[0])));
  EXPECT_EQ(other_anchor_.Get(), GetCachedTrack(tracks_[1]));

  // Only the anchor the view holds is released.
  view->ReleaseAnchor(&anchor_);
  EXPECT_TRUE(other_anchor_.IsHeld());
  view->ReleaseAnchor(&other_anchor_);
  EXPECT_FALSE(other_anchor_.IsHeld());
}

//==============================================================================
// Mappings
//==============================================================================

TEST_F(ViewTest, MappingsNeedTheirPropertiesAndControl) {
  device_->AddButton("Button");
  const std::string button = GetControlName("Button");
  View* view = root_->AddChildView("View");
  EXPECT_TRUE(
      view->AddMapping(ViewMapping::kWriteControl, "user:flag", button));

  EXPECT_FALSE(
      view->AddMapping(ViewMapping::kWriteControl, "user:nothing", button));
  EXPECT_FALSE(view->AddMapping(ViewMapping::kWriteControl, "user:flag",
                                GetControlName("Nothing")));
  EXPECT_FALSE(view->AddMapping(
      ViewMapping::kWriteControl, "user:flag", button,
      {.write = {.mode_overrides = {{.property = "user:nothing"}}}}));
  EXPECT_FALSE(view->AddMapping(
      ViewMapping::kWriteControl, "user:flag", button,
      {.condition = ViewCondition::Config{.property = "user:nothing"}}));
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(4));
}

TEST_F(ViewTest, OnlyActiveViewsWriteTheirControls) {
  FakeDevice::FakeControl button = device_->AddButton("Button");
  View* view = root_->AddChildView("View");
  ASSERT_TRUE(view->AddMapping(ViewMapping::kWriteControl, "user:flag",
                               GetControlName("Button")));
  root_->Enable();
  flag_->SetBool(true);
  AddSurface();
  EXPECT_EQ(button.dvalue_output->GetValue(), 0);

  view->Enable();
  RunUntilShown();
  EXPECT_EQ(button.dvalue_output->GetValue(), 1);
}

}  // namespace
}  // namespace jpr
