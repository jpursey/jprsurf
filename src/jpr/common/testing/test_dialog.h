// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

// Last, as its macros break code included after it.
#include <windows.h>

namespace jpr {

// REAPER's control surface dialog, for tests of a surface type's panel in it
// (see CreateConfigPanel() and FakeReaper::ShowSurfaceConfig()): a window that
// is never shown, which destroys the panels in it when it is destroyed.
class TestDialog final {
 public:
  TestDialog();
  TestDialog(const TestDialog&) = delete;
  TestDialog& operator=(const TestDialog&) = delete;
  ~TestDialog();

  HWND GetHwnd() const { return hwnd_; }

 private:
  HWND hwnd_ = nullptr;
};

}  // namespace jpr
