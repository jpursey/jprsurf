// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin.h"

#include <memory>
#include <string>
#include <vector>

#include "gb/file/local_file_protocol.h"
#include "gb/file/memory_file_protocol.h"
#include "gb/test/log_error_guard.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/file_system.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/common/testing/test_control_surface.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::StartsWith;

class PluginTest : public ::testing::Test {
 protected:
  // The surface's scene maps REAPER's actions, so a REAPER without them
  // creates none. Config files are read from the local file system, where
  // JPRSurf's is checked in, or from "test:".
  PluginTest() {
    AddReaperActions(&reaper_);
    EXPECT_TRUE(
        GetFileSystem()->Register(gb::LocalFileProtocol::CreateOsRoot()));
    EXPECT_TRUE(GetFileSystem()->Register(
        std::make_unique<gb::MemoryFileProtocol>(), "test"));
  }

  // Loads the plugin as REAPER does, with `options`.
  bool Load(const Plugin::Options& options = {.config_path = JPR_CONFIG_FILE}) {
    return Plugin::Load(nullptr, reaper_.GetPluginInfo(), options);
  }

  // Returns the messages logged at ERROR or above since the last call.
  std::vector<std::string> TakeErrors() {
    std::vector<std::string> errors;
    for (const gb::LogRecorder::Message& message :
         log_error_guard_.TakeMessages()) {
      errors.push_back(message.text);
    }
    return errors;
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeMidiInput* xtouch_in_ = reaper_.AddMidiInput("X-Touch");
  FakeMidiOutput* xtouch_out_ = reaper_.AddMidiOutput("X-Touch");
};

TEST_F(PluginTest, LoadsAndUnloads) {
  ASSERT_TRUE(Load());
  EXPECT_NE(Plugin::GetInstance(), nullptr);

  Plugin::Unload();
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
}

TEST_F(PluginTest, AddingASurfaceOpensTheXTouchPorts) {
  FakeMidiInput* xtouch_ext_in = reaper_.AddMidiInput("X-Touch-Ext");
  FakeMidiOutput* xtouch_ext_out = reaper_.AddMidiOutput("X-Touch-Ext");
  ASSERT_TRUE(Load());

  std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(xtouch_in_->IsOpen());
  EXPECT_TRUE(xtouch_out_->IsOpen());
  EXPECT_TRUE(xtouch_ext_in->IsOpen());
  EXPECT_TRUE(xtouch_ext_out->IsOpen());

  surface.reset();
  EXPECT_FALSE(xtouch_in_->IsOpen());
  EXPECT_FALSE(xtouch_out_->IsOpen());
  EXPECT_FALSE(xtouch_ext_in->IsOpen());
  EXPECT_FALSE(xtouch_ext_out->IsOpen());
  Plugin::Unload();
}

TEST_F(PluginTest, NullIsTheDefaultConfig) {
  ASSERT_TRUE(Load());
  std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface("null");
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(xtouch_in_->IsOpen());
  EXPECT_STREQ(surface->GetConfigString(), "null");
  surface.reset();
  Plugin::Unload();
}

TEST_F(PluginTest, AConfigStringIsTheConfigFilesPath) {
  ASSERT_TRUE(Load({.config_path = "test:/missing.jprsurf"}));

  std::unique_ptr<TestControlSurface> surface =
      reaper_.AddSurface(JPR_CONFIG_FILE);
  ASSERT_NE(surface, nullptr);
  EXPECT_TRUE(xtouch_in_->IsOpen());
  EXPECT_STREQ(surface->GetConfigString(), JPR_CONFIG_FILE);
  surface.reset();
  Plugin::Unload();
}

TEST_F(PluginTest, AMissingConfigFileRunsNothing) {
  ASSERT_TRUE(Load());
  std::unique_ptr<TestControlSurface> surface =
      reaper_.AddSurface("test:/missing.jprsurf");
  ASSERT_NE(surface, nullptr);
  EXPECT_FALSE(xtouch_in_->IsOpen());
  EXPECT_THAT(
      TakeErrors(),
      ElementsAre("test:/missing.jprsurf: the config file can't be read"));
  surface.reset();
  Plugin::Unload();
}

TEST_F(PluginTest, AConfigWithErrorsLogsEachAndRunsNothing) {
  ASSERT_TRUE(GetFileSystem()->WriteFile("test:/errors.jprsurf", R"(jprsurf 1
devices { xtouch = XTouch(input = "X-Touch", output = "X-Touch") }
widgets { include xtouch }
mappings {
  nothing -> cmd:1007
  play -> state:nothing
}
)"));
  ASSERT_TRUE(Load());
  std::unique_ptr<TestControlSurface> surface =
      reaper_.AddSurface("test:/errors.jprsurf");
  ASSERT_NE(surface, nullptr);
  EXPECT_FALSE(xtouch_in_->IsOpen());
  EXPECT_THAT(
      TakeErrors(),
      ElementsAre("test:/errors.jprsurf: line 5: /: nothing: names nothing",
                  "test:/errors.jprsurf: line 6: /: play: property "
                  "\"state:nothing\" isn't a property"));
  surface.reset();
  Plugin::Unload();
}

TEST_F(PluginTest, LoadingTwiceFails) {
  ASSERT_TRUE(Load());
  Plugin* plugin = Plugin::GetInstance();

  EXPECT_FALSE(Load());
  EXPECT_EQ(Plugin::GetInstance(), plugin);
  EXPECT_THAT(TakeErrors(), ElementsAre("Plugin instance already exists."));
  Plugin::Unload();
}

TEST_F(PluginTest, LoadingFromAnotherVersionOfREAPERFails) {
  reaper_plugin_info_t plugin_info = reaper_.GetPluginInfo();
  plugin_info.caller_version = REAPER_PLUGIN_VERSION - 1;

  EXPECT_FALSE(Plugin::Load(nullptr, plugin_info, {}));
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
  EXPECT_THAT(TakeErrors(), ElementsAre(StartsWith("Plugin version mismatch")));
}

TEST_F(PluginTest, LoadingWithNoApiFails) {
  reaper_plugin_info_t plugin_info = reaper_.GetPluginInfo();
  plugin_info.GetFunc = nullptr;

  EXPECT_FALSE(Plugin::Load(nullptr, plugin_info, {}));
  EXPECT_EQ(Plugin::GetInstance(), nullptr);
  EXPECT_THAT(TakeErrors(), ElementsAre("Plugin info GetFunc is null."));
}

TEST_F(PluginTest, TracesToTheTracePath) {
  ASSERT_TRUE(GetFileSystem()->Register(
      std::make_unique<gb::MemoryFileProtocol>(), "trace"));
  ASSERT_TRUE(Load({.trace_path = "trace:/test.txt"}));
  Plugin::Unload();
  std::string trace;
  ASSERT_TRUE(GetFileSystem()->ReadFile("trace:/test.txt", &trace));
  EXPECT_THAT(trace, StartsWith("JPRSurf REAPER trace"));
}

TEST_F(PluginTest, SurfacesWriteTheirProfileToTheProfilePath) {
  ASSERT_TRUE(GetFileSystem()->Register(
      std::make_unique<gb::MemoryFileProtocol>(), "profile"));
  ASSERT_TRUE(Load(
      {.profile_path = "profile:/test.txt", .config_path = JPR_CONFIG_FILE}));
  std::unique_ptr<TestControlSurface> surface = reaper_.AddSurface();
  ASSERT_NE(surface, nullptr);
  surface->Run();

  surface.reset();
  std::string profile;
  ASSERT_TRUE(GetFileSystem()->ReadFile("profile:/test.txt", &profile));
  EXPECT_THAT(profile, StartsWith("JPRSurf profile\n"));
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
