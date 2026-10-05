// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/polled_toggle_property.h"

#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/scene/scene.h"

namespace jpr {
namespace {

// The state the property polls, and the values written to it.
bool g_state = false;
std::vector<bool> g_writes;

bool ReadState() { return g_state; }
void WriteState(bool value) {
  g_writes.push_back(value);
  g_state = value;
}

// Each test brings the property up to date as the scene does while it is
// watched, with UpdateState() (see scene_test.cc for the scene's polling).
class PolledTogglePropertyTest : public ::testing::Test {
 protected:
  PolledTogglePropertyTest() {
    g_state = false;
    g_writes.clear();
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  Scene scene_{"Scene"};
};

TEST_F(PolledTogglePropertyTest, ReadsTheStateWhenCreated) {
  g_state = true;
  PolledToggleProperty property(&scene_, "user:polled", &ReadState);
  EXPECT_TRUE(property.GetBool());
}

TEST_F(PolledTogglePropertyTest, UpdatesNotifyOnlyOnChange) {
  PolledToggleProperty property(&scene_, "user:polled", &ReadState);
  bool changed = false;
  property.RegisterFlag(&changed);
  changed = false;

  property.UpdateState();
  EXPECT_FALSE(changed);

  g_state = true;
  EXPECT_FALSE(property.GetBool());
  property.UpdateState();
  EXPECT_TRUE(changed);
  EXPECT_TRUE(property.GetBool());
  property.UnregisterFlag(&changed);
}

TEST_F(PolledTogglePropertyTest, WritesChangeTheState) {
  PolledToggleProperty property(&scene_, "user:polled", &ReadState,
                                &WriteState);

  property.SetBool(true);
  EXPECT_TRUE(g_state);
  EXPECT_TRUE(property.GetBool());

  // Writing the value it already has writes nothing.
  property.SetBool(true);
  EXPECT_EQ(g_writes, std::vector<bool>{true});
}

TEST_F(PolledTogglePropertyTest, WithoutAWriteFunctionWritesDoNothing) {
  PolledToggleProperty property(&scene_, "user:polled", &ReadState);

  property.SetBool(true);
  EXPECT_FALSE(g_state);
  EXPECT_FALSE(property.GetBool());
}

}  // namespace
}  // namespace jpr
