// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/types/span.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"

namespace jpr {

// One of REAPER's actions, with REAPER's text for it, and its toggle state in
// a new project with REAPER's default preferences: -1 if it isn't a toggle.
struct ReaperAction {
  int id;
  const char* text;
  int toggle_state;
};

// One of the ruler's time unit actions, with REAPER's text for it. Which is on
// is a preference.
struct RulerMode {
  int id;
  const char* text;
};

// The actions AddReaperActions() adds, apart from the ruler's, so contract
// tests can check each against REAPER.
inline constexpr ReaperAction kReaperActions[] = {
    {1007, "Transport: Play", 0},
    {1013, "Transport: Record", 0},
    {1016, "Transport: Stop", -1},
    {kRepeatAction, "Transport: Toggle repeat", 0},
    {40013, "Insert click source", -1},
    {40020, "Time selection: Remove (unselect) time selection and loop points",
     -1},
    {40026, "File: Save project", -1},
    {kUndoAction, "Edit: Undo", -1},
    {kRedoAction, "Edit: Redo", -1},
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
    {GetAutoModeAction(AutoMode::kTrimRead),
     "Automation: Set track automation mode to trim/read", -1},
    {GetAutoModeAction(AutoMode::kRead),
     "Automation: Set track automation mode to read", -1},
    {GetAutoModeAction(AutoMode::kTouch),
     "Automation: Set track automation mode to touch", -1},
    {GetAutoModeAction(AutoMode::kWrite),
     "Automation: Set track automation mode to write", -1},
    {GetAutoModeAction(AutoMode::kLatch),
     "Automation: Set track automation mode to latch", -1},
    {40745, "Options: Solo in front", 0},
    {41040, "Move edit cursor to start of next measure", -1},
    {41041, "Move edit cursor to start of current/previous measure", -1},
    {41895,
     "File: Save new version of project (automatically increment project "
     "name)",
     -1},
    {GetAutoModeAction(AutoMode::kLatchPreview),
     "Automation: Set track automation mode to latch preview", -1},
};

// The ruler's time unit actions, and its secondary ones, each a radio group.
inline constexpr RulerMode kRulerModes[] = {
    {kRulerMeasuresBeatsMinimal,
     "View: Time unit for ruler: Measures.Beats (minimal)"},
    {kRulerMeasuresBeats, "View: Time unit for ruler: Measures.Beats"},
    {kRulerMeasuresFractions, "View: Time unit for ruler: Measures.Fractions"},
    {kRulerMinutesSecondsMinimal,
     "View: Time unit for ruler: Minutes:Seconds (minimal)"},
    {kRulerMinutesSeconds, "View: Time unit for ruler: Minutes:Seconds"},
    {kRulerSeconds, "View: Time unit for ruler: Seconds"},
    {kRulerFrames, "View: Time unit for ruler: Hours:Minutes:Seconds:Frames"},
    {kRulerAbsoluteFrames, "View: Time unit for ruler: Absolute frames"},
    {kRulerSamples, "View: Time unit for ruler: Samples"},
};
inline constexpr RulerMode kRulerSecondaryModes[] = {
    {kRulerSecondaryNone, "View: Secondary time unit for ruler: None"},
    {kRulerSecondaryMinutesSecondsMinimal,
     "View: Secondary time unit for ruler: Minutes:Seconds (minimal)"},
    {kRulerSecondaryMinutesSeconds,
     "View: Secondary time unit for ruler: Minutes:Seconds"},
    {kRulerSecondarySeconds, "View: Secondary time unit for ruler: Seconds"},
    {kRulerSecondaryFrames,
     "View: Secondary time unit for ruler: Hours:Minutes:Seconds:Frames"},
    {kRulerSecondaryAbsoluteFrames,
     "View: Secondary time unit for ruler: Absolute frames"},
    {kRulerSecondarySamples, "View: Secondary time unit for ruler: Samples"},
};
inline constexpr absl::Span<const RulerMode> kRulerGroups[] = {
    kRulerModes, kRulerSecondaryModes};

}  // namespace jpr
