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

}  // namespace jpr
