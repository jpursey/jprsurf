// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output.h"

#include <algorithm>

namespace jpr {

void ControlCValueOutput::SetValue(double value) {
  OnValueChanged(std::clamp(value, 0.0, 1.0));
}

void ControlDValueOutput::SetValue(int value) {
  OnValueChanged(std::clamp(value, 0, max_value_));
}

void ControlTextOutput::SetText(std::string_view text) { OnTextChanged(text); }

void ControlColorOutput::SetColor(Color color) { OnColorChanged(color); }

}  // namespace jpr
