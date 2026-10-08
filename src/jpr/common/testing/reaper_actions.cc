// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/reaper_actions.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"

namespace jpr {

namespace {

// One of REAPER's actions, with its text and toggle state.
struct Action {
  int id;
  const char* text;
  int toggle_state;  // -1 if it isn't a toggle.
};

constexpr Action kActions[] = {
    {1007, "Transport: Play", 0},
    {1013, "Transport: Record", 0},
    {1016, "Transport: Stop", -1},
    {1068, "Transport: Toggle repeat", 0},
    {40013, "Insert click source", -1},
    {40020, "Time selection: Remove (unselect) time selection and loop points",
     -1},
    {40026, "File: Save project", -1},
    {40029, "Edit: Undo", -1},
    {40030, "Edit: Redo", -1},
    {40073, "Transport: Play/pause", -1},
    {40142, "Insert empty item", -1},
    {40172, "Markers: Go to previous marker/project start", -1},
    {40173, "Markers: Go to next marker/project end", -1},
    {40214, "Insert new MIDI item...", -1},
    {40230, "Move edit cursor to start of current/previous beat", -1},
    {40231, "Move edit cursor to start of next beat", -1},
    {40289, "Item: Unselect (clear selection of) all items", -1},
    {40340, "Track: Unsolo all tracks", -1},
    {40364, "Options: Toggle metronome", 0},
    {40400, "Automation: Set track automation mode to trim/read", -1},
    {40401, "Automation: Set track automation mode to read", -1},
    {40402, "Automation: Set track automation mode to touch", -1},
    {40403, "Automation: Set track automation mode to write", -1},
    {40404, "Automation: Set track automation mode to latch", -1},
    {40745, "Options: Solo in front", 0},
    {41040, "Move edit cursor to start of next measure", -1},
    {41041, "Move edit cursor to start of current/previous measure", -1},
    {41895,
     "File: Save new version of project (automatically increment project "
     "name)",
     -1},
};

// The ruler's time unit actions, and its secondary ones, and the one of each
// that is on.
constexpr int kRulerModes[] = {41916, 40367, 43205, 43204, 40365,
                               40368, 40370, 41973, 40369};
constexpr int kRulerSecondaryModes[] = {42360, 43705, 42361, 42362,
                                        42364, 42365, 42363};
constexpr int kRulerModeOn = 40367;           // Measure.Beats.
constexpr int kRulerSecondaryModeOn = 42360;  // None.

// Returns the ruler group `id` is in, or an empty span if it is in neither.
absl::Span<const int> FindRulerGroup(int id) {
  if (std::ranges::find(kRulerModes, id) != std::end(kRulerModes)) {
    return kRulerModes;
  }
  if (std::ranges::find(kRulerSecondaryModes, id) !=
      std::end(kRulerSecondaryModes)) {
    return kRulerSecondaryModes;
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
void AddRulerGroup(FakeReaper* reaper, absl::Span<const int> group, int on) {
  for (int id : group) {
    reaper->AddCommand(
        {.id = id, .toggle_state = (id == on ? 1 : 0), .on_run = [reaper, id] {
           SelectRulerMode(reaper, id);
         }});
  }
}

}  // namespace

void AddReaperActions(FakeReaper* reaper) {
  for (const Action& action : kActions) {
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
  AddRulerGroup(reaper, kRulerModes, kRulerModeOn);
  AddRulerGroup(reaper, kRulerSecondaryModes, kRulerSecondaryModeOn);
}

void SelectRulerMode(FakeReaper* reaper, int id) {
  const absl::Span<const int> group = FindRulerGroup(id);
  if (group.empty()) {
    ADD_FAILURE() << "SelectRulerMode() was given action " << id
                  << ", which isn't a ruler time unit";
    return;
  }
  for (int other : group) {
    reaper->SetToggleState(other, other == id ? 1 : 0);
  }
}

}  // namespace jpr
