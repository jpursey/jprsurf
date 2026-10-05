// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/state_properties.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using ::testing::SizeIs;

using Type = ViewProperty::Type;

class SceneReferenceTest : public SceneTest {
 protected:
  SceneReferenceTest() : tracks_(reaper_.GetProject().AddTracks(3)) {}

  // Returns the track the reference refers to, or null if it refers to
  // nothing.
  MediaTrack* GetTrackOf(std::string_view name) {
    const TrackReference* reference = scene_.GetTrackReference(name);
    EXPECT_NE(reference, nullptr) << name;
    if (reference == nullptr || reference->GetTrack() == nullptr) {
      return nullptr;
    }
    return reference->GetTrack()->GetTrackId();
  }

  std::vector<FakeTrack*> tracks_;
};

//==============================================================================
// Controls
//==============================================================================

TEST_F(SceneTest, FindsControlsByDeviceAndName) {
  FakeDevice::FakeControl play = device_->AddButton("Play");

  EXPECT_EQ(scene_.GetControl(GetControlName("Play")), play.control);
  EXPECT_EQ(scene_.GetControl(GetControlName("Stop")), nullptr);
  EXPECT_EQ(scene_.GetControl("Other/Play"), nullptr);
  EXPECT_EQ(scene_.GetControl("Play"), nullptr);
}

//==============================================================================
// Properties
//==============================================================================

TEST_F(SceneTest, CommandPropertiesAreCreatedOnce) {
  ViewProperty* undo = scene_.GetProperty(kCmdUndo);
  ASSERT_NE(undo, nullptr);
  EXPECT_EQ(undo->GetName(), kCmdUndo);
  EXPECT_EQ(scene_.GetProperty(kCmdUndo), undo);
  EXPECT_EQ(scene_.GetProperty("cmd:99999"), nullptr);
}

TEST_F(SceneTest, StatePropertiesAreCreatedOnce) {
  ViewProperty* can_redo = scene_.GetProperty(kStateCanRedo);
  ASSERT_NE(can_redo, nullptr);
  EXPECT_EQ(can_redo->GetName(), kStateCanRedo);
  EXPECT_EQ(scene_.GetProperty(kStateCanRedo), can_redo);
  EXPECT_EQ(scene_.GetProperty("state:nothing"), nullptr);
}

TEST_F(SceneTest, ViewNamespacesArentGlobal) {
  EXPECT_EQ(scene_.GetProperty(TrackProperties::kMute), nullptr);
  EXPECT_EQ(scene_.GetProperty(View::kChildInc), nullptr);
}

TEST_F(SceneTest, ModifierPropertiesAreBuiltIn) {
  for (std::string_view name :
       {ModifierProperty::kShift, ModifierProperty::kCtrl,
        ModifierProperty::kAlt, ModifierProperty::kOpt}) {
    ViewProperty* modifier = scene_.GetProperty(name);
    ASSERT_NE(modifier, nullptr) << name;
    EXPECT_EQ(modifier->GetType(), Type::kToggle) << name;
  }
}

TEST_F(SceneTest, AddedModifierPropertiesHaveFlagsOfTheirOwn) {
  const Modifiers fn = scene_.AddModifierProperty("mod:fn");
  const Modifiers fn2 = scene_.AddModifierProperty("mod:fn2");
  EXPECT_NE(fn, 0);
  EXPECT_NE(fn2, 0);
  EXPECT_NE(fn, fn2);

  ViewProperty* property = scene_.GetProperty("mod:fn");
  ASSERT_NE(property, nullptr);
  property->SetBool(true);
  EXPECT_TRUE(AreModifiersOn(fn));
  EXPECT_FALSE(AreModifiersOn(fn2));
  property->SetBool(false);
  EXPECT_FALSE(AreModifiersOn(fn));
}

TEST_F(SceneTest, AddedModifierPropertiesNeedANewModName) {
  ASSERT_NE(scene_.AddModifierProperty("mod:fn"), 0);
  EXPECT_EQ(scene_.AddModifierProperty("mod:fn"), 0);
  EXPECT_EQ(scene_.AddModifierProperty(ModifierProperty::kShift), 0);
  EXPECT_EQ(scene_.AddModifierProperty("user:fn"), 0);
}

TEST_F(SceneTest, AddedModifierPropertiesRunOut) {
  int count = 0;
  while (scene_.AddModifierProperty(absl::StrCat("mod:m", count)) != 0) {
    ++count;
  }
  EXPECT_EQ(count, 60);
}

