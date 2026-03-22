// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>

namespace jpr {

using Modifiers = uint64_t;

// Standard modifier bit masks, used in common code for "UI" behavior.
inline constexpr Modifiers kModShift = 1;
inline constexpr Modifiers kModCtrl = 2;  // Command on Mac
inline constexpr Modifiers kModAlt = 4;   // Option on Mac
inline constexpr Modifiers kModOpt = 8;   // Control on Mac, Windows on Windows

// The first available "user" modifier bit mask that can be used for custom
// modifiers in property
inline constexpr Modifiers kModUserStart = 16;

// Returns the current state of the modifier keys (shift, ctrl, alt, etc.) as a
// bitmask. The standard modifier bit masks are defined above, but additional
// bits may be used for other modifiers as needed.
Modifiers GetModifiers();

// Resets the state of all modifiers to off.
void ResetModifiers();

// Sets the state of the specified modifiers to on or off. This is used to
// simulate modifier key presses for "UI" behavior or for use in property
// mappings.
void SetModifiers(Modifiers modifiers, bool on);

// Returns true if all of the specified modifiers are currently on, and false
// otherwise.
bool AreModifiersOn(Modifiers modifiers);

}  // namespace jpr
