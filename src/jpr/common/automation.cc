// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/automation.h"

#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

AutoOverride last_auto_override = AutoOverride::kBypass;

void RememberAutoOverride(AutoOverride auto_override) {
  if (auto_override != AutoOverride::kNone) {
    last_auto_override = auto_override;
  }
}

}  // namespace

AutoOverride GetAutoOverride() {
  const auto auto_override =
      static_cast<AutoOverride>(GetGlobalAutomationOverride());
  RememberAutoOverride(auto_override);
  return auto_override;
}

void SetAutoOverride(AutoOverride auto_override) {
  SetGlobalAutomationOverride(static_cast<int>(auto_override));
  RememberAutoOverride(auto_override);
}

AutoOverride GetLastAutoOverride() { return last_auto_override; }

}  // namespace jpr
