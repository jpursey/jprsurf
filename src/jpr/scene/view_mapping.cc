// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

void NoOpSyncFunction(ViewProperty& property, Control& control, int mode) {}

// 10dB above unity gain, which matches the X-Touch faders.
constexpr double kMaxVolume = 3.16228;

// Extracts a double from a ViewProperty::Value, returning std::nullopt if the
// value is not a double.
std::optional<double> GetDouble(
    const std::optional<ViewProperty::Value>& value) {
  if (value.has_value() && std::holds_alternative<double>(*value)) {
    return std::get<double>(*value);
  }
  return std::nullopt;
}

// Extracts a string from a ViewProperty::Value, returning std::nullopt if the
// value is not a string.
std::optional<std::string> GetString(
    const std::optional<ViewProperty::Value>& value) {
  if (value.has_value() && std::holds_alternative<std::string>(*value)) {
    return std::get<std::string>(*value);
  }
  return std::nullopt;
}

// Extracts a Color from a ViewProperty::Value, returning std::nullopt if the
// value is not a Color.
std::optional<Color> GetColor(const std::optional<ViewProperty::Value>& value) {
  if (value.has_value() && std::holds_alternative<Color>(*value)) {
    return std::get<Color>(*value);
  }
  return std::nullopt;
}

// Extracts an int from a ViewProperty::Value, returning std::nullopt if the
// value is not an int.
std::optional<int> GetInt(const std::optional<ViewProperty::Value>& value) {
  if (value.has_value() && std::holds_alternative<int>(*value)) {
    return std::get<int>(*value);
  }
  return std::nullopt;
}

// Linearly interpolates a value in [0,1] to [min,max].
double Lerp(double t, double min, double max) { return min + t * (max - min); }

// Linearly interpolates a value in [0,1] to a color between min and max.
Color LerpColor(double t, Color min, Color max) {
  return {
      static_cast<uint8_t>(min.r + t * (max.r - min.r)),
      static_cast<uint8_t>(min.g + t * (max.g - min.g)),
      static_cast<uint8_t>(min.b + t * (max.b - min.b)),
  };
}

// Toggles a double value between min and max. If the current value is not
// exactly at min or max, it toggles to whichever is closer.
double ToggleDouble(double current, double min, double max) {
  double mid = (min + max) / 2.0;
  if (current <= mid) {
    return max;
  }
  return min;
}

// Compares two ViewProperty::Value instances for equality. Returns true if both
// hold the same alternative type and their values are equal. Types that do not
// support equality (e.g. TimelinePosition) always return false.
bool PropertyValueEquals(const ViewProperty::Value& a,
                         const ViewProperty::Value& b) {
  if (a.index() != b.index()) {
    return false;
  }
  return std::visit(
      [&b](const auto& a_val) -> bool {
        using T = std::decay_t<decltype(a_val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return true;
        } else if constexpr (std::is_same_v<T, bool> ||
                             std::is_same_v<T, int> ||
                             std::is_same_v<T, double> ||
                             std::is_same_v<T, std::string> ||
                             std::is_same_v<T, Color>) {
          return a_val == std::get<T>(b);
        } else {
          return false;
        }
      },
      a);
}

}  // namespace

ViewMapping::ViewMapping(View* view, TypeFlags type, ViewProperty* property,
                         Control* control, Config config,
                         ViewProperty* mode_property)
    : type_(type),
      view_(view),
      property_(property),
      control_(control),
      config_(std::move(config)),
      mode_property_(mode_property),
      reads_property_(type.IsSet(kWriteControl)),
      write_control_(NoOpSyncFunction) {
  InitReadControl();
  InitWriteControl();
}

ViewMapping::~ViewMapping() {
  if (active_) {
    enabled_ = false;
    RefreshActive(false);
  }
}

