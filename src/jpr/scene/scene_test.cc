// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gb/test/log_error_guard.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/device/device.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_shape.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/state_properties.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
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
    const TrackReference* reference = GetScene()->GetTrackReference(name);
    EXPECT_NE(reference, nullptr) << name;
    if (reference == nullptr || reference->GetTrack() == nullptr) {
      return nullptr;
    }
    return reference->GetTrack()->GetTrackId();
  }

  std::vector<FakeTrack*> tracks_;
};

//==============================================================================
// Widgets
//==============================================================================

// Two devices of one catalog, a struct of "play", "stop", "transport" (a struct
// of "record"), and "strip" (an array of two structs of "mute"): "present",
// which has only play and record, and "absent". Each is a button, with the
// modes "solid" and "blink". The widgets are present's play and transport, and
// absent's stop and strip. The scene has a toggle, "user:flag".
class SceneWidgetTest : public ::testing::Test {
 protected:
  SceneWidgetTest() {
    auto device = std::make_unique<FakeDevice>(runner_);
    play_ = device->AddButton("play");
    record_ = device->AddButton("transport.record");
    const WidgetShape button = WidgetShape::ForControl(
        play_.control->GetInputs(), play_.control->GetOutputs(),
        {"solid", "blink"});
    const WidgetShape catalog = WidgetShape::ForStruct(
        {{"play", button},
         {"stop", button},
         {"transport", WidgetShape::ForStruct({{"record", button}})},
         {"strip", WidgetShape::ForArray(
                       WidgetShape::ForStruct({{"mute", button}}), 2)}});
    const WidgetDevice widget_devices[] = {
        {.name = "present", .catalog = &catalog, .device = device.get()},
        {.name = "absent", .catalog = &catalog},
    };
    std::optional<WidgetRoot> widgets = ResolveWidgets(
        {.includes = {{.path = "present", .excluded = {"stop", "strip"}}},
         .fields = {{.name = "stop", .widget = std::string("absent.stop")},
                    {.name = "strip",
                     .widget =
                         WidgetJoinDefinition{
                             .slices = {{.path = "absent.strip"}}}}}},
        widget_devices, nullptr);
    EXPECT_TRUE(widgets.has_value());

    std::vector<std::unique_ptr<Device>> devices;
    devices.push_back(std::move(device));
    scene_ = std::make_unique<Scene>(
        "Scene", std::move(devices),
        widgets.has_value() ? std::move(*widgets) : WidgetRoot{});
    scene_->AddUserProperty(std::make_unique<ToggleValueProperty>("user:flag"));
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  Runner runner_{"Device"};
  FakeDevice::FakeControl play_;
  FakeDevice::FakeControl record_;
  std::unique_ptr<Scene> scene_;
};

TEST_F(SceneWidgetTest, FindsControlsByWidgetPath) {
  EXPECT_EQ(scene_->GetControl("play"), play_.control);
  EXPECT_EQ(scene_->GetControl("transport.record"), record_.control);

  for (std::string_view path :
       {"stop", "strip[1].mute", "transport", "nothing", "Device/Play"}) {
    EXPECT_EQ(scene_->GetControl(path), nullptr) << path;
  }
}

TEST_F(SceneWidgetTest, MappingsToAbsentControlsDoNothing) {
  ToggleValueProperty* other = scene_->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:other"));
  ASSERT_NE(other, nullptr);
  View* root = scene_->GetRootView();
  EXPECT_TRUE(root->AddMapping(ViewMapping::kWriteControl, "user:flag", "stop",
                               {.write = {.mode = "blink"}}));
  EXPECT_TRUE(root->AddMapping(ViewMapping::kWriteControl, "user:flag",
                               "strip[1].mute"));
  ASSERT_TRUE(
      root->AddMapping(ViewMapping::kWriteControl, "user:other", "play"));

  // An active mapping that writes watches its property, so only the mapping to
  // play, which is present, was added.
  root->Enable();
  Runner scene_runner("Scene");
  scene_->Activate(scene_runner);
  EXPECT_TRUE(other->IsWatched());
  EXPECT_FALSE(scene_->GetProperty("user:flag")->IsWatched());
  scene_->Deactivate();

  // They are still validated, modes included.
  EXPECT_FALSE(
      root->AddMapping(ViewMapping::kWriteControl, "user:nothing", "stop"));
  EXPECT_FALSE(root->AddMapping(
      ViewMapping::kWriteControl, "user:flag", "stop",
      {.condition = ViewCondition::Config{.property = "user:nothing"}}));
  EXPECT_FALSE(root->AddMapping(ViewMapping::kWriteControl, "user:flag", "stop",
                                {.write = {.mode = "fast"}}));
  EXPECT_FALSE(root->AddMapping(
      ViewMapping::kWriteControl, "user:flag", "stop",
      {.write = {.mode_overrides = {{.property = "user:other",
                                     .value_to_mode = {{true, "fast"}}}}}}));
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(4));
}

