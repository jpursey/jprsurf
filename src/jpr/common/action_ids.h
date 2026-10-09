// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/automation.h"

namespace jpr {

//==============================================================================
// REAPER's action IDs
//
// The IDs of REAPER's built in actions, for Main_OnCommand() and
// GetToggleCommandState().
//==============================================================================

inline constexpr int kRepeatAction = 1068;  // Transport: Toggle repeat
inline constexpr int kUndoAction = 40029;   // Edit: Undo
inline constexpr int kRedoAction = 40030;   // Edit: Redo

//------------------------------------------------------------------------------
// Automation modes
//------------------------------------------------------------------------------

// Returns the action that sets the selected tracks' automation mode to `mode`,
// "Automation: Set track automation mode to <mode>".
constexpr int GetAutoModeAction(AutoMode mode) {
  switch (mode) {
    case AutoMode::kTrimRead:
      return 40400;
    case AutoMode::kRead:
      return 40401;
    case AutoMode::kTouch:
      return 40402;
    case AutoMode::kWrite:
      return 40403;
    case AutoMode::kLatch:
      return 40404;
    case AutoMode::kLatchPreview:
      return 42023;
  }
  return 0;
}

//------------------------------------------------------------------------------
// Ruler time units
//------------------------------------------------------------------------------

// The ruler's time units, "View: Time unit for ruler: <unit>", a radio group.
// Frames is "Hours:Minutes:Seconds:Frames".
inline constexpr int kRulerMeasuresBeatsMinimal = 41916;
inline constexpr int kRulerMeasuresBeats = 40367;
inline constexpr int kRulerMeasuresFractions = 43205;
inline constexpr int kRulerMinutesSecondsMinimal = 43204;
inline constexpr int kRulerMinutesSeconds = 40365;
inline constexpr int kRulerSeconds = 40368;
inline constexpr int kRulerFrames = 40370;
inline constexpr int kRulerAbsoluteFrames = 41973;
inline constexpr int kRulerSamples = 40369;

// The ruler's secondary time units, "View: Secondary time unit for ruler:
// <unit>", a radio group of their own.
inline constexpr int kRulerSecondaryNone = 42360;
inline constexpr int kRulerSecondaryMinutesSecondsMinimal = 43705;
inline constexpr int kRulerSecondaryMinutesSeconds = 42361;
inline constexpr int kRulerSecondarySeconds = 42362;
inline constexpr int kRulerSecondaryFrames = 42364;
inline constexpr int kRulerSecondaryAbsoluteFrames = 42365;
inline constexpr int kRulerSecondarySamples = 42363;

}  // namespace jpr