void ViewMapping::InitReadControl() {
  if (!type_.IsSet(kReadControl)) {
    return;
  }
  if (config_.read.press_release && !control_->HasPressRelease()) {
    type_.Clear(kReadControl);
    config_.read.press_release = false;
    return;
  }
  input_config_.required_modifiers = config_.read.required_modifiers;
  input_config_.press_behavior = config_.read.press_behavior;
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      InitReadActionSyncFunction();
      return;
    case ViewProperty::Type::kToggle:
      InitReadToggleSyncFunction();
      break;
    case ViewProperty::Type::kPan:
      InitReadPanSyncFunction();
      break;
    case ViewProperty::Type::kVolume:
      InitReadVolumeSyncFunction();
      break;
    case ViewProperty::Type::kNormalized:
      InitReadNormalizedSyncFunction();
      break;
    case ViewProperty::Type::kText:
      InitReadTextSyncFunction();
      break;
    case ViewProperty::Type::kColor:
      InitReadColorSyncFunction();
      break;
    case ViewProperty::Type::kTimelinePosition:
      InitReadTimelinePositionSyncFunction();
      break;
    case ViewProperty::Type::kEnumerated:
      InitReadEnumeratedSyncFunction();
      break;
  }
}

void ViewMapping::InitReadActionSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();

  // Only press inputs make sense for triggering actions.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    // If there is a press input, we trigger the action on every press event.
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      if (control.GetPressCount(id) > 0) {
        property.RunAction();
      }
    };
    return;
  }
}

void ViewMapping::InitReadToggleSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();

  // If we are configured for press/release behavior, then we set the property
  // based on whether the control is currently pressed.
  if (config_.read.press_release) {
    input_config_.input_type = ControlInput::Type::kPress;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      property.SetBool(control.IsPressed(id));
    };
    return;
  }

  // If there is a press input, we toggle the property on each press event.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    reads_property_ = true;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      if (control.GetPressCount(id) % 2 != 0) {
        property.SetBool(!property.GetBool());
      }
    };
    return;
  }

  // If there is a delta input, we set the property to false when the delta is
  // negative, and true when the delta is positive.
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetBool(delta > 0);
      }
    };
    return;
  }

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      property.SetNormalized(control.GetValue(id));
    };
    return;
  }
}

void ViewMapping::InitReadPanSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();
  auto cfg_min = GetDouble(config_.read.property_min);
  auto cfg_max = GetDouble(config_.read.property_max);

  // Panning does not make sense for press/release behavior.
  if (config_.read.press_release) {
    return;
  }

  // The value input is preferred for pan controls, if it is available, which
  // maps [0,1] to the configured range (default [-1,1]).
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    double min = cfg_min.value_or(-1.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double value = control.GetValue(id);
      property.SetPan(Lerp(value, min, max));
    };
    return;
  }

  // If there is a delta input, we add the delta to the current pan value, and
  // clamp to the configured range (default [-1,1]).
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    double min = cfg_min.value_or(-1.0);
    double max = cfg_max.value_or(1.0);
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetPan(std::clamp(property.GetPan() + delta, min, max));
      }
    };
    return;
  }

  // If there is a press input, behavior depends on the configured range.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    double min = cfg_min.value_or(-1.0);
    double max = cfg_max.value_or(1.0);
    reads_property_ = true;
    // Three-way toggle (min, 0, max) if zero is strictly between min and max.
    if (min < 0.0 && max > 0.0) {
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        int press_count = control.GetPressCount(id) % 3;
        if (press_count == 0) {
          return;
        }
        double current_pan = property.GetPan();
        double new_pan = current_pan;
        for (; press_count > 0; --press_count) {
          if (new_pan <= min) {
            new_pan = 0.0;
          } else if (new_pan < max) {
            new_pan = max;
          } else {
            new_pan = min;
          }
        }
        if (new_pan != current_pan) {
          property.SetPan(new_pan);
        }
      };
    } else {
      // Binary toggle between min and max.
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        if (control.GetPressCount(id) % 2 != 0) {
          property.SetPan(ToggleDouble(property.GetPan(), min, max));
        }
      };
    }
    return;
  }
}

