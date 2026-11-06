// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/win_config_panel.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gb/base/unicode.h"
#include "gb/base/win_platform.h"

// Last, as its macros break code included after it.
#include <windows.h>

// Needs windows.h before it.
#include <commdlg.h>

namespace jpr {

namespace {

constexpr int kPathId = 1001;
constexpr int kBrowseId = 1002;

// The longest path the file picker returns, in characters.
constexpr int kMaxPathLength = 32768;

// Where a control goes in the panel, in dialog units.
struct Place {
  int x;
  int y;
  int width;
  int height;
};

// A dialog with no controls (the panel adds its own), as a child of REAPER's
// dialog, in the font REAPER's own panels use, so dialog units size the
// controls as theirs. It is a DLGTEMPLATE, followed by its variable length
// parts, which must start on a DWORD.
struct alignas(4) PanelTemplate {
  DLGTEMPLATE dialog;
  WORD menu;
  WORD window_class;
  WCHAR title;
  WORD point_size;
  WCHAR font[13];
};

constexpr PanelTemplate kPanelTemplate = {
    .dialog = {.style = DS_CONTROL | DS_SETFONT | WS_CHILD,
               .cx = 268,
               .cy = 20},
    .point_size = 8,
    .font = L"MS Shell Dlg"};

// Returns UTF-16 text as Windows takes it, as gb/base/unicode.h suggests.
const wchar_t* AsWide(const std::u16string& text) {
  return reinterpret_cast<const wchar_t*>(text.c_str());
}

wchar_t* AsWide(std::u16string& text) {
  return reinterpret_cast<wchar_t*>(text.data());
}

// Sets the path in a panel's edit box.
void SetPath(HWND panel, std::string_view path) {
  SetDlgItemTextW(panel, kPathId, AsWide(gb::ToUtf16(path)));
}

// Returns the module this code is in, which owns the windows it creates.
HINSTANCE GetModule() {
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&GetModule), &module);
  return module;
}

// The panel's state, which its window owns: created with the window, and
// deleted when it is destroyed.
class ConfigPanel final {
 public:
  ConfigPanel(const FileFilter& filter, PickFile pick_file)
      : filter_(filter), pick_file_(std::move(pick_file)) {}
  ConfigPanel(const ConfigPanel&) = delete;
  ConfigPanel& operator=(const ConfigPanel&) = delete;
  ~ConfigPanel() = default;

  static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam);

 private:
  void Init(HWND hwnd);
  void AddControl(const wchar_t* window_class, std::string_view text,
                  DWORD style, DWORD ex_style, int id, const Place& place);
  void Browse();
  std::string GetPath() const;
  void WritePath(char* buffer, int size) const;

  HWND hwnd_ = nullptr;
  FileFilter filter_;
  PickFile pick_file_;
};

INT_PTR CALLBACK ConfigPanel::DialogProc(HWND hwnd, UINT message, WPARAM wparam,
                                         LPARAM lparam) {
  if (message == WM_INITDIALOG) {
    SetWindowLongPtrW(hwnd, DWLP_USER, lparam);
    reinterpret_cast<ConfigPanel*>(lparam)->Init(hwnd);
    return FALSE;  // REAPER's dialog keeps the focus.
  }

  // Messages before WM_INITDIALOG, and after WM_DESTROY, have no panel.
  auto* panel =
      reinterpret_cast<ConfigPanel*>(GetWindowLongPtrW(hwnd, DWLP_USER));
  if (panel == nullptr) {
    return FALSE;
  }
  switch (message) {
    case WM_COMMAND:
      if (LOWORD(wparam) == kBrowseId && HIWORD(wparam) == BN_CLICKED) {
        panel->Browse();
        return TRUE;
      }
      break;
    case kWriteConfigMessage:
      panel->WritePath(reinterpret_cast<char*>(lparam),
                       static_cast<int>(wparam));
      return TRUE;
    case WM_DESTROY:
      SetWindowLongPtrW(hwnd, DWLP_USER, 0);
      delete panel;
      break;
  }
  return FALSE;
}

void ConfigPanel::Init(HWND hwnd) {
  hwnd_ = hwnd;
  AddControl(L"STATIC", "Config file:", SS_LEFT, 0, 0, {6, 7, 50, 8});
  AddControl(L"EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
             kPathId, {58, 5, 160, 12});
  AddControl(L"BUTTON", "Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, kBrowseId,
             {222, 4, 40, 14});
}

