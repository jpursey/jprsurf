// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/automation.h"

#include "sdk/reaper_plugin_functions.h"

namespace jpr {

AutoOverride GetAutoOverride() {
  return static_cast<AutoOverride>(GetGlobalAutomationOverride());
}

void SetAutoOverride(AutoOverride auto_override) {
  SetGlobalAutomationOverride(static_cast<int>(auto_override));
}

}  // namespace jpr
