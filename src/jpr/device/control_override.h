// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "jpr/device/control.h"
#include "jpr/device/spec_line.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

// The inputs and outputs of one control that a unit doesn't use, such as a
// fader whose touch sensing is broken. A device drops them from the control as
// it adds it (see Device).
struct ControlOverride {
  friend bool operator==(const ControlOverride&,
                         const ControlOverride&) = default;

  // The control's path in its device's catalog, such as "strip[2].fader".
  std::string control;

  Control::Inputs inputs;    // The inputs it doesn't use.
  Control::Outputs outputs;  // The outputs it doesn't use.
  SpecLine line;             // Where it was written, for errors.
};

// Returns true if every override names a control in the catalog, at most once,
// and drops something, but only inputs and outputs that control has. Otherwise,
// an error is added to `errors` (if provided) for each problem found, starting
// with the override's line (see GetSpecError()), then `device_name`, which
// must not be empty, then the override's control, such as `xtouch_ext:
// strip[2].fader: has no delta input`.
bool ValidateControlOverrides(const WidgetShape& catalog,
                              absl::Span<const ControlOverride> overrides,
                              std::string_view device_name,
                              std::vector<std::string>* errors);

// Returns the overrides by the name of their control, which is its path's text
// as WidgetPath::ToString() writes it, however the override writes it. The
// overrides of one control are combined, where the first was written, and an
// override whose control isn't a path is left out, as it names no control.
absl::flat_hash_map<std::string, ControlOverride> GetOverridesByControl(
    absl::Span<const ControlOverride> overrides);

}  // namespace jpr
