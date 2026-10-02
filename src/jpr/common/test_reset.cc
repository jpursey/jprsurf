// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/test_reset.h"

#include <vector>

#include "absl/base/no_destructor.h"

namespace jpr {

namespace {

// Returns every registered reset. This is a function static, so it exists
// before any TestReset, whichever file is initialized first.
std::vector<TestReset::Function>& GetResets() {
  static absl::NoDestructor<std::vector<TestReset::Function>> s_resets;
  return *s_resets;
}

}  // namespace

TestReset::TestReset(Function reset) { GetResets().push_back(reset); }

void TestReset::ResetAll() {
  for (Function reset : GetResets()) {
    reset();
  }
}

}  // namespace jpr