TEST_F(SceneWidgetTest, MappingsNeedAControlInTheWidgets) {
  View* root = scene_->GetRootView();
  const std::string_view paths[] = {
      // Paths to nothing.
      "nothing", "transport.play", "strip[2].mute", "play[0]",
      // Paths to a struct or an array.
      "transport", "strip", "strip[0]",
      // Text that isn't a path.
      "Device/Play", "play.", ""};
  for (std::string_view path : paths) {
    EXPECT_FALSE(
        root->AddMapping(ViewMapping::kWriteControl, "user:flag", path))
        << path;
  }
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(std::size(paths)));
}

//==============================================================================
// Properties
//==============================================================================

TEST_F(SceneTest, CommandPropertiesAreCreatedOnce) {
  ViewProperty* undo = GetScene()->GetProperty(kCmdUndo);
  ASSERT_NE(undo, nullptr);
  EXPECT_EQ(undo->GetName(), kCmdUndo);
  EXPECT_EQ(GetScene()->GetProperty(kCmdUndo), undo);
  EXPECT_EQ(GetScene()->GetProperty("cmd:99999"), nullptr);
}

TEST_F(SceneTest, StatePropertiesAreCreatedOnce) {
  ViewProperty* can_redo = GetScene()->GetProperty(kStateCanRedo);
  ASSERT_NE(can_redo, nullptr);
  EXPECT_EQ(can_redo->GetName(), kStateCanRedo);
  EXPECT_EQ(GetScene()->GetProperty(kStateCanRedo), can_redo);
  EXPECT_EQ(GetScene()->GetProperty("state:nothing"), nullptr);
}

TEST_F(SceneTest, ViewNamespacesArentGlobal) {
  EXPECT_EQ(GetScene()->GetProperty(TrackProperties::kMute), nullptr);
  EXPECT_EQ(GetScene()->GetProperty(View::kChildInc), nullptr);
}

TEST_F(SceneTest, ModifierPropertiesAreBuiltIn) {
  for (std::string_view name :
       {ModifierProperty::kShift, ModifierProperty::kCtrl,
        ModifierProperty::kAlt, ModifierProperty::kOpt}) {
    ViewProperty* modifier = GetScene()->GetProperty(name);
    ASSERT_NE(modifier, nullptr) << name;
    EXPECT_EQ(modifier->GetType(), Type::kToggle) << name;
  }
}

TEST_F(SceneTest, AddedModifierPropertiesHaveFlagsOfTheirOwn) {
  const Modifiers fn = GetScene()->AddModifierProperty("mod:fn");
  const Modifiers fn2 = GetScene()->AddModifierProperty("mod:fn2");
  EXPECT_NE(fn, 0);
  EXPECT_NE(fn2, 0);
  EXPECT_NE(fn, fn2);

  ViewProperty* property = GetScene()->GetProperty("mod:fn");
  ASSERT_NE(property, nullptr);
  property->SetBool(true);
  EXPECT_TRUE(AreModifiersOn(fn));
  EXPECT_FALSE(AreModifiersOn(fn2));
  property->SetBool(false);
  EXPECT_FALSE(AreModifiersOn(fn));
}

TEST_F(SceneTest, ModifiersAreFoundByName) {
  EXPECT_EQ(GetScene()->GetModifier(ModifierProperty::kShift), kModShift);
  EXPECT_EQ(GetScene()->GetModifier(ModifierProperty::kOpt), kModOpt);
  const Modifiers fn = GetScene()->AddModifierProperty("mod:fn");
  EXPECT_EQ(GetScene()->GetModifier("mod:fn"), fn);

  EXPECT_EQ(GetScene()->GetModifier("mod:nothing"), 0);
  ASSERT_NE(GetScene()->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:fn")),
            nullptr);
  EXPECT_EQ(GetScene()->GetModifier("user:fn"), 0);
}

TEST_F(SceneTest, AddedModifierPropertiesNeedANewModName) {
  ASSERT_NE(GetScene()->AddModifierProperty("mod:fn"), 0);
  EXPECT_EQ(GetScene()->AddModifierProperty("mod:fn"), 0);
  EXPECT_EQ(GetScene()->AddModifierProperty(ModifierProperty::kShift), 0);
  EXPECT_EQ(GetScene()->AddModifierProperty("user:fn"), 0);
}