TEST_F(SceneTest, ConstPropertiesAreFoundByName) {
  ViewProperty* on = scene_.AddConstProperty(Type::kToggle, true);
  ASSERT_NE(on, nullptr);
  EXPECT_EQ(scene_.GetProperty(on->GetName()), on);
  EXPECT_EQ(scene_.AddConstProperty(Type::kVolume, 1.0), nullptr);
}

TEST_F(SceneTest, UserPropertiesAreFoundByName) {
  ToggleValueProperty* flag = scene_.AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:flag"));
  ASSERT_NE(flag, nullptr);
  EXPECT_EQ(scene_.GetProperty("user:flag"), flag);
}

TEST_F(SceneTest, UserPropertiesNeedANewUserName) {
  View* view = scene_.GetRootView()->AddChildView("View");
  ASSERT_NE(view->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:in_view")),
            nullptr);
  ASSERT_NE(scene_.AddTrackReference("user:reference"), nullptr);
  ASSERT_NE(scene_.AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:flag")),
            nullptr);

  for (std::string_view name : {"user:flag", "user:in_view", "user:reference",
                                "state:flag", "user:a.b"}) {
    EXPECT_EQ(
        scene_.AddUserProperty(std::make_unique<ToggleValueProperty>(name)),
        nullptr)
        << name;
  }
}

TEST_F(SceneTest, ReferenceFieldsAreFoundByName) {
  ASSERT_NE(scene_.AddTrackReference("user:track"), nullptr);

  ViewProperty* volume = scene_.GetProperty("state:master_track.volume");
  ASSERT_NE(volume, nullptr);
  EXPECT_EQ(volume->GetName(), TrackProperties::kVolume);
  EXPECT_EQ(scene_.GetProperty("state:master_track.volume"), volume);
  EXPECT_NE(scene_.GetProperty("user:track.volume"), nullptr);
  EXPECT_EQ(scene_.GetProperty("state:master_track.nothing"), nullptr);
  EXPECT_EQ(scene_.GetProperty("state:nothing.volume"), nullptr);
}

//==============================================================================
// References
//==============================================================================

TEST_F(SceneReferenceTest, MasterTrackIsReferred) {
  AddSurface();
  EXPECT_EQ(GetTrackOf(Scene::kMasterTrack),
            ToMediaTrack(reaper_.GetProject().GetMasterTrack()));
}

TEST_F(SceneReferenceTest, SelectedTrackIsTheOnlyTrackSelected) {
  AddSurface();
  EXPECT_EQ(GetTrackOf(Scene::kSelectedTrack), nullptr);

  notifier_.ClickTrack(tracks_[1]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf(Scene::kSelectedTrack), ToMediaTrack(tracks_[1]));

  notifier_.CtrlClickTrack(tracks_[2]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf(Scene::kSelectedTrack), nullptr);
}

TEST_F(SceneReferenceTest, SelectedTrackIsOnlyATrackOnTheSurface) {
  reaper_.GetProject().ShowInMixer(tracks_[1], false);
  AddSurface();

  notifier_.ClickTrack(tracks_[1]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf(Scene::kSelectedTrack), nullptr);
}

TEST_F(SceneReferenceTest, LastTouchedTrackIsReferred) {
  AddSurface();

  notifier_.ClickTrack(tracks_[2]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf(Scene::kLastTouchedTrack), ToMediaTrack(tracks_[2]));
}

TEST_F(SceneReferenceTest, AddedReferencesNeedValidNamesAndRules) {
  ASSERT_NE(scene_.AddTrackReference("user:track"), nullptr);

  EXPECT_EQ(scene_.AddTrackReference("user:track"), nullptr);
  EXPECT_EQ(scene_.AddTrackReference("state:track"), nullptr);
  EXPECT_EQ(scene_.AddTrackReference("user:a.b"), nullptr);
  EXPECT_EQ(
      scene_.AddTrackReference("user:fallback", {.fallback = "user:nothing"}),
      nullptr);
  EXPECT_EQ(scene_.AddTrackReference("user:follow", {.follow = "user:nothing"}),
            nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(5));
}

TEST_F(SceneReferenceTest, AddedReferencesStartAsNothing) {
  ASSERT_NE(scene_.AddTrackReference("user:track"), nullptr);
  AddSurface();
  EXPECT_EQ(GetTrackOf("user:track"), nullptr);
}

