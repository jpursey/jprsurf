// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/state_properties.h"

#include <iterator>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/view_property.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;

// Each state property is polled while it is watched, which the scene does on
// each run (see scene_test.cc). The selected tracks' automation modes follow
// REAPER's calls back, so these tests run the scene from its surface.
class StatePropertiesTest : public SceneTest {
 protected:
  // A state property's name, and what it is for.
  template <typename Value>
  struct Row {
    std::string_view name;
    Value value;
  };

  StatePropertiesTest() : tracks_(project_.AddTracks(2)) {}

  // Returns the state property `name`, watched, which brings it up to date.
  ViewProperty* Watch(std::string_view name) {
    ViewProperty* property = scene_.GetProperty(name);
    EXPECT_NE(property, nullptr) << name;
    if (property != nullptr) {
      property->RegisterFlag(&changed_);
      watched_.push_back(property);
    }
    return property;
  }

  // Returns each of the properties' values.
  static std::vector<bool> GetValues(
      const std::vector<ViewProperty*>& properties) {
    std::vector<bool> values;
    for (const ViewProperty* property : properties) {
      values.push_back(property->GetBool());
    }
    return values;
  }

  // The global automation override, as REAPER's UI sets it.
  AutoOverride GetOverride() {
    return static_cast<AutoOverride>(project_.GetAutomationOverride());
  }
  void SetOverride(AutoOverride auto_override) {
    project_.SetAutomationOverride(static_cast<int>(auto_override));
  }

  // Unwatches the properties, before the scene goes.
  void TearDown() override {
    for (ViewProperty* property : watched_) {
      property->UnregisterFlag(&changed_);
    }
    SceneTest::TearDown();
  }

  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> tracks_;
  std::vector<ViewProperty*> watched_;
  bool changed_ = false;
};

TEST_F(StatePropertiesTest, UnknownNamesAreNull) {
  EXPECT_EQ(CreateStateProperty(&scene_, "state:nothing"), nullptr);
  EXPECT_EQ(CreateStateProperty(&scene_, "nothing"), nullptr);
}

TEST_F(StatePropertiesTest, PolledTogglesEachFollowTheirState) {
  AddSurface();
  const std::vector<ViewProperty*> toggles = {
      Watch(kStateAnyTrackSolo), Watch(kStateCanRedo),
      Watch(kStateProjectDirty), Watch(kStateAnyItemSelected)};
  EXPECT_THAT(GetValues(toggles), ElementsAre(false, false, false, false));

  tracks_[1]->solo = true;
  surface_->Run();
  EXPECT_THAT(GetValues(toggles), ElementsAre(true, false, false, false));
  SetTrackUIMute(ToMediaTrack(tracks_[0]), 1, kPreventGroupingAndGanging);
  Undo_OnStateChangeEx("Mute", UNDO_STATE_TRACKCFG, -1);
  project_.Undo();
  project_.SetDirty(false);
  surface_->Run();
  EXPECT_THAT(GetValues(toggles), ElementsAre(true, true, false, false));
  project_.SetDirty(true);
  surface_->Run();
  EXPECT_THAT(GetValues(toggles), ElementsAre(true, true, true, false));
  project_.SetSelectedItemCount(2);
  surface_->Run();
  EXPECT_THAT(GetValues(toggles), ElementsAre(true, true, true, true));
}

