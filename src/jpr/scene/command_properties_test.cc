// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/command_properties.h"

#include <memory>
#include <string_view>

#include "gb/test/log_error_guard.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

using Type = ViewProperty::Type;

// REAPER's actions, with the metronome's off. Each test brings a toggle up to
// date as the scene does while it is watched, with UpdateState() (see
// scene_test.cc for the scene's polling).
class CommandPropertiesTest : public ::testing::Test {
 protected:
  CommandPropertiesTest() { AddReaperActions(&reaper_); }

  // Makes the metronome's action toggle its state, as REAPER's does.
  void ToggleMetronomeWhenRun() {
    reaper_.SetCommandHandler(40364, [this] {
      reaper_.SetToggleState(40364, 1 - reaper_.GetToggleState(40364));
    });
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  Scene scene_{"Scene"};
};

TEST_F(CommandPropertiesTest, CommandsWithoutAToggleStateAreActions) {
  std::unique_ptr<ViewProperty> undo = CreateCommandProperty(&scene_, kCmdUndo);
  ASSERT_NE(undo, nullptr);
  EXPECT_EQ(undo->GetType(), Type::kAction);
  EXPECT_EQ(undo->GetName(), kCmdUndo);

  undo->RunAction();
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(kUndoAction));
}

TEST_F(CommandPropertiesTest, CommandsWithAToggleStateAreToggles) {
  reaper_.SetToggleState(40364, 1);
  std::unique_ptr<ViewProperty> metronome =
      CreateCommandProperty(&scene_, kCmdMetronome);
  ASSERT_NE(metronome, nullptr);
  EXPECT_EQ(metronome->GetType(), Type::kToggle);
  EXPECT_TRUE(metronome->GetBool());
}

TEST_F(CommandPropertiesTest, TogglesFollowTheState) {
  CommandToggleProperty metronome(&scene_, kCmdMetronome, 40364, false);
  bool changed = false;
  metronome.RegisterFlag(&changed);
  changed = false;

  reaper_.SetToggleState(40364, 1);
  metronome.UpdateState();
  EXPECT_TRUE(changed);
  EXPECT_TRUE(metronome.GetBool());

  changed = false;
  metronome.UpdateState();
  EXPECT_FALSE(changed);
  metronome.UnregisterFlag(&changed);
}

TEST_F(CommandPropertiesTest, WritingAToggleRunsItsCommand) {
  ToggleMetronomeWhenRun();
  std::unique_ptr<ViewProperty> metronome =
      CreateCommandProperty(&scene_, kCmdMetronome);

  // Writing the value it already has runs nothing.
  metronome->SetBool(false);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());

  metronome->SetBool(true);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(40364));
  EXPECT_TRUE(metronome->GetBool());
}

TEST_F(CommandPropertiesTest, NamedCommandsAreFoundByName) {
  reaper_.AddCommand(
      {.id = 55000, .text = "Script: Test", .name = "_TEST_SCRIPT"});
  std::unique_ptr<ViewProperty> script =
      CreateCommandProperty(&scene_, "cmd:_TEST_SCRIPT");
  ASSERT_NE(script, nullptr);
  EXPECT_EQ(script->GetType(), Type::kAction);

  script->RunAction();
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(55000));
}

TEST_F(CommandPropertiesTest, UnknownCommandsAreNull) {
  for (std::string_view name : {"cmd:99999", "cmd:0", "cmd:-1", "cmd:undo",
                                "cmd:", "cmd:_TEST_NOTHING"}) {
    EXPECT_EQ(CreateCommandProperty(&scene_, name), nullptr) << name;
  }
}

}  // namespace
}  // namespace jpr
