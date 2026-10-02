// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

namespace jpr {

//==============================================================================
// TestReset
//
// For tests only: registers a function that resets process state, as it was
// when the extension loaded. The fake REAPER (FakeReaper, in
// jpr/common/testing) calls every registered reset when it is created and when
// it is destroyed, and nothing else can.
//
// Define one in the .cc file that holds the state: at namespace scope for a
// file-local global, or as a private static member for a class's own state,
// whose initializer can reach the class's private members:
//
//   const TestReset kResetRulerModes([] { g_last_ruler_modes = {}; });
//
//   const TestReset TrackCache::s_test_reset_([] {
//     delete s_instance_;
//     s_instance_ = nullptr;
//   });
//
// It is then registered whenever the state is linked into a binary, by any
// library. It must be in the same .cc file as the state: a binary only links
// the files of a library that it uses, so a file that held nothing but
// registrations would be left out.
//==============================================================================

class TestReset final {
 public:
  using Function = void (*)();

  explicit TestReset(Function reset);
  TestReset(const TestReset&) = delete;
  TestReset& operator=(const TestReset&) = delete;
  ~TestReset() = default;

 private:
  friend class FakeReaper;

  // Calls every registered reset.
  static void ResetAll();
};

}  // namespace jpr
