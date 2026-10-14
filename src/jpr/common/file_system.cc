// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/file_system.h"

#include "jpr/common/test_reset.h"

namespace jpr {

namespace {

gb::FileSystem* g_file_system = nullptr;

const TestReset kResetFileSystem([] {
  delete g_file_system;
  g_file_system = nullptr;
});

}  // namespace

gb::FileSystem* GetFileSystem() {
  if (g_file_system == nullptr) {
    g_file_system = new gb::FileSystem;
  }
  return g_file_system;
}

}  // namespace jpr
