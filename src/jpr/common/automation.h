// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "gb/base/flags.h"

namespace jpr {

// A REAPER track automation mode. The values match the track's I_AUTOMODE.
enum class AutoMode {
  kTrimRead,
  kRead,
  kTouch,
  kWrite,
  kLatch,

  // REAPER supports this mode, but it is not documented in the SDK.
  kLatchPreview,
};

// The number of AutoMode values. AutoMode values are contiguous starting at
// zero.
inline constexpr int kAutoModeCount = 6;

// A set of automation modes.
using AutoModes = gb::Flags<AutoMode>;

// REAPER's global automation override for the current project, which makes
// every track behave as if it were in one mode, without changing the tracks'
// own modes. The values match REAPER's, and the modes match AutoMode.
enum class AutoOverride {
  kNone = -1,
  kTrimRead = 0,
  kRead,
  kTouch,
  kWrite,
  kLatch,
  kLatchPreview,

  // Ignores all automation. The SDK documents this as 5, but REAPER uses 6.
  kBypass,
};

// Returns the global automation override for the current project.
AutoOverride GetAutoOverride();

// Sets the global automation override for the current project. This does not
// add an undo point.
void SetAutoOverride(AutoOverride auto_override);

}  // namespace jpr