void ViewMapping::InitReadVolumeSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();
  auto cfg_min = GetDouble(config_.read.property_min);
  auto cfg_max = GetDouble(config_.read.property_max);

  // Volume does not make sense for press/release behavior.
  if (config_.read.press_release) {
    return;
  }

  // The value input is preferred for volume controls, if it is available, which
  // maps [0,1] to the configured range (default [0,kMaxVolume]).
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(kMaxVolume);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double value = control.GetValue(id);
      property.SetVolume(Lerp(value, min, max));
    };
    return;
  }

  // If there is a delta input, we add the delta to the current volume value,
  // and clamp to the configured range (default [0,kMaxVolume]).
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(kMaxVolume);
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetVolume(std::clamp(property.GetVolume() + delta, min, max));
      }
    };
    return;
  }

  // If there is a press input, we toggle between the configured min and max
  // values (default 0.0 and 1.0).
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      if (control.GetPressCount(id) % 2 != 0) {
        property.SetVolume(ToggleDouble(property.GetVolume(), min, max));
      }
    };
    return;
  }
}

void ViewMapping::InitReadNormalizedSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();
  auto cfg_min = GetDouble(config_.read.property_min);
  auto cfg_max = GetDouble(config_.read.property_max);

  // While weird, press/release behavior is technically supported for normalized
  // properties. If configured, the property is set to 1.0 when the control is
  // pressed, and set to 0.0 when the control is released.
  if (config_.read.press_release) {
    input_config_.input_type = ControlInput::Type::kPress;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetNormalized(control.IsPressed(id) ? max : min);
    };
    return;
  }

  // If there is a value input, we linearly map [0,1] to the configured range
  // (default [0,1]).
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetNormalized(Lerp(control.GetValue(id), min, max));
    };
    return;
  }

  // If there is a delta input, we add the delta to the current value and clamp
  // to the configured range (default [0,1]).
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetNormalized(
            std::clamp(property.GetNormalized() + delta, min, max));
      }
    };
    return;
  }

  // If there is a press input, we toggle between the configured min and max
  // values (default 0.0 and 1.0).
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      if (control.GetPressCount(id) % 2 != 0) {
        property.SetNormalized(
            ToggleDouble(property.GetNormalized(), min, max));
      }
    };
    return;
  }
}

void ViewMapping::InitReadTextSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();

  // While weird, press/release behavior is technically supported for text
  // properties. If configured, the property is set to the configured max text
  // value when the control is pressed, and set to the configured min text value
  // when the control is released.
  if (config_.read.press_release) {
    auto cfg_min = GetString(config_.read.property_min);
    auto cfg_max = GetString(config_.read.property_max);
    if (cfg_min.has_value() && cfg_max.has_value()) {
      input_config_.input_type = ControlInput::Type::kPress;
      std::string min = std::move(*cfg_min);
      std::string max = std::move(*cfg_max);
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        property.SetText(control.IsPressed(id) ? max : min);
      };
    }
    return;
  }

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      property.SetNormalized(control.GetValue(id));
    };
    return;
  }

  // If there is a press input and both min and max text values are configured,
  // we toggle between them.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    auto cfg_min = GetString(config_.read.property_min);
    auto cfg_max = GetString(config_.read.property_max);
    if (cfg_min.has_value() && cfg_max.has_value()) {
      input_config_.input_type = ControlInput::Type::kPress;
      std::string min = std::move(*cfg_min);
      std::string max = std::move(*cfg_max);
      reads_property_ = true;
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        if (control.GetPressCount(id) % 2 != 0) {
          property.SetText(property.GetText() == max ? min : max);
        }
      };
      return;
    }
  }

  // The other input types don't make much sense for text properties, so we
  // don't support them.
}

