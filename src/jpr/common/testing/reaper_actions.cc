// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/reaper_actions.h"

#include <algorithm>
#include <utility>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/testing/reaper_action_list.h"

namespace jpr {

namespace {

// Returns the ruler group `id` is in, or an empty span if it is in neither.
absl::Span<const RulerMode> FindRulerGroup(int id) {
  for (absl::Span<const RulerMode> group : kRulerGroups) {
    if (std::ranges::find(group, id, &RulerMode::id) != group.end()) {
      return group;
    }
  }
  return {};
}

// Sets the automation mode of every selected track in the current project,
// the master's too.
void SetSelectedAutoModes(FakeReaper* reaper, AutoMode mode) {
  for (FakeTrack* track :
       reaper->GetProject().GetSelectedTracks(/*include_master=*/true)) {
    track->auto_mode = static_cast<int>(mode);
  }
}

// Adds the ruler actions `group`, with `on` on.
void AddRulerGroup(FakeReaper* reaper, absl::Span<const RulerMode> group,
                   int on) {
  for (const RulerMode& mode : group) {
    const int id = mode.id;
    reaper->AddCommand(
        {.id = id,
         .text = mode.text,
         .toggle_state = (id == on ? 1 : 0),
         .on_run = [reaper, id] { SelectRulerMode(reaper, id); }});
  }
}

}  // namespace

void AddReaperActions(FakeReaper* reaper) {
  for (const ReaperAction& action : kReaperActions) {
    FakeCommand command = {.id = action.id,
                           .text = action.text,
                           .toggle_state = action.toggle_state};
    if (action.id >= kFirstAutoModeAction && action.id <= kLastAutoModeAction) {
      const AutoMode mode =
          static_cast<AutoMode>(action.id - kFirstAutoModeAction);
      command.on_run = [reaper, mode] { SetSelectedAutoModes(reaper, mode); };
    }
    reaper->AddCommand(std::move(command));
  }
  AddRulerGroup(reaper, kRulerModes, kRulerMeasuresBeats);
  AddRulerGroup(reaper, kRulerSecondaryModes, kRulerSecondaryNone);
}

void SelectRulerMode(FakeReaper* reaper, int id) {
  const absl::Span<const RulerMode> group = FindRulerGroup(id);
  if (group.empty()) {
    ADD_FAILURE() << "SelectRulerMode() was given action " << id
                  << ", which isn't a ruler time unit";
    return;
  }
  for (const RulerMode& other : group) {
    reaper->SetToggleState(other.id, other.id == id ? 1 : 0);
  }
}

}  // namespace jpr
