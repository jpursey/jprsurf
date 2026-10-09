// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/automation.h"

namespace jpr {

// The IDs of REAPER's actions that tests run, which REAPER and the fake both
// have (see AddReaperActions()).

inline constexpr int kRepeatAction = 1068;  // Transport: Toggle repeat
inline constexpr int kUndoAction = 40029;   // Edit: Undo
inline constexpr int kRedoAction = 40030;   // Edit: Redo

// The automation mode actions, which set the selected tracks' automation mode:
// Trim/Read to Latch, each setting the AutoMode of its offset from the first.
inline constexpr int kFirstAutoModeAction = 40400;
inline constexpr int kLastAutoModeAction = 40404;
static_assert(kLastAutoModeAction - kFirstAutoModeAction ==
              static_cast<int>(AutoMode::kLatch));

// Ruler time units (see kRulerModes in reaper_action_list.h).
inline constexpr int kRulerMeasuresBeats = 40367;
inline constexpr int kRulerSeconds = 40368;
inline constexpr int kRulerSecondaryNone = 42360;
inline constexpr int kRulerSecondaryFrames = 42364;  // Hours:...:Frames.

}  // namespace jpr