void ViewMapping::InitReadColorSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();
  auto cfg_min = GetColor(config_.read.property_min);
  auto cfg_max = GetColor(config_.read.property_max);

  // While weird, press/release behavior is technically supported for color
  // properties. If configured, the property is set to the configured max color
  // value when the control is pressed, and set to the configured min color
  // value when the control is released.
  if (config_.read.press_release) {
    input_config_.input_type = ControlInput::Type::kPress;
    if (cfg_min.has_value() && cfg_max.has_value()) {
      Color min = *cfg_min;
      Color max = *cfg_max;
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        property.SetColor(control.IsPressed(id) ? max : min);
      };
    } else {
      read_control_ = [](ViewProperty& property, Control& control, InputId id) {
        property.SetBool(control.IsPressed(id));
      };
    }
    return;
  }

  // If there is a value input, we linearly interpolate per-channel between
  // the configured min and max colors (default black to white).
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    if (cfg_min.has_value() || cfg_max.has_value()) {
      Color min = cfg_min.value_or(Color{0, 0, 0});
      Color max = cfg_max.value_or(Color{255, 255, 255});
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        property.SetColor(LerpColor(control.GetValue(id), min, max));
      };
    } else {
      read_control_ = [](ViewProperty& property, Control& control, InputId id) {
        property.SetNormalized(control.GetValue(id));
      };
    }
    return;
  }

  // For a delta input, we let the property convert it to and from a normalized
  // value. ReadConfig min/max are not applied for delta on color properties, as
  // per-channel clamping from a single delta doesn't have clear semantics.
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    reads_property_ = true;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetNormalized(property.GetNormalized() + delta);
      }
    };
    return;
  }

  // For a press input, we toggle between the configured min and max colors
  // if specified, otherwise we toggle the boolean representation.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    reads_property_ = true;
    if (cfg_min.has_value() && cfg_max.has_value()) {
      Color min = *cfg_min;
      Color max = *cfg_max;
      read_control_ = [min, max](ViewProperty& property, Control& control,
                                 InputId id) {
        if (control.GetPressCount(id) % 2 != 0) {
          Color current = property.GetColor();
          property.SetColor(current == max ? min : max);
        }
      };
    } else {
      read_control_ = [](ViewProperty& property, Control& control, InputId id) {
        if (control.GetPressCount(id) % 2 == 1) {
          property.SetBool(!property.GetBool());
        }
      };
    }
    return;
  }
}

void ViewMapping::InitReadTimelinePositionSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();

  // Only delta inputs make sense for timeline position (e.g. jog wheel to
  // scrub). Value and press inputs are not supported.
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    reads_property_ = true;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        TimelinePosition pos = property.GetTimelinePosition();
        pos.SetValue(pos.GetValue() + delta);
        property.SetTimelinePosition(pos);
      }
    };
    return;
  }
}

void ViewMapping::InitReadEnumeratedSyncFunction() {
  Control::Inputs inputs = control_->GetInputs();
  auto cfg_min = GetInt(config_.read.property_min);
  auto cfg_max = GetInt(config_.read.property_max);

  // If we are configured for press/release behavior, then we set the property
  // to the max value when pressed and the min value when released.
  if (config_.read.press_release) {
    input_config_.input_type = ControlInput::Type::kPress;
    int min = cfg_min.value_or(0);
    int max = cfg_max.value_or(property_->GetMaxValue());
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetInt(control.IsPressed(id) ? max : min);
    };
    return;
  }

  // If there is a value input, we linearly map [0,1] to the configured integer
  // range (default [0,GetMaxValue()]).
  if (inputs.IsSet(ControlInput::Type::kValue)) {
    input_config_.input_type = ControlInput::Type::kValue;
    int min = cfg_min.value_or(0);
    int max = cfg_max.value_or(property_->GetMaxValue());
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double value = control.GetValue(id);
      int range = max - min;
      int int_value = min + static_cast<int>(value * range + 0.5);
      property.SetInt(std::clamp(int_value, min, max));
    };
    return;
  }

  // If there is a delta input, we step the integer value up or down, clamped
  // to the configured range (default [0,GetMaxValue()]).
  if (inputs.IsSet(ControlInput::Type::kDelta)) {
    input_config_.input_type = ControlInput::Type::kDelta;
    int min = cfg_min.value_or(0);
    int max = cfg_max.value_or(property_->GetMaxValue());
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        int step = delta > 0 ? 1 : -1;
        property.SetInt(std::clamp(property.GetInt() + step, min, max));
      }
    };
    return;
  }

  // If there is a press input, we cycle to the next enumerated value, wrapping
  // around from max to min.
  if (inputs.IsSet(ControlInput::Type::kPress)) {
    input_config_.input_type = ControlInput::Type::kPress;
    int min = cfg_min.value_or(0);
    int max = cfg_max.value_or(property_->GetMaxValue());
    reads_property_ = true;
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      int press_count = control.GetPressCount(id);
      if (press_count > 0) {
        int range = max - min + 1;
        int current = property.GetInt();
        int new_value = min + ((current - min + press_count) % range);
        property.SetInt(new_value);
      }
    };
    return;
  }
}

