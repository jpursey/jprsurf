// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>

#include "jpr/common/testing/fake_project.h"

namespace jpr {

// Returns `project` as the text of an RPP file, which REAPER opens as the same
// project, so a test can start from it in REAPER as it does under the fake.
//
// It holds the tracks, in order, with their folders, GUIDs, names, colors,
// volumes and pans, mute and solo, rec arm, selection, automation modes,
// groups, and visibility; each send and hardware output; the selected media
// items; the edit cursor; and the automation override. The project has the
// fake's tempo (FakeProject::kBeatsPerMinute).
//
// A project opens stopped, clean, and with nothing to undo or redo, so
// anything else fails the test, naming it, as do peaks. So does state REAPER's
// master can't have: a name of its own, solo, rec arm, a group, or being hidden
// (which only REAPER's preferences do). The text is still returned, without
// it.
std::string WriteProjectFile(const FakeProject& project);

}  // namespace jpr
