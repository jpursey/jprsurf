// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

namespace jpr {

//==============================================================================
// ResetTestKey
//
// Lets the fake REAPER (FakeReaper, in jpr/common/testing) reset process state
// between tests, such as TrackCache and ContinuousUndo (see
// ResetCommonState()). Each function that resets process state takes one, and
// only FakeReaper can create one, so nothing else can call them.
//==============================================================================

class ResetTestKey final {
 private:
  friend class FakeReaper;

  ResetTestKey() = default;
};

}  // namespace jpr
