// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "gb/file/file_system.h"

namespace jpr {

// Returns the file system JPRSurf writes its files through, which lives until
// the process exits. It starts with no protocols. Whatever loads JPRSurf
// registers one under each name its files are written to, such as "profile",
// before they are written, and they are named by it:
// "profile:/jprsurf_profile.txt".
//
// The fake REAPER (FakeReaper) resets it to no protocols, replacing it, so a
// test registers what it needs after creating the fake, usually a
// gb::MemoryFileProtocol, and nothing keeps the pointer across a reset.
gb::FileSystem* GetFileSystem();

}  // namespace jpr