TEST_F(SceneTest, AddedModifierPropertiesRunOut) {
  int count = 0;
  while (GetScene()->AddModifierProperty(absl::StrCat("mod:m", count)) != 0) {
    ++count;
  }
  EXPECT_EQ(count, kModUserCount);
  EXPECT_EQ(count, 60);
}

TEST_F(SceneTest, ConstPropertiesAreFoundByName) {
  ViewProperty* on = GetScene()->AddConstProperty(Type::kToggle, true);
  ASSERT_NE(on, nullptr);
  EXPECT_EQ(GetScene()->GetProperty(on->GetName()), on);
  EXPECT_EQ(GetScene()->AddConstProperty(Type::kVolume, 1.0), nullptr);
}

TEST_F(SceneTest, UserPropertiesAreFoundByName) {
  ToggleValueProperty* flag = GetScene()->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:flag"));
  ASSERT_NE(flag, nullptr);
  EXPECT_EQ(GetScene()->GetProperty("user:flag"), flag);
}

TEST_F(SceneTest, UserPropertiesNeedANewUserName) {
  View* view = GetScene()->GetRootView()->AddChildView("View");
  ASSERT_NE(view->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:in_view")),
            nullptr);
  ASSERT_NE(GetScene()->AddTrackReference("user:reference"), nullptr);
  ASSERT_NE(GetScene()->AddUserProperty(
                std::make_unique<ToggleValueProperty>("user:flag")),
            nullptr);

  for (std::string_view name : {"user:flag", "user:in_view", "user:reference",
                                "state:flag", "user:a.b"}) {
    EXPECT_EQ(GetScene()->AddUserProperty(
                  std::make_unique<ToggleValueProperty>(name)),
              nullptr)
        << name;
  }
}

TEST_F(SceneTest, ReferenceFieldsAreFoundByName) {
  ASSERT_NE(GetScene()->AddTrackReference("user:track"), nullptr);

  ViewProperty* volume = GetScene()->GetProperty("state:master_track.volume");
  ASSERT_NE(volume, nullptr);
  EXPECT_EQ(volume->GetName(), TrackProperties::kVolume);
  EXPECT_EQ(GetScene()->GetProperty("state:master_track.volume"), volume);
  EXPECT_NE(GetScene()->GetProperty("user:track.volume"), nullptr);
  EXPECT_EQ(GetScene()->GetProperty("state:master_track.nothing"), nullptr);
  EXPECT_EQ(GetScene()->GetProperty("state:nothing.volume"), nullptr);
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
  ASSERT_NE(GetScene()->AddTrackReference("user:track"), nullptr);

  EXPECT_EQ(GetScene()->AddTrackReference("user:track"), nullptr);
  EXPECT_EQ(GetScene()->AddTrackReference("state:track"), nullptr);
  EXPECT_EQ(GetScene()->AddTrackReference("user:a.b"), nullptr);
  EXPECT_EQ(GetScene()->AddTrackReference("user:fallback",
                                          {.fallback = "user:nothing"}),
            nullptr);
  EXPECT_EQ(
      GetScene()->AddTrackReference("user:follow", {.follow = "user:nothing"}),
      nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(5));
}

TEST_F(SceneReferenceTest, AddedReferencesStartAsNothing) {
  ASSERT_NE(GetScene()->AddTrackReference("user:track"), nullptr);
  AddSurface();
  EXPECT_EQ(GetTrackOf("user:track"), nullptr);
}

TEST_F(SceneReferenceTest, AddedReferencesFallBack) {
  TrackReference* reference = GetScene()->AddTrackReference(
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
      GetScene()->AddTrackReference(
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
  ASSERT_NE(GetScene()->AddTrackReference(
                "user:first", {.follow = std::string(Scene::kSelectedTrack)}),
            nullptr);
  ASSERT_NE(
      GetScene()->AddTrackReference("user:second", {.follow = "user:first"}),
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
  View* root = GetScene()->GetRootView();
  root->Enable();
  EXPECT_FALSE(GetScene()->IsActive());
  EXPECT_FALSE(root->IsActive());

  AddSurface();
  EXPECT_TRUE(GetScene()->IsActive());
  EXPECT_TRUE(root->IsActive());

  RemoveSurface();
  EXPECT_FALSE(GetScene()->IsActive());
  EXPECT_FALSE(root->IsActive());
}

TEST_F(SceneTest, StatePropertiesArePolledOnlyWhileWatched) {
  ViewProperty* dirty = GetScene()->GetProperty(kStateProjectDirty);
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
  ToggleValueProperty* flag = GetScene()->AddUserProperty(
      std::make_unique<ToggleValueProperty>("user:flag"));
  ASSERT_NE(flag, nullptr);
  View* root = GetScene()->GetRootView();
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
