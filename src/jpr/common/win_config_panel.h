// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Last, as its macros break code included after it.
#include <windows.h>

namespace jpr {

// What REAPER sends a surface type's panel in its control surface dialog on OK,
// for the config string: lParam is a buffer, and wParam its size.
constexpr UINT kWriteConfigMessage = WM_USER + 1024;

// The files a file picker offers: those `pattern` matches, described as
// `description`.
struct FileFilter {
  std::string description;  // Such as "JPRSurf config".
  std::string pattern;      // Such as "*.jprsurf".
};

// Asks the user for a file to open, in a dialog owned by `owner`, which offers
// the files `filter` matches, starting at `path` (which may be empty). Returns
// the file's path, or nullopt if the user cancels.
using PickFile = std::function<std::optional<std::string>(
    HWND owner, const FileFilter& filter, std::string_view path)>;

// Picks a file with Windows' own Open dialog, which also offers all files.
std::optional<std::string> PickFileToOpen(HWND owner, const FileFilter& filter,
                                          std::string_view path);

// Creates the panel REAPER's control surface dialog shows for a surface whose
// config string is a file's path (see ControlSurface::Type): the path, which
// the user may edit, with a Browse button that replaces it with a file
// `pick_file` picks.
//
// REAPER asks the open surface for its config string (GetConfigString()), and
// gives it to its type's ShowConfig(), which creates the panel as a hidden
// child of `parent`, REAPER's dialog. REAPER shows the panel. On OK, REAPER
// sends it kWriteConfigMessage, and the panel writes the path in its buffer
// (truncated, and logging an error, if it doesn't fit). REAPER then destroys
// the panel, destroys the surface, and creates one with that config string,
// even if it is unchanged. On Cancel, REAPER only destroys the panel. Returns
// null (and logs an error) if the panel can't be created.
HWND CreateConfigPanel(HWND parent, std::string_view path,
                       const FileFilter& filter,
                       PickFile pick_file = PickFileToOpen);

}  // namespace jpr
