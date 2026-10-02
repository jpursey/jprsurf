// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/reset_test_key.h"

namespace jpr {

// For tests only: resets all of the common library's process state, as it was
// when the extension loaded: TrackCache, ContinuousUndo, the registered control
// surface type, the ruler modes, and the modifiers. No control surface may
// exist.
//
// A new global in the common library is added here, so this stays the one list
// of them.
void ResetCommonState(ResetTestKey key);

}  // namespace jpr