TEST_F(StatePropertiesTest, SelectedAutomationModesAreEachMode) {
  const Row<AutoMode> kModes[] = {
      {kStateSelectedAutoTrimRead, AutoMode::kTrimRead},
      {kStateSelectedAutoRead, AutoMode::kRead},
      {kStateSelectedAutoTouch, AutoMode::kTouch},
      {kStateSelectedAutoWrite, AutoMode::kWrite},
      {kStateSelectedAutoLatch, AutoMode::kLatch},
      {kStateSelectedAutoLatchPreview, AutoMode::kLatchPreview},
  };
  AddSurface();
  std::vector<ViewProperty*> modes;
  for (const Row<AutoMode>& mode : kModes) {
    modes.push_back(Watch(mode.name));
  }
  ViewProperty* mixed = Watch(kStateSelectedAutoMixed);

  // A track's mode is read when the selection changes, as REAPER calls back
  // when a mode changes. The master, in trim/read, isn't selected.
  for (const Row<AutoMode>& expected : kModes) {
    tracks_[0]->auto_mode = static_cast<int>(expected.value);
    notifier_.ClickTrack(tracks_[1]);
    notifier_.ClickTrack(tracks_[0]);
    surface_->Run();
    for (int i = 0; i < std::ssize(kModes); ++i) {
      EXPECT_EQ(modes[i]->GetBool(), kModes[i].name == expected.name)
          << expected.name << " selected, " << kModes[i].name;
    }
    EXPECT_FALSE(mixed->GetBool()) << expected.name;
  }

  // The first track is in latch preview.
  tracks_[1]->auto_mode = static_cast<int>(AutoMode::kRead);
  notifier_.CtrlClickTrack(tracks_[1]);
  surface_->Run();
  EXPECT_THAT(GetValues(modes),
              ElementsAre(false, true, false, false, false, true));
  EXPECT_TRUE(mixed->GetBool());
}

TEST_F(StatePropertiesTest, OverridesAreEachOverride) {
  const Row<AutoOverride> kOverrides[] = {
      {kStateAutoOverrideTrimRead, AutoOverride::kTrimRead},
      {kStateAutoOverrideRead, AutoOverride::kRead},
      {kStateAutoOverrideTouch, AutoOverride::kTouch},
      {kStateAutoOverrideWrite, AutoOverride::kWrite},
      {kStateAutoOverrideLatch, AutoOverride::kLatch},
      {kStateAutoOverrideLatchPreview, AutoOverride::kLatchPreview},
      {kStateAutoOverrideBypass, AutoOverride::kBypass},
  };
  AddSurface();
  ViewProperty* active = Watch(kStateAutoOverrideActive);
  ViewProperty* any_latch = Watch(kStateAutoOverrideAnyLatch);
  std::vector<ViewProperty*> overrides;
  for (const Row<AutoOverride>& expected : kOverrides) {
    overrides.push_back(Watch(expected.name));
  }

  for (const Row<AutoOverride>& expected : kOverrides) {
    SetOverride(expected.value);
    surface_->Run();
    EXPECT_TRUE(active->GetBool()) << expected.name;
    for (int i = 0; i < std::ssize(kOverrides); ++i) {
      EXPECT_EQ(overrides[i]->GetBool(), kOverrides[i].name == expected.name)
          << expected.name << " on, " << kOverrides[i].name;
    }
    EXPECT_EQ(any_latch->GetBool(),
              expected.value == AutoOverride::kLatch ||
                  expected.value == AutoOverride::kLatchPreview)
        << expected.name;
  }

  SetOverride(AutoOverride::kNone);
  surface_->Run();
  EXPECT_FALSE(active->GetBool());
}

TEST_F(StatePropertiesTest, WritingAnOverrideSetsIt) {
  AddSurface();
  ViewProperty* touch = Watch(kStateAutoOverrideTouch);
  ViewProperty* bypass = Watch(kStateAutoOverrideBypass);

  touch->SetBool(true);
  EXPECT_EQ(GetOverride(), AutoOverride::kTouch);
  EXPECT_TRUE(touch->GetBool());

  // Turning a mode off sets Bypass, and turning Bypass off removes it.
  touch->SetBool(false);
  EXPECT_EQ(GetOverride(), AutoOverride::kBypass);
  surface_->Run();
  EXPECT_TRUE(bypass->GetBool());
  bypass->SetBool(false);
  EXPECT_EQ(GetOverride(), AutoOverride::kNone);
  bypass->SetBool(true);
  EXPECT_EQ(GetOverride(), AutoOverride::kBypass);
}

TEST_F(StatePropertiesTest, AnyLatchIsReadOnly) {
  AddSurface();
  ViewProperty* any_latch = Watch(kStateAutoOverrideAnyLatch);

  any_latch->SetBool(true);
  EXPECT_EQ(GetOverride(), AutoOverride::kNone);
  EXPECT_FALSE(any_latch->GetBool());
}