void ViewMapping::InitWriteControl() {
  if (!type_.IsSet(kWriteControl)) {
    return;
  }
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      // Action properties have no state, so they don't need to write to the
      // control.
      return;
    case ViewProperty::Type::kToggle:
      InitWriteToggleSyncFunction();
      break;
    case ViewProperty::Type::kPan:
      InitWritePanSyncFunction();
      break;
    case ViewProperty::Type::kVolume:
      InitWriteVolumeSyncFunction();
      break;
    case ViewProperty::Type::kNormalized:
      InitWriteNormalizedSyncFunction();
      break;
    case ViewProperty::Type::kText:
      InitWriteTextSyncFunction();
      break;
    case ViewProperty::Type::kColor:
      InitWriteColorSyncFunction();
      break;
    case ViewProperty::Type::kTimelinePosition:
      InitWriteTimelinePositionSyncFunction();
      break;
    case ViewProperty::Type::kEnumerated:
      InitWriteEnumeratedSyncFunction();
      break;
  }
}

void ViewMapping::InitWriteToggleSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a dvalue output, we set it to the max value when the property
  // is true, and 0 when the property is false.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      int max_value = control.GetDValueMaxValue(mode);
      control.SetDValue(property.GetBool() ? max_value : 0, mode);
    };
    return;
  }

  // If there is a cvalue output, we set it to 1.0 when the property is true,
  // and 0.0 when the property is false.
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // If there is a text output, we set it to "On" when the property is true, and
  // "Off" when the property is false.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a color output, we set it to white when the property is true,
  // and black when the property is false.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }
}

namespace {

int MapPanToTwoStates(double pan) {
  if (pan < 0.0) {
    return 0;  // Left of center
  }
  return 1;  // Center or right of center
}

int MapPanToThreeStates(double pan) {
  if (pan < -0.65) {
    return 0;  // Left
  } else if (pan > 0.65) {
    return 2;  // Right
  }
  return 1;  // Center
}

int MapPanToEvenRange(double pan, int max) {
  if (pan <= -1.0) {
    return 0;
  }
  if (pan >= 1.0) {
    return max;
  }

  // Determine the number of remaining intermediate values.
  int value_count = max - 1;
  int value = (pan + 1.0) * 0.5;  // Shift pan to the range (0,1)
  return static_cast<int>(std::ceil(value * value_count));
}

int MapPanToOddRange(double pan, int max) {
  int center = (max + 1) / 2;

  // Reserve specific values for hard left, center, and hard right to ensure
  // they always map to a single index.
  if (pan <= -1.0) {
    return 0;
  }
  if (pan >= 1.0) {
    return max;
  }
  if (pan == 0.0) {
    return center;
  }

  // Determine the number of values on either side of the center.
  int value_count = center - 1;

  // Determine the number of steps from the center, symmetrically for left and
  // right. This will always be greater than zero, as we check for pan == 0.0
  // above.
  int value = static_cast<int>(std::ceil(std::abs(pan) * value_count));
  if (pan < 0.0) {
    return center - value;
  }
  return center + value;
}

}  // namespace

