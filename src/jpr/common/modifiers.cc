// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/modifiers.h"

namespace jpr {

namespace {

Modifiers g_modifiers = 0;

}  // namespace

Modifiers GetModifiers() { return g_modifiers; }

void ResetModifiers() { g_modifiers = 0; }

void SetModifiers(Modifiers modifiers, bool on) {
  if (on) {
    g_modifiers |= modifiers;
  } else {
    g_modifiers &= ~modifiers;
  }
}

bool AreModifiersOn(Modifiers modifiers) {
  return (g_modifiers & modifiers) == modifiers;
}

}  // namespace jpr
