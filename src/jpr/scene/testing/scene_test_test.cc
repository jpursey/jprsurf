// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/testing/scene_test.h"

#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;

using PressBehavior = InputConfig::PressBehavior;

using SceneTestTest = SceneTest;

// Each button counts the presses its mapping reads, with its press behavior.
class SceneTestPressTest : public SceneTest {
 protected:
  SceneTestPressTest()
      : tap_(device_->AddButton("Tap")),
        double_press_(device_->AddButton("DoublePress")),
        long_press_(device_->AddButton("LongPress")),
        hold_(device_->AddButton("Hold")) {
    View* root = scene_.GetRootView();
    root->Enable();
    AddCounter(root, "Tap", PressBehavior::kNormal, &taps_);
    AddCounter(root, "DoublePress", PressBehavior::kDoublePress,
               &double_presses_);
    AddCounter(root, "LongPress", PressBehavior::kLongPress, &long_presses_);
    held_ = scene_.AddUserProperty(
        std::make_unique<ToggleValueProperty>("user:held"));
    EXPECT_TRUE(root->AddMapping(ViewMapping::kReadControl, "user:held",
                                 GetControlName("Hold"),
                                 {.read = {.press_release = true}}));
    AddSurface();
  }

  // Maps the button `name` to an action that counts in `count`.
  void AddCounter(View* view, std::string_view name, PressBehavior behavior,
                  int* count) {
    const std::string property = absl::StrCat("user:", name);
    scene_.AddUserProperty(std::make_unique<CallbackActionProperty>(
        property, [count] { ++*count; }));
    EXPECT_TRUE(view->AddMapping(ViewMapping::kReadControl, property,
                                 GetControlName(name),
                                 {.read = {.press_behavior = behavior}}));
  }

  FakeDevice::FakeControl tap_;
  FakeDevice::FakeControl double_press_;
  FakeDevice::FakeControl long_press_;
  FakeDevice::FakeControl hold_;
  int taps_ = 0;
  int double_presses_ = 0;
  int long_presses_ = 0;
  ToggleValueProperty* held_ = nullptr;
};

TEST_F(SceneTestTest, ErrorsLoggedAreKeptToFailTheTest) {
  LOG(ERROR) << "Expected";
  EXPECT_THAT(log_error_guard_.TakeMessages(),
              ElementsAre(Field(&gb::LogRecorder::Message::text, "Expected")));
}

TEST_F(SceneTestTest, ControlsAreNamedForTheDevice) {
  EXPECT_EQ(GetControlName("Play"), "Device/Play");
}

TEST_F(SceneTestTest, TheSurfaceRunsTheSceneUntilItIsShown) {
  FakeDevice::FakeControl mute = device_->AddButton("Mute");
  View* root = scene_.GetRootView();
  root->Enable();
  ASSERT_TRUE(root->AddMapping(ViewMapping::kWriteControl,
                               "state:master_track.mute",
                               GetControlName("Mute")));
  reaper_.GetProject().GetMasterTrack()->mute = true;

  AddSurface();
  EXPECT_EQ(mute.dvalue_output->GetValue(), 1);

  reaper_.GetProject().GetMasterTrack()->mute = false;
  RunUntilShown();
  EXPECT_EQ(mute.dvalue_output->GetValue(), 0);
}

TEST_F(SceneTestPressTest, TapPressesOnce) {
  Tap(tap_);
  EXPECT_EQ(taps_, 1);
  EXPECT_EQ(double_presses_, 0);
  EXPECT_EQ(long_presses_, 0);
}

TEST_F(SceneTestPressTest, DoublePressIsADoublePress) {
  DoublePress(double_press_);
  EXPECT_EQ(double_presses_, 1);
}

TEST_F(SceneTestPressTest, LongPressIsALongPress) {
  LongPress(long_press_);
  EXPECT_EQ(long_presses_, 1);
}

TEST_F(SceneTestPressTest, HoldHoldsUntilReleased) {
  Hold(hold_);
  EXPECT_TRUE(held_->GetBool());

  Release(hold_);
  RunUntilShown();
  EXPECT_FALSE(held_->GetBool());
}

}  // namespace
}  // namespace jpr