void ViewMapping::InitWritePanSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a dvalue output, we clamp the pan value hard left, center, and
  // hard right to explicit values if possible, and then interpolate for values
  // in between.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    int max_value = control_->GetDValueMaxValue(config_.write.mode);
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapPanToTwoStates(property.GetPan()), mode);
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapPanToThreeStates(property.GetPan()), mode);
      };
    } else if (max_value % 2 == 1) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapPanToEvenRange(property.GetPan(),
                                            control.GetDValueMaxValue(mode)),
                          mode);
      };
    } else {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapPanToOddRange(property.GetPan(),
                                           control.GetDValueMaxValue(mode)),
                          mode);
      };
    }
    return;
  }

  // If there is a cvalue output, we map the pan value from [-1,1] to [0,1].
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // If there is a text output, let the property convert the pan value to text.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a color output, let the property convert the pan value to a
  // color.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }
}

namespace {

int MapVolumeToTwoStates(double volume) {
  // Unless it is totally silent, consider the volume to be on.
  return volume > 0.0 ? 1 : 0;
}

int MapVolumeToThreeStates(double volume) {
  if (volume <= 0.0) {
    return 0;  // Silent
  }
  if (volume > 1.0) {
    return 2;  // Greater than unity gain
  }
  return 1;  // Less than or equal to unity gain
}

int MapVolumeToRange(double volume, int max) {
  if (volume <= 0.0) {
    return 0;
  }
  if (volume >= kMaxVolume) {
    return max;
  }
  double value = volume / kMaxVolume;
  return static_cast<int>(std::ceil(value * (max - 1)));
}

}  // namespace

void ViewMapping::InitWriteVolumeSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a dvalue output, we map the volume value from [0,kMaxVolume] to
  // discrete steps based on the max value.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    int max_value = control_->GetDValueMaxValue(config_.write.mode);
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapVolumeToTwoStates(property.GetVolume()), mode);
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapVolumeToThreeStates(property.GetVolume()), mode);
      };
    } else {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapVolumeToRange(property.GetVolume(),
                                           control.GetDValueMaxValue(mode)),
                          mode);
      };
    }
    return;
  }

  // If there is a cvalue output, we map the volume value from [0,kMaxVolume] to
  // [0,1].
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      double volume = property.GetVolume();
      double normalized_volume = std::clamp(volume / kMaxVolume, 0.0, 1.0);
      control.SetCValue(normalized_volume, mode);
    };
    return;
  }

  // If there is a text output, let the property convert the volume value to
  // text.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a color output, let the property convert the volume value to a
  // color.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }
}

namespace {

int MapNormalizedToTwoStates(double value) { return value > 0.5 ? 1 : 0; }

int MapNormalizedToThreeStates(double value) {
  if (value <= 0.0) {
    return 0;
  }
  if (value >= 1.0) {
    return 2;
  }
  return 1;
}

int MapNormalizedToRange(double value, int max) {
  if (value <= 0.0) {
    return 0;
  }
  if (value >= 1.0) {
    return max;
  }
  return static_cast<int>(std::ceil(value * (max - 1)));
}

}  // namespace

void ViewMapping::InitWriteNormalizedSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a dvalue output, we map the normalized value from [0,1] to
  // to discrete steps depending on the max value.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    int max_value = control_->GetDValueMaxValue(config_.write.mode);
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToTwoStates(property.GetNormalized()),
                          mode);
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToThreeStates(property.GetNormalized()),
                          mode);
      };
    } else {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToRange(property.GetNormalized(),
                                               control.GetDValueMaxValue(mode)),
                          mode);
      };
    }
    return;
  }

  // If there is a cvalue output, we just set the value and let the control
  // handle the conversion.
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // If there is a text output, let the property convert the normalized value to
  // text.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a color output, let the property convert the normalized value
  // to a color.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }
}

void ViewMapping::InitWriteTextSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a text output, use it.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // The other output types don't make much sense for text properties, so we
  // don't support them.
}

