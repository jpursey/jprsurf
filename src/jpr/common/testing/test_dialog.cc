// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/test_dialog.h"

// Last, as its macros break code included after it.
#include <windows.h>

namespace jpr {

TestDialog::TestDialog()
    : hwnd_(CreateWindowExW(0, L"STATIC", L"Test dialog", WS_OVERLAPPEDWINDOW,
                            0, 0, 600, 400, nullptr, nullptr, nullptr,
                            nullptr)) {}

TestDialog::~TestDialog() { DestroyWindow(hwnd_); }

}  // namespace jpr
