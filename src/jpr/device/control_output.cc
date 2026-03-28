// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output.h"

#include <algorithm>

namespace jpr {

//==============================================================================
// ControlCValueOutput
//==============================================================================

void ControlCValueOutput::SetValue(double value, int mode) {
  OnValueChanged(std::clamp(value, 0.0, 1.0),
                 std::clamp(mode, 0, GetModeCount() - 1));
}

//==============================================================================
// ControlDValueOutput
//==============================================================================

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

//==============================================================================
// ControlTextOutput
//==============================================================================

void ControlTextOutput::SetText(std::string_view text, int mode) {
  OnTextChanged(text, std::clamp(mode, 0, GetModeCount() - 1));
}

void ControlTextOutput::SetTimelineText(TimelinePosition position, int mode) {
  mode = std::clamp(mode, 0, 4);
  TimelineMode timeline_mode = TimelineMode::kBeats;
  switch (mode) {
    case 0:
      timeline_mode = GetRulerMode();
      break;
    case 1:
      timeline_mode = TimelineMode::kBeats;
      break;
    case 2:
      timeline_mode = TimelineMode::kTime;
      break;
    case 3:
      timeline_mode = TimelineMode::kFrames;
      break;
    case 4:
      timeline_mode = TimelineMode::kSamples;
      break;
  }
  OnTimelineTextChanged(position, timeline_mode);
}

void ControlTextOutput::OnTimelineTextChanged(TimelinePosition position,
                                              TimelineMode mode) {
  SetText(position.ToString(mode), /*mode=*/0);
}

//==============================================================================
// ControlColorOutput
//==============================================================================

void ControlColorOutput::SetColor(Color color, int mode) {
  OnColorChanged(color, std::clamp(mode, 0, GetModeCount() - 1));
}

}  // namespace jpr