void ViewMapping::InitWriteColorSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a color output, use it.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }

  // If there is a text output, let the property convert the color value to
  // text.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a cvalue output, let the property convert the color value to a
  // normalized value.
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // If there is a dvalue output, let the property convert the color value to a
  // normalized value, and make it discrete in the same way as the normalized
  // property.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    int max_value = control_->GetDValueMaxValue(config_.write.mode);
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToTwoStates(property.GetNormalized()),
                          mode);
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToThreeStates(property.GetNormalized()),
                          mode);
      };
    } else {
      write_control_ = [](ViewProperty& property, Control& control, int mode) {
        control.SetDValue(MapNormalizedToRange(property.GetNormalized(),
                                               control.GetDValueMaxValue(mode)),
                          mode);
      };
    }
    return;
  }
}

void ViewMapping::InitWriteTimelinePositionSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // Only text outputs make sense for timeline position, as the position needs
  // to be formatted for display. The mode is passed through to
  // Control::SetTimelineText to select the timeline display format.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetTimelineText(property.GetTimelinePosition(), mode);
    };
    return;
  }
}

void ViewMapping::InitWriteEnumeratedSyncFunction() {
  Control::Outputs outputs = control_->GetOutputs();

  // If there is a dvalue output, we map the enumerated integer value directly
  // to the discrete output value.
  if (outputs.IsSet(ControlOutput::Type::kDValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      int max_value = control.GetDValueMaxValue(mode);
      int prop_max = property.GetMaxValue();
      if (prop_max <= 0) {
        control.SetDValue(0, mode);
        return;
      }
      int value = property.GetInt();
      int mapped = static_cast<int>(
          static_cast<double>(value) / prop_max * max_value + 0.5);
      control.SetDValue(std::clamp(mapped, 0, max_value), mode);
    };
    return;
  }

  // If there is a cvalue output, we normalize the enumerated value to [0,1].
  if (outputs.IsSet(ControlOutput::Type::kCValue)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // If there is a text output, let the property convert the value to text.
  if (outputs.IsSet(ControlOutput::Type::kText)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetText(property.GetText(), mode);
    };
    return;
  }

  // If there is a color output, let the property convert the value to a color.
  if (outputs.IsSet(ControlOutput::Type::kColor)) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }
}

void ViewMapping::Enable() {
  if (enabled_) {
    return;
  }
  enabled_ = true;
  RefreshActive(view_->IsActive());
}

void ViewMapping::Disable() {
  if (!enabled_) {
    return;
  }
  enabled_ = false;
  RefreshActive(view_->IsActive());
}

void ViewMapping::RefreshActive(bool parent_active) {
  bool should_be_active = enabled_ && parent_active;
  if (active_ == should_be_active) {
    return;
  }
  active_ = should_be_active;
  if (active_) {
    if (reads_property_) {
      property_changed_ = true;
      property_->RegisterFlag(&property_changed_);
      if (mode_property_ != nullptr) {
        mode_property_->RegisterFlag(&property_changed_);
      }
    }
    if (read_control_ != nullptr) {
      input_handle_ = control_->RegisterInput(input_config_, &control_changed_);
    }
  } else {
    if (reads_property_) {
      property_->UnregisterFlag(&property_changed_);
      if (mode_property_ != nullptr) {
        mode_property_->UnregisterFlag(&property_changed_);
      }
    }
    input_handle_ = {};
  }
}

void ViewMapping::Sync() {
  if (!active_) {
    return;
  }
  if (control_changed_) {
    ReadControl();
  }
  if (property_changed_) {
    WriteControl();
  }
}

void ViewMapping::ReadControl() {
  control_changed_ = false;
  if (read_control_ != nullptr) {
    read_control_(*property_, *control_, input_handle_.GetId());
  }
}

void ViewMapping::WriteControl() {
  property_changed_ = false;
  if (type_.IsSet(kWriteControl)) {
    write_control_(*property_, *control_, ResolveMode());
  }
}

int ViewMapping::ResolveMode() const {
  if (mode_property_ != nullptr) {
    ViewProperty::Value value = mode_property_->GetValue();
    for (const auto& [map_value, map_mode] : config_.write.mode_map) {
      if (PropertyValueEquals(value, map_value)) {
        return map_mode;
      }
    }
  }
  return config_.write.mode;
}

}  // namespace jpr