TEST_F(StatePropertiesTest, TurningTheOverrideOnRestoresTheLastSeen) {
  AddSurface();
  ViewProperty* active = Watch(kStateAutoOverrideActive);

  // An override set in REAPER is seen while the property is polled.
  SetOverride(AutoOverride::kLatch);
  surface_->Run();
  active->SetBool(false);
  EXPECT_EQ(GetOverride(), AutoOverride::kNone);
  EXPECT_FALSE(active->GetBool());

  active->SetBool(true);
  EXPECT_EQ(GetOverride(), AutoOverride::kLatch);
  EXPECT_TRUE(active->GetBool());
}

// TimelinePositionProperty and the ruler properties are tested on their own
// (see timeline_property_test.cc). These check that each name is the one it
// says.
TEST_F(StatePropertiesTest, TimelinePositionsAreEachSource) {
  project_.SetPlayPosition(2.0);
  project_.SetCursorPosition(1.0);
  AddSurface();
  ViewProperty* current = Watch(kStateTimelinePosition);
  ViewProperty* playback = Watch(kStatePlaybackPosition);
  ViewProperty* edit = Watch(kStateEditPosition);
  EXPECT_EQ(current->GetTimelinePosition().GetValue(), 1.0);
  EXPECT_EQ(playback->GetTimelinePosition().GetValue(), 2.0);
  EXPECT_EQ(edit->GetTimelinePosition().GetValue(), 1.0);

  project_.SetPlayState(1);
  surface_->Run();
  EXPECT_EQ(current->GetTimelinePosition().GetValue(), 2.0);
  EXPECT_EQ(edit->GetTimelinePosition().GetValue(), 1.0);
}

TEST_F(StatePropertiesTest, RulersAreEachMode) {
  // The command that selects each mode.
  const Row<int> kModes[] = {
      {kStateRulerBeats, kRulerMeasuresBeats},
      {kStateRulerTime, kRulerMinutesSeconds},
      {kStateRulerFrames, kRulerFrames},
      {kStateRulerSamples, kRulerSamples},
  };
  const Row<int> kSecondaryModes[] = {
      {kStateSecondaryRulerTime, kRulerSecondaryMinutesSeconds},
      {kStateSecondaryRulerFrames, kRulerSecondaryFrames},
      {kStateSecondaryRulerSamples, kRulerSecondarySamples},
  };
  AddSurface();
  ViewProperty* mode = Watch(kStateRulerMode);
  ViewProperty* secondary_mode = Watch(kStateSecondaryRulerMode);

  for (int i = 0; i < std::ssize(kModes); ++i) {
    SelectRulerMode(&reaper_, kModes[i].value);
    surface_->Run();
    EXPECT_EQ(mode->GetInt(), i) << kModes[i].name;
    for (const Row<int>& other : kModes) {
      EXPECT_EQ(Watch(other.name)->GetBool(), other.name == kModes[i].name)
          << kModes[i].name << " on, " << other.name;
    }
  }
  for (int i = 0; i < std::ssize(kSecondaryModes); ++i) {
    SelectRulerMode(&reaper_, kSecondaryModes[i].value);
    surface_->Run();
    EXPECT_EQ(secondary_mode->GetInt(), i + 1) << kSecondaryModes[i].name;
    for (const Row<int>& other : kSecondaryModes) {
      EXPECT_EQ(Watch(other.name)->GetBool(),
                other.name == kSecondaryModes[i].name)
          << kSecondaryModes[i].name << " on, " << other.name;
    }
  }
}

TEST(StatePropertiesResetTest, TurningTheOverrideOnRestoresBypassWithEachFake) {
  {
    FakeReaper reaper;
    reaper.GetProject().SetAutomationOverride(
        static_cast<int>(AutoOverride::kWrite));
    Scene scene("Scene");

    // Seeing an override on keeps it, for turning the override on again.
    ViewProperty* active = scene.GetProperty(kStateAutoOverrideActive);
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->GetBool());
  }
  FakeReaper reaper;
  Scene scene("Scene");
  ViewProperty* active = scene.GetProperty(kStateAutoOverrideActive);
  ASSERT_NE(active, nullptr);
  active->SetBool(true);
  EXPECT_EQ(reaper.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kBypass));
}

}  // namespace
}  // namespace jpr
