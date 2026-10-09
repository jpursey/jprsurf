// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/testing/fake_reaper.h"

namespace jpr {

//==============================================================================
// REAPER's actions
//
// AddReaperActions() adds a set of REAPER's actions to a fake REAPER (see
// FakeReaper::AddCommand()), each with REAPER's text and toggle state (see
// reaper_action_list.h, which contract tests check against REAPER). The
// fake holds REAPER's state, not its behavior, so this is the opt-in for tests
// that need REAPER's actions to exist, and to do what they do, as
// SurfaceNotifier is for REAPER's calls to a control surface. Those whose
// effect tests need change what REAPER's do:
// - The automation mode actions set the mode of every selected track in the
//   current project, the master's too, with an undo point if any changed.
// - Edit: Undo and Edit: Redo undo and redo the current project's undo points
//   (see FakeProject::Undo()).
// - The ruler's time unit actions, and its secondary ones (see timeline.cc),
//   are each a radio group (see SelectRulerMode()), with Measure.Beats on, and
//   no secondary unit, where REAPER's default preferences have
//   Minutes:Seconds.
// A test gives another action a handler with FakeReaper::SetCommandHandler().
//
// Brittleness: an action that isn't in the set doesn't exist in REAPER, as
// far as a test that uses it can tell, so code that starts using another
// action needs it added to reaper_action_list.h.
//==============================================================================

// Adds the actions to `reaper`, which none of them may have been added to.
void AddReaperActions(FakeReaper* reaper);

// Turns the ruler time unit action `id` on, and the rest of its group off, as
// running it does, or picking it from the ruler's menu in REAPER (which runs
// nothing). AddReaperActions() must have added it.
void SelectRulerMode(FakeReaper* reaper, int id);

}  // namespace jpr