void ConfigPanel::AddControl(const wchar_t* window_class, std::string_view text,
                             DWORD style, DWORD ex_style, int id,
                             const Place& place) {
  RECT rect = {place.x, place.y, place.x + place.width, place.y + place.height};
  MapDialogRect(hwnd_, &rect);
  HWND control = CreateWindowExW(
      ex_style, window_class, AsWide(gb::ToUtf16(text)),
      WS_CHILD | WS_VISIBLE | style, rect.left, rect.top,
      rect.right - rect.left, rect.bottom - rect.top, hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModule(), nullptr);
  SendMessageW(control, WM_SETFONT, SendMessageW(hwnd_, WM_GETFONT, 0, 0),
               FALSE);
}

void ConfigPanel::Browse() {
  if (std::optional<std::string> path =
          pick_file_(GetAncestor(hwnd_, GA_ROOT), filter_, GetPath())) {
    SetPath(hwnd_, *path);
  }
}

std::string ConfigPanel::GetPath() const {
  HWND edit = GetDlgItem(hwnd_, kPathId);
  std::u16string text(GetWindowTextLengthW(edit) + 1, u'\0');
  text.resize(
      GetWindowTextW(edit, AsWide(text), static_cast<int>(text.size())));
  return gb::ToUtf8(text);
}

void ConfigPanel::WritePath(char* buffer, int size) const {
  if (buffer == nullptr || size <= 0) {
    return;
  }
  const std::string path = GetPath();
  const int length = std::min(static_cast<int>(path.size()), size - 1);
  if (length < static_cast<int>(path.size())) {
    LOG(ERROR) << "The config file's path is too long for REAPER, which keeps "
               << length << " bytes of it: " << path;
  }
  std::memcpy(buffer, path.data(), length);
  buffer[length] = '\0';
}

}  // namespace

std::optional<std::string> PickFileToOpen(HWND owner, const FileFilter& filter,
                                          std::string_view path) {
  // Each part ends in a null, and the list in another, which c_str() adds.
  constexpr std::string_view kEnd("\0", 1);
  const std::u16string filters = gb::ToUtf16(
      absl::StrCat(filter.description, " (", filter.pattern, ")", kEnd,
                   filter.pattern, kEnd, "All files (*.*)", kEnd, "*.*", kEnd));

  // The dialog starts at the path, unless it is too long.
  std::u16string file = gb::ToUtf16(path);
  if (static_cast<int>(file.size()) >= kMaxPathLength) {
    file.clear();
  }
  file.resize(kMaxPathLength);
  OPENFILENAMEW open = {.lStructSize = sizeof(OPENFILENAMEW),
                        .hwndOwner = owner,
                        .lpstrFilter = AsWide(filters),
                        .lpstrFile = AsWide(file),
                        .nMaxFile = static_cast<DWORD>(file.size()),
                        .Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST |
                                 OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                                 OFN_NOCHANGEDIR};
  BOOL picked = GetOpenFileNameW(&open);
  if (!picked && CommDlgExtendedError() == FNERR_INVALIDFILENAME) {
    // The dialog can't start at the path, so it starts with none.
    file[0] = u'\0';
    picked = GetOpenFileNameW(&open);
  }
  if (!picked) {
    if (const DWORD error = CommDlgExtendedError(); error != 0) {
      LOG(ERROR) << "The file picker failed, with error " << error;
    }
    return std::nullopt;
  }
  return gb::ToUtf8(std::u16string_view(file.c_str()));
}

HWND CreateConfigPanel(HWND parent, std::string_view path,
                       const FileFilter& filter, PickFile pick_file) {
  auto panel = std::make_unique<ConfigPanel>(filter, std::move(pick_file));
  HWND hwnd = CreateDialogIndirectParamW(GetModule(), &kPanelTemplate.dialog,
                                         parent, &ConfigPanel::DialogProc,
                                         reinterpret_cast<LPARAM>(panel.get()));
  if (hwnd == nullptr) {
    LOG(ERROR) << "The config panel can't be created: "
               << gb::GetWindowsError();
    return nullptr;
  }
  panel.release();  // The window deletes it when it is destroyed.
  SetPath(hwnd, path);
  return hwnd;
}

}  // namespace jpr
