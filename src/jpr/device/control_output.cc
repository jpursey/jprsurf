// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output.h"

#include <algorithm>

namespace jpr {

void ControlCValueOutput::SetValue(double value, int mode) {
  OnValueChanged(std::clamp(value, 0.0, 1.0),
                 std::clamp(mode, 0, GetModeCount() - 1));
}

ControlDValueOutput::ControlDValueOutput(int max_value)
    : ControlOutput(1), max_values_({max_value}) {}

ControlDValueOutput::ControlDValueOutput(absl::Span<const int> max_values)
    : ControlOutput(static_cast<int>(max_values.size())),
      max_values_(max_values.begin(), max_values.end()) {}

int ControlDValueOutput::GetMaxValue(int mode) const {
  mode = std::clamp(mode, 0, GetModeCount() - 1);
  return max_values_[mode];
}

void ControlDValueOutput::SetValue(int value, int mode) {
  mode = std::clamp(mode, 0, GetModeCount() - 1);
  OnValueChanged(std::clamp(value, 0, max_values_[mode]), mode);
}

void ControlTextOutput::SetText(std::string_view text, int mode) {
  OnTextChanged(text, std::clamp(mode, 0, GetModeCount() - 1));
}

void ControlColorOutput::SetColor(Color color, int mode) {
  OnColorChanged(color, std::clamp(mode, 0, GetModeCount() - 1));
}

}  // namespace jpr
