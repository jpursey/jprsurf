// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/win_config_panel.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gb/test/log_recorder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/test_dialog.h"

// Last, as its macros break code included after it.
#include <windows.h>

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;

const FileFilter kFilter = {.description = "Test config", .pattern = "*.test"};

class ConfigPanelTest : public ::testing::Test {
 protected:
  // A call to the panel's file picker.
  struct Pick {
    HWND owner;
    std::string description;
    std::string path;
  };

  // Creates the panel, with RecordPick() as its file picker.
  HWND CreatePanel(std::string_view path) {
    return CreateConfigPanel(
        dialog_.GetHwnd(), path, kFilter,
        std::bind_front(&ConfigPanelTest::RecordPick, this));
  }

  // The panel's file picker: records the call, and picks `picked_`.
  std::optional<std::string> RecordPick(HWND owner, const FileFilter& filter,
                                        std::string_view path) {
    picks_.push_back({owner, filter.description, std::string(path)});
    return picked_;
  }

  // Sends the panel what REAPER sends on OK, with a buffer of `size`, and
  // returns what it wrote there.
  static std::string WriteConfig(HWND panel,
                                 int size = FakeReaper::kConfigBufferSize) {
    // Filled, so a panel that writes no terminating null shows.
    std::string buffer(size, 'x');
    SendMessageW(panel, kWriteConfigMessage, size,
                 reinterpret_cast<LPARAM>(buffer.data()));
    return buffer.c_str();
  }

  // Returns the panel's control of the window class.
  static HWND GetControl(HWND panel, const wchar_t* window_class) {
    return FindWindowExW(panel, nullptr, window_class, nullptr);
  }

  // Clicks the panel's Browse button.
  static void ClickBrowse(HWND panel) {
    HWND button = GetControl(panel, L"Button");
    ASSERT_NE(button, nullptr);
    SendMessageW(panel, WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED),
                 reinterpret_cast<LPARAM>(button));
  }

  // The user edits the path.
  static void EditPath(HWND panel, const wchar_t* path) {
    SetWindowTextW(GetControl(panel, L"Edit"), path);
  }

  gb::LogErrorGuard log_error_guard_;
  TestDialog dialog_;
  std::vector<Pick> picks_;
  std::optional<std::string> picked_;
};

TEST_F(ConfigPanelTest, IsAHiddenChildOfREAPERsDialog) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(GetParent(panel), dialog_.GetHwnd());
  EXPECT_NE(GetWindowLongPtrW(panel, GWL_STYLE) & WS_CHILD, 0);
  EXPECT_FALSE(IsWindowVisible(panel));
  EXPECT_NE(GetControl(panel, L"Edit"), nullptr);
  EXPECT_NE(GetControl(panel, L"Button"), nullptr);
}

TEST_F(ConfigPanelTest, WritesThePathOnOK) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  EXPECT_EQ(WriteConfig(panel), "C:\\configs\\a.test");
}

TEST_F(ConfigPanelTest, WritesAnEmptyPath) {
  HWND panel = CreatePanel("");
  EXPECT_EQ(WriteConfig(panel), "");
}

TEST_F(ConfigPanelTest, WritesAnEditedPath) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  EditPath(panel, L"D:\\other\\b.test");
  EXPECT_EQ(WriteConfig(panel), "D:\\other\\b.test");

  EditPath(panel, L"");
  EXPECT_EQ(WriteConfig(panel), "");
}

TEST_F(ConfigPanelTest, KeepsAPathThatIsNotASCII) {
  const std::string path = "C:\\M\xC3\xBAsica\\\xE9\x9F\xB3.test";
  HWND panel = CreatePanel(path);
  EXPECT_EQ(WriteConfig(panel), path);
}

TEST_F(ConfigPanelTest, BrowseReplacesThePathWithAPickedFile) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  picked_ = "C:\\picked\\b.test";
  ClickBrowse(panel);

  ASSERT_EQ(picks_.size(), 1);
  EXPECT_EQ(picks_[0].owner, dialog_.GetHwnd());
  EXPECT_EQ(picks_[0].description, "Test config");
  EXPECT_EQ(picks_[0].path, "C:\\configs\\a.test");
  EXPECT_EQ(WriteConfig(panel), "C:\\picked\\b.test");
}

TEST_F(ConfigPanelTest, BrowseStartsAtAnEditedPath) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  EditPath(panel, L"D:\\other\\b.test");
  ClickBrowse(panel);

  ASSERT_EQ(picks_.size(), 1);
  EXPECT_EQ(picks_[0].path, "D:\\other\\b.test");
}

TEST_F(ConfigPanelTest, CancellingBrowseKeepsThePath) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  ClickBrowse(panel);

  ASSERT_EQ(picks_.size(), 1);
  EXPECT_EQ(WriteConfig(panel), "C:\\configs\\a.test");
}

TEST_F(ConfigPanelTest, TruncatesAPathTooLongForTheBuffer) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  EXPECT_EQ(WriteConfig(panel, 8), "C:\\conf");
  EXPECT_THAT(log_error_guard_.TakeMessages(),
              ElementsAre(Field(&gb::LogRecorder::Message::text,
                                HasSubstr("too long for REAPER"))));
}

TEST_F(ConfigPanelTest, IgnoresAnEmptyBuffer) {
  HWND panel = CreatePanel("C:\\configs\\a.test");
  SendMessageW(panel, kWriteConfigMessage, 0, 0);
  char buffer = 'x';
  SendMessageW(panel, kWriteConfigMessage, 0,
               reinterpret_cast<LPARAM>(&buffer));
  EXPECT_EQ(buffer, 'x');
}

}  // namespace
}  // namespace jpr
