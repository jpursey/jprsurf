// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/deferred_refresh.h"

#include <utility>

#include "jpr/common/reaper_api.h"
#include "jpr/common/test_reset.h"

namespace jpr {

namespace {

// How many DeferredRefresh exist, outside the action RunAction() is running.
int g_deferred_depth = 0;

const TestReset kResetDeferredDepth([] { g_deferred_depth = 0; });

}  // namespace

DeferredRefresh::DeferredRefresh() {
  PreventUIRefresh(1);
  ++g_deferred_depth;
}

DeferredRefresh::~DeferredRefresh() {
  --g_deferred_depth;
  PreventUIRefresh(-1);
}

void RunAction(int command_id) {
  // The action starts at a depth of none, so a DeferredRefresh in REAPER's
  // calls back from inside it, and an action run from one, see where they are.
  const int depth = std::exchange(g_deferred_depth, 0);
  if (depth > 0) {
    PreventUIRefresh(-depth);
  }
  Main_OnCommand(command_id, 0);
  if (depth > 0) {
    PreventUIRefresh(depth);
  }
  g_deferred_depth = depth;
}

}  // namespace jpr
