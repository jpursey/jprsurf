// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

namespace jpr {

//==============================================================================
// DeferredRefresh
//
// Defers REAPER's UI refresh while it exists (a PreventUIRefresh() scope):
// REAPER refreshes its UI once, when the outermost one ends, for everything
// changed meanwhile, and its calls back to the surface for a deferrable change
// (mute, solo, rec arm, selection, volume, pan, and the automation override)
// come then too. They nest.
//
// RunAction() knows how deep the deferred refreshes are only by counting these,
// so nothing else in the extension may call PreventUIRefresh().
//==============================================================================

class DeferredRefresh final {
 public:
  DeferredRefresh();
  DeferredRefresh(const DeferredRefresh&) = delete;
  DeferredRefresh& operator=(const DeferredRefresh&) = delete;
  ~DeferredRefresh();
};

// Runs REAPER's action `command_id`, outside any deferred refresh, as an
// action may open REAPER's UI (such as the Save As dialog): the refresh
// deferred so far is paid first, with REAPER's calls back for it, and
// deferring resumes after the action. Every action the extension runs goes
// through this.
void RunAction(int command_id);

}  // namespace jpr