TEST_F(SceneReferenceTest, AddedReferencesFallBack) {
  TrackReference* reference = scene_.AddTrackReference(
      "user:track", {.fallback = std::string(Scene::kMasterTrack)});
  ASSERT_NE(reference, nullptr);
  AddSurface();
  const MediaTrack* master =
      ToMediaTrack(reaper_.GetProject().GetMasterTrack());
  EXPECT_EQ(GetTrackOf("user:track"), master);

  reference->Set(GetCachedTrack(tracks_[1]));
  EXPECT_EQ(GetTrackOf("user:track"), ToMediaTrack(tracks_[1]));
  reference->Set(nullptr);
  EXPECT_EQ(GetTrackOf("user:track"), master);

  reference->Set(GetCachedTrack(tracks_[1]));
  reaper_.GetProject().DeleteTrack(tracks_[1]);
  surface_->SetTrackListChange();
  surface_->Run();
  EXPECT_EQ(GetTrackOf("user:track"), master);
}

TEST_F(SceneReferenceTest, AddedReferencesFollowTracksOnTheSurface) {
  ASSERT_NE(
      scene_.AddTrackReference(
          "user:track", {.follow = std::string(Scene::kLastTouchedTrack)}),
      nullptr);
  reaper_.GetProject().ShowInMixer(tracks_[2], false);
  AddSurface();

  notifier_.ClickTrack(tracks_[1]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf("user:track"), ToMediaTrack(tracks_[1]));

  notifier_.ClickTrack(tracks_[2]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf("user:track"), ToMediaTrack(tracks_[1]));
}

TEST_F(SceneReferenceTest, AddedReferencesUpdateInTheOrderAdded) {
  ASSERT_NE(scene_.AddTrackReference(
                "user:first", {.follow = std::string(Scene::kSelectedTrack)}),
            nullptr);
  ASSERT_NE(scene_.AddTrackReference("user:second", {.follow = "user:first"}),
            nullptr);
  AddSurface();

  notifier_.ClickTrack(tracks_[1]);
  surface_->Run();
  EXPECT_EQ(GetTrackOf("user:first"), ToMediaTrack(tracks_[1]));
  EXPECT_EQ(GetTrackOf("user:second"), ToMediaTrack(tracks_[1]));
}

//==============================================================================
// Activation and views
//==============================================================================

TEST_F(SceneTest, IsActiveWhileItsSurfaceIs) {
  View* root = scene_.GetRootView();
  root->Enable();
  EXPECT_FALSE(scene_.IsActive());
  EXPECT_FALSE(root->IsActive());

  AddSurface();
  EXPECT_TRUE(scene_.IsActive());
  EXPECT_TRUE(root->IsActive());

  RemoveSurface();
  EXPECT_FALSE(scene_.IsActive());
  EXPECT_FALSE(root->IsActive());
}

TEST_F(SceneTest, StatePropertiesArePolledOnlyWhileWatched) {
  ViewProperty* dirty = scene_.GetProperty(kStateProjectDirty);
  ASSERT_NE(dirty, nullptr);
  AddSurface();

  reaper_.GetProject().SetDirty(true);
  surface_->Run();
  EXPECT_FALSE(dirty->GetBool());

  // Watching it brings it up to date at once.
  bool changed = false;
  dirty->RegisterFlag(&changed);
  EXPECT_TRUE(dirty->GetBool());

  changed = false;
  reaper_.GetProject().SetDirty(false);
  surface_->Run();
  EXPECT_TRUE(changed);
  EXPECT_FALSE(dirty->GetBool());

  dirty->UnregisterFlag(&changed);
  reaper_.GetProject().SetDirty(true);
  surface_->Run();
  EXPECT_FALSE(dirty->GetBool());
}

TEST_F(SceneTest, ViewConditionsApplyOnTheNextRun) {
  ToggleValueProperty* flag = scene_.AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:flag"));
  ASSERT_NE(flag, nullptr);
  View* root = scene_.GetRootView();
  root->Enable();
  View* view = root->AddChildView(
      "View", {.condition = ViewCondition::Config{.property = "user:flag"}});
  ASSERT_NE(view, nullptr);
  view->Enable();
  AddSurface();
  EXPECT_FALSE(view->IsActive());

  flag->SetBool(true);
  EXPECT_FALSE(view->IsActive());
  surface_->Run();
  EXPECT_TRUE(view->IsActive());

  flag->SetBool(false);
  surface_->Run();
  EXPECT_FALSE(view->IsActive());
}

}  // namespace
}  // namespace jpr
