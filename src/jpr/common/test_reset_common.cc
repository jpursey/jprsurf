// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/test_reset_common.h"

#include "jpr/common/control_surface.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/timeline.h"
#include "jpr/common/track_cache.h"
#include "jpr/common/undo.h"

namespace jpr {

void ResetCommonState(ResetTestKey key) {
  ControlSurface::Reset(key);
  TrackCache::Reset(key);
  ContinuousUndo::Reset(key);
  ResetRulerModes(key);
  ResetModifiers();
}

}  // namespace jpr
