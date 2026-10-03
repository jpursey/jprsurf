// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin.h"

#include <filesystem>
#include <memory>

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// Returns a path for a file a test writes, after removing any left from an
// earlier run.
std::filesystem::path GetTestPath(const char* file_name) {
  std::filesystem::path path =
      std::filesystem::path(::testing::TempDir()) / file_name;
  std::filesystem::remove(path);
  return path;
}

class PluginTest : public ::testing::Test {
 protected:
  // Loads the plugin as REAPER does, with `options`.
  bool Load(const Plugin::Options& options = {}) {
    return Plugin::Load(nullptr, reaper_.GetPluginInfo(), options);
  }

  FakeReaper reaper_;
};

TEST_F(PluginTest, LoadsAndUnloads) {
  ASSERT_TRUE(Load());
  EXPECT_NE(Plugin::GetInstance(), nullptr);

  Plugin::Unload();
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
}

TEST_F(PluginTest, AddingASurfaceOpensTheXTouchPorts) {
  FakeMidiInput* xtouch_in = reaper_.AddMidiInput("X-Touch");
  FakeMidiOutput* xtouch_out = reaper_.AddMidiOutput("X-Touch");
  FakeMidiInput* xtouch_ext_in = reaper_.AddMidiInput("X-Touch-Ext");
  FakeMidiOutput* xtouch_ext_out = reaper_.AddMidiOutput("X-Touch-Ext");
  ASSERT_TRUE(Load());

  std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(xtouch_in->IsOpen());
  EXPECT_TRUE(xtouch_out->IsOpen());
  EXPECT_TRUE(xtouch_ext_in->IsOpen());
  EXPECT_TRUE(xtouch_ext_out->IsOpen());

  surface.reset();
  EXPECT_FALSE(xtouch_in->IsOpen());
  EXPECT_FALSE(xtouch_out->IsOpen());
  EXPECT_FALSE(xtouch_ext_in->IsOpen());
  EXPECT_FALSE(xtouch_ext_out->IsOpen());
  Plugin::Unload();
}

TEST_F(PluginTest, LoadingTwiceFails) {
  ASSERT_TRUE(Load());
  Plugin* plugin = Plugin::GetInstance();

  EXPECT_FALSE(Load());
  EXPECT_EQ(Plugin::GetInstance(), plugin);
  Plugin::Unload();
}

TEST_F(PluginTest, LoadingFromAnotherVersionOfREAPERFails) {
  reaper_plugin_info_t plugin_info = reaper_.GetPluginInfo();
  plugin_info.caller_version = REAPER_PLUGIN_VERSION - 1;

  EXPECT_FALSE(Plugin::Load(nullptr, plugin_info, {}));
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
}

TEST_F(PluginTest, LoadingWithNoApiFails) {
  reaper_plugin_info_t plugin_info = reaper_.GetPluginInfo();
  plugin_info.GetFunc = nullptr;

  EXPECT_FALSE(Plugin::Load(nullptr, plugin_info, {}));
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
}

TEST_F(PluginTest, TracesToTheTracePath) {
  const std::filesystem::path path = GetTestPath("plugin_test_trace.txt");
  ASSERT_TRUE(Load({.trace_path = path}));
  EXPECT_TRUE(std::filesystem::exists(path));
  Plugin::Unload();
}

TEST_F(PluginTest, SurfacesWriteTheirProfileToTheProfilePath) {
  const std::filesystem::path path = GetTestPath("plugin_test_profile.txt");
  ASSERT_TRUE(Load({.profile_path = path}));
  std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
  ASSERT_NE(surface, nullptr);
  surface->Run();

  surface.reset();
  EXPECT_TRUE(std::filesystem::exists(path));
  Plugin::Unload();
}

TEST(PluginResetTest, TheFakeResetsAnInstanceLeftLoaded) {
  {
    FakeReaper reaper;
    ASSERT_TRUE(Plugin::Load(nullptr, reaper.GetPluginInfo(), {}));
  }
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
}

}  // namespace
}  // namespace jpr
