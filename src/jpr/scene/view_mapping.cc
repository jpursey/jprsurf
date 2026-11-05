// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include "absl/algorithm/container.h"
#include "absl/types/span.h"
#include "gb/base/flags.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_shape.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

void NoOpSyncFunction(ViewProperty& property, Control& control, int mode) {}

// Writes the property's text, fitted to the control's text width.
void WriteFittedText(ViewProperty& property, Control& control, int mode) {
  control.SetText(property.GetFittedText(control.GetTextWidth()), mode);
}

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

// Returns true if the value is a string.
bool IsString(const std::optional<ViewProperty::Value>& value) {
  return value.has_value() && std::holds_alternative<std::string>(*value);
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

// Toggles a double value between min and max. A value at (or beyond) either
// end goes to the other end, and one between them goes to whichever is
// nearer, or max from the middle.
double ToggleDouble(double current, double min, double max) {
  if (current <= min) {
    return max;
  }
  if (current >= max) {
    return min;
  }
  return current < (min + max) / 2.0 ? min : max;
}

// Steps a pan whose range spans the center through min, the center, and max:
// from below the center to it, from below max to max, and from max (or beyond)
// to min.
double StepPan(double current, double min, double max) {
  if (current < 0.0) {
    return 0.0;
  }
  if (current < max) {
    return max;
  }
  return min;
}

// Returns the value after `press_count` presses, each of which steps it
// between min and max (as ToggleDouble() or StepPan() do).
using StepFunction = double(double current, double min, double max);
double Press(StepFunction* step, double value, int press_count, double min,
             double max) {
  for (int i = 0; i < press_count; ++i) {
    value = step(value, min, max);
  }
  return value;
}

}  // namespace

ViewMapping::ViewMapping(View* view, TypeFlags type, ViewProperty* property,
                         Control* control, ReadConfig read_config,
                         Modifiers required_modifiers, int mode,
                         std::vector<PropertyModes> mode_overrides,
                         std::unique_ptr<ViewCondition> condition)
    : type_(type),
      view_(view),
      property_(property),
      control_(control),
      read_config_(std::move(read_config)),
      mode_(mode),
      mode_overrides_(std::move(mode_overrides)),
      condition_(std::move(condition)),
      write_control_(NoOpSyncFunction) {
  InitReadControl(required_modifiers);
  InitWriteControl();
}

bool ViewMapping::CanReadControlInto(PropertyAccessFlags access) {
  return access.Intersects({PropertyAccess::kWrite, PropertyAccess::kTrigger});
}

bool ViewMapping::CanWriteControlFrom(PropertyAccessFlags access) {
  return access.IsSet(PropertyAccess::kRead);
}

std::optional<ControlInput::Type> ViewMapping::GetReadInput(
    ViewProperty::Type type, PropertyAccessFlags access, Control::Inputs inputs,
    bool has_press_release, const ReadConfig& config) {
  using Input = ControlInput::Type;
  if (!CanReadControlInto(access)) {
    return std::nullopt;
  }

  // Touch reads as a press with release on any control with input. Otherwise,
  // a configured input type leaves only that input to read from, if the
  // control has it.
  if (config.touch) {
    if (inputs.IsEmpty()) {
      return std::nullopt;
    }
    inputs = Input::kPress;
    has_press_release = true;
  } else if (config.input_type.has_value()) {
    inputs = gb::Intersect(inputs, *config.input_type);
  }

  // The inputs each type reads, best first. Text is set from a press only to
  // one of the range's ends, so it needs both.
  const bool has_text_range =
      IsString(config.property_min) && IsString(config.property_max);
  absl::Span<const Input> best;
  switch (type) {
    case ViewProperty::Type::kAction: {
      static constexpr Input kAction[] = {Input::kPress};
      best = kAction;
      break;
    }
    case ViewProperty::Type::kToggle: {
      static constexpr Input kToggle[] = {Input::kPress, Input::kDelta,
                                          Input::kValue};
      best = kToggle;
      break;
    }
    case ViewProperty::Type::kText: {
      static constexpr Input kText[] = {Input::kValue, Input::kPress};
      best = has_text_range ? absl::Span<const Input>(kText)
                            : absl::Span<const Input>(kText, 1);
      break;
    }
    case ViewProperty::Type::kTimelinePosition: {
      static constexpr Input kTimelinePosition[] = {Input::kDelta};
      best = kTimelinePosition;
      break;
    }
    case ViewProperty::Type::kPan:
    case ViewProperty::Type::kVolume:
    case ViewProperty::Type::kNormalized:
    case ViewProperty::Type::kColor:
    case ViewProperty::Type::kEnumerated: {
      static constexpr Input kValues[] = {Input::kValue, Input::kDelta,
                                          Input::kPress};
      best = kValues;
      break;
    }
  }

  // Press release reads only a press with a release, into a type that a press
  // can set, and that holds a value while pressed, which a pan or a volume
  // doesn't.
  if (config.press_release) {
    if (!has_press_release || !inputs.IsSet(Input::kPress) ||
        !absl::c_linear_search(best, Input::kPress) ||
        type == ViewProperty::Type::kPan ||
        type == ViewProperty::Type::kVolume) {
      return std::nullopt;
    }
    return Input::kPress;
  }
  for (Input input : best) {
    if (inputs.IsSet(input)) {
      return input;
    }
  }
  return std::nullopt;
}

std::optional<ControlOutput::Type> ViewMapping::GetWriteOutput(
    ViewProperty::Type type, PropertyAccessFlags access,
    Control::Outputs outputs) {
  using Output = ControlOutput::Type;
  if (!CanWriteControlFrom(access)) {
    return std::nullopt;
  }

  // The outputs each type writes, best first. An action has no state to
  // write.
  absl::Span<const Output> best;
  switch (type) {
    case ViewProperty::Type::kAction:
      return std::nullopt;
    case ViewProperty::Type::kText:
    case ViewProperty::Type::kTimelinePosition: {
      static constexpr Output kText[] = {Output::kText};
      best = kText;
      break;
    }
    case ViewProperty::Type::kColor: {
      static constexpr Output kColor[] = {Output::kColor, Output::kText,
                                          Output::kCValue, Output::kDValue};
      best = kColor;
      break;
    }
    case ViewProperty::Type::kToggle:
    case ViewProperty::Type::kPan:
    case ViewProperty::Type::kVolume:
    case ViewProperty::Type::kNormalized:
    case ViewProperty::Type::kEnumerated: {
      static constexpr Output kValues[] = {Output::kDValue, Output::kCValue,
                                           Output::kText, Output::kColor};
      best = kValues;
      break;
    }
  }
  for (Output output : best) {
    if (outputs.IsSet(output)) {
      return output;
    }
  }
  return std::nullopt;
}

std::optional<int> ViewMapping::FindMode(ViewProperty::Type type,
                                         const WidgetShape& shape,
                                         std::string_view name) {
  if (name.empty()) {
    return 0;
  }
  // A timeline position written as text names its format.
  if (type == ViewProperty::Type::kTimelinePosition &&
      shape.GetOutputs().IsSet(ControlOutput::Type::kText)) {
    for (const TimelineFormat& format : kTimelineFormats) {
      if (format.name == name) {
        return ControlTextOutput::GetTimelineTextMode(format.mode);
      }
    }
    return std::nullopt;
  }
  const absl::Span<const std::string> names = shape.GetModeNames();
  const auto it = std::ranges::find(names, name);
  if (it == names.end()) {
    return std::nullopt;
  }
  return static_cast<int>(it - names.begin());
}

ViewMapping::~ViewMapping() {
  // This releases everything the mapping has registered, including watching
  // its condition.
  enabled_ = false;
  RefreshActive(false);
}

void ViewMapping::InitReadControl(Modifiers required_modifiers) {
  if (!type_.IsSet(kReadControl)) {
    return;
  }
  // A mapping that reads nothing isn't a read mapping.
  const std::optional<ControlInput::Type> input = GetReadInput(
      property_->GetType(), property_->GetAccess(), control_->GetInputs(),
      control_->HasPressRelease(), read_config_);
  if (!input.has_value()) {
    type_.Clear(kReadControl);
    read_config_.press_release = false;
    return;
  }
  input_config_.input_type = *input;
  input_config_.required_modifiers = required_modifiers;
  input_config_.press_behavior = read_config_.press_behavior;
  input_config_.touch = read_config_.touch;
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      InitReadActionSyncFunction();
      break;
    case ViewProperty::Type::kToggle:
      InitReadToggleSyncFunction(*input);
      break;
    case ViewProperty::Type::kPan:
      InitReadPanSyncFunction(*input);
      break;
    case ViewProperty::Type::kVolume:
      InitReadVolumeSyncFunction(*input);
      break;
    case ViewProperty::Type::kNormalized:
      InitReadNormalizedSyncFunction(*input);
      break;
    case ViewProperty::Type::kText:
      InitReadTextSyncFunction(*input);
      break;
    case ViewProperty::Type::kColor:
      InitReadColorSyncFunction(*input);
      break;
    case ViewProperty::Type::kTimelinePosition:
      InitReadTimelinePositionSyncFunction();
      break;
    case ViewProperty::Type::kEnumerated:
      InitReadEnumeratedSyncFunction(*input);
      break;
  }
}

void ViewMapping::InitReadActionSyncFunction() {
  // An action is read only from a press (see GetReadInput()), and runs on
  // every press.
  read_control_ = [](ViewProperty& property, Control& control, InputId id) {
    if (control.GetPressCount(id) > 0) {
      property.RunAction();
    }
  };
}

void ViewMapping::InitReadToggleSyncFunction(ControlInput::Type input) {
  // If we are configured for press/release behavior, then we set the property
  // based on whether the control is currently pressed.
  if (read_config_.press_release) {
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      property.SetBool(control.IsPressed(id));
    };
    return;
  }

  // From a press input, we toggle the property on each press event.
  if (input == ControlInput::Type::kPress) {
    reads_property_ = true;
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      if (control.GetPressCount(id) % 2 != 0) {
        property.SetBool(!property.GetBool());
      }
    };
    return;
  }

  // From a delta input, we set the property to false when the delta is
  // negative, and true when the delta is positive.
  if (input == ControlInput::Type::kDelta) {
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      double delta = control.GetDelta(id);
      if (delta != 0) {
        property.SetBool(delta > 0);
      }
    };
    return;
  }

  // From a value input, we just set the value and let the property
  // handle the conversion.
  read_control_ = [](ViewProperty& property, Control& control, InputId id) {
    property.SetNormalized(control.GetValue(id));
  };
}

void ViewMapping::InitReadPanSyncFunction(ControlInput::Type input) {
  auto cfg_min = GetDouble(read_config_.property_min);
  auto cfg_max = GetDouble(read_config_.property_max);

  // The value input is preferred for pan controls, if it is available, which
  // maps [0,1] to the configured range (default [-1,1]).
  if (input == ControlInput::Type::kValue) {
    double min = cfg_min.value_or(-1.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double value = control.GetValue(id);
      property.SetPan(Lerp(value, min, max));
    };
    return;
  }

  // From a delta input, we add the delta to the current pan value, and
  // clamp to the configured range (default [-1,1]).
  if (input == ControlInput::Type::kDelta) {
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

  // From a press input, behavior depends on the configured range.
  double min = cfg_min.value_or(-1.0);
  double max = cfg_max.value_or(1.0);
  reads_property_ = true;
  // A three-way toggle (min, 0, max) if zero is strictly between min and
  // max, and a binary toggle between min and max otherwise.
  StepFunction* step = (min < 0.0 && max > 0.0) ? StepPan : ToggleDouble;
  read_control_ = [step, min, max](ViewProperty& property, Control& control,
                                   InputId id) {
    int press_count = control.GetPressCount(id);
    if (press_count > 0) {
      property.SetPan(Press(step, property.GetPan(), press_count, min, max));
    }
  };
}

void ViewMapping::InitReadVolumeSyncFunction(ControlInput::Type input) {
  auto cfg_min = GetDouble(read_config_.property_min);
  auto cfg_max = GetDouble(read_config_.property_max);

  // The value input is preferred for volume controls, if it is available, which
  // maps [0,1] to the configured range (default [0,kMaxVolume]).
  if (input == ControlInput::Type::kValue) {
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(kMaxVolume);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      double value = control.GetValue(id);
      property.SetVolume(Lerp(value, min, max));
    };
    return;
  }

  // From a delta input, we add the delta to the current volume value,
  // and clamp to the configured range (default [0,kMaxVolume]).
  if (input == ControlInput::Type::kDelta) {
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

  // From a press input, we toggle between the configured min and max
  // values (default 0.0 and 1.0).
  double min = cfg_min.value_or(0.0);
  double max = cfg_max.value_or(1.0);
  reads_property_ = true;
  read_control_ = [min, max](ViewProperty& property, Control& control,
                             InputId id) {
    int press_count = control.GetPressCount(id);
    if (press_count > 0) {
      property.SetVolume(
          Press(ToggleDouble, property.GetVolume(), press_count, min, max));
    }
  };
}

void ViewMapping::InitReadNormalizedSyncFunction(ControlInput::Type input) {
  auto cfg_min = GetDouble(read_config_.property_min);
  auto cfg_max = GetDouble(read_config_.property_max);

  // While weird, press/release behavior is technically supported for normalized
  // properties. If configured, the property is set to 1.0 when the control is
  // pressed, and set to 0.0 when the control is released.
  if (read_config_.press_release) {
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetNormalized(control.IsPressed(id) ? max : min);
    };
    return;
  }

  // From a value input, we linearly map [0,1] to the configured range
  // (default [0,1]).
  if (input == ControlInput::Type::kValue) {
    double min = cfg_min.value_or(0.0);
    double max = cfg_max.value_or(1.0);
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetNormalized(Lerp(control.GetValue(id), min, max));
    };
    return;
  }

  // From a delta input, we add the delta to the current value and clamp
  // to the configured range (default [0,1]).
  if (input == ControlInput::Type::kDelta) {
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

  // From a press input, we toggle between the configured min and max
  // values (default 0.0 and 1.0).
  double min = cfg_min.value_or(0.0);
  double max = cfg_max.value_or(1.0);
  reads_property_ = true;
  read_control_ = [min, max](ViewProperty& property, Control& control,
                             InputId id) {
    int press_count = control.GetPressCount(id);
    if (press_count > 0) {
      property.SetNormalized(
          Press(ToggleDouble, property.GetNormalized(), press_count, min, max));
    }
  };
}

void ViewMapping::InitReadTextSyncFunction(ControlInput::Type input) {
  // From a value input, we just set the value and let the property handle the
  // conversion.
  if (input == ControlInput::Type::kValue) {
    read_control_ = [](ViewProperty& property, Control& control, InputId id) {
      property.SetNormalized(control.GetValue(id));
    };
    return;
  }

  // A press sets the text to an end of the range, which GetReadInput() made
  // sure there are both of. While weird, press/release behavior is technically
  // supported for text properties: the max text while the control is pressed,
  // and the min text once it is released. Otherwise, each press toggles
  // between them.
  std::string min = *GetString(read_config_.property_min);
  std::string max = *GetString(read_config_.property_max);
  if (read_config_.press_release) {
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetText(control.IsPressed(id) ? max : min);
    };
    return;
  }
  reads_property_ = true;
  read_control_ = [min, max](ViewProperty& property, Control& control,
                             InputId id) {
    if (control.GetPressCount(id) % 2 != 0) {
      property.SetText(property.GetText() == max ? min : max);
    }
  };
}

void ViewMapping::InitReadColorSyncFunction(ControlInput::Type input) {
  auto cfg_min = GetColor(read_config_.property_min);
  auto cfg_max = GetColor(read_config_.property_max);

  // While weird, press/release behavior is technically supported for color
  // properties. If configured, the property is set to the configured max color
  // value when the control is pressed, and set to the configured min color
  // value when the control is released.
  if (read_config_.press_release) {
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

  // From a value input, we linearly interpolate per-channel between
  // the configured min and max colors (default black to white).
  if (input == ControlInput::Type::kValue) {
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
  if (input == ControlInput::Type::kDelta) {
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
}

void ViewMapping::InitReadTimelinePositionSyncFunction() {
  // A timeline position is read only from a delta (see GetReadInput()), such
  // as a jog wheel's, to scrub.
  reads_property_ = true;
  read_control_ = [](ViewProperty& property, Control& control, InputId id) {
    double delta = control.GetDelta(id);
    if (delta != 0) {
      TimelinePosition pos = property.GetTimelinePosition();
      pos.SetValue(pos.GetValue() + delta);
      property.SetTimelinePosition(pos);
    }
  };
}

void ViewMapping::InitReadEnumeratedSyncFunction(ControlInput::Type input) {
  auto cfg_min = GetInt(read_config_.property_min);
  auto cfg_max = GetInt(read_config_.property_max);

  // If we are configured for press/release behavior, then we set the property
  // to the max value when pressed and the min value when released.
  if (read_config_.press_release) {
    int min = cfg_min.value_or(0);
    int max = cfg_max.value_or(property_->GetMaxValue());
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      property.SetInt(control.IsPressed(id) ? max : min);
    };
    return;
  }

  // From a value input, we linearly map [0,1] to the configured integer
  // range (default [0,GetMaxValue()]).
  if (input == ControlInput::Type::kValue) {
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

  // From a delta input, we step the integer value up or down, clamped
  // to the configured range (default [0,GetMaxValue()]).
  if (input == ControlInput::Type::kDelta) {
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

  // From a press input, we either toggle between max and min, or cycle
  // to the next enumerated value, wrapping around from max to min.
  int min = cfg_min.value_or(0);
  int max = cfg_max.value_or(property_->GetMaxValue());
  reads_property_ = true;
  if (read_config_.press_toggles) {
    read_control_ = [min, max](ViewProperty& property, Control& control,
                               InputId id) {
      if (control.GetPressCount(id) % 2 != 0) {
        property.SetInt(property.GetInt() == max ? min : max);
      }
    };
    return;
  }
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
}

void ViewMapping::InitWriteControl() {
  if (!type_.IsSet(kWriteControl)) {
    return;
  }
  // If nothing can be written to the control for this property, this is not a
  // write mapping. In particular, it must not register as an output writer, or
  // the control would never be cleared.
  const std::optional<ControlOutput::Type> output = GetWriteOutput(
      property_->GetType(), property_->GetAccess(), control_->GetOutputs());
  if (!output.has_value()) {
    type_.Clear(kWriteControl);
    return;
  }
  reads_property_ = true;
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      // GetWriteOutput() writes no action.
      break;
    case ViewProperty::Type::kToggle:
      InitWriteToggleSyncFunction(*output);
      break;
    case ViewProperty::Type::kPan:
      InitWritePanSyncFunction(*output);
      break;
    case ViewProperty::Type::kVolume:
      InitWriteVolumeSyncFunction(*output);
      break;
    case ViewProperty::Type::kNormalized:
      InitWriteNormalizedSyncFunction(*output);
      break;
    case ViewProperty::Type::kText:
      InitWriteTextSyncFunction();
      break;
    case ViewProperty::Type::kColor:
      InitWriteColorSyncFunction(*output);
      break;
    case ViewProperty::Type::kTimelinePosition:
      InitWriteTimelinePositionSyncFunction();
      break;
    case ViewProperty::Type::kEnumerated:
      InitWriteEnumeratedSyncFunction(*output);
      break;
  }
}

void ViewMapping::InitWriteToggleSyncFunction(ControlOutput::Type output) {
  // To a dvalue output, we set it to the max value when the property
  // is true, and 0 when the property is false.
  if (output == ControlOutput::Type::kDValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      int max_value = control.GetDValueMaxValue(mode);
      control.SetDValue(property.GetBool() ? max_value : 0, mode);
    };
    return;
  }

  // To a cvalue output, we set it to 1.0 when the property is true,
  // and 0.0 when the property is false.
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // To a text output, we set it to "On" when the property is true, and
  // "Off" when the property is false.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a color output, we set it to white when the property is true,
  // and black when the property is false.
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetColor(property.GetColor(), mode);
  };
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
  double value = (pan + 1.0) * 0.5;  // Shift pan to the range (0,1)
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

// Maps the pan to a DValue output's steps, up to `max`. With more than three
// steps, hard left and hard right each have a step of their own (and the
// center, with an odd number of steps), and the pans in between are spread
// over the rest.
int MapPanToSteps(double pan, int max) {
  if (max == 1) {
    return MapPanToTwoStates(pan);
  }
  if (max == 2) {
    return MapPanToThreeStates(pan);
  }

  // There are max + 1 steps, so an odd max is an even number of them.
  if (max % 2 == 1) {
    return MapPanToEvenRange(pan, max);
  }
  return MapPanToOddRange(pan, max);
}

}  // namespace

void ViewMapping::InitWritePanSyncFunction(ControlOutput::Type output) {
  // To a dvalue output, we map the pan value to the steps of the mode
  // it is written in (see MapPanToSteps()).
  if (output == ControlOutput::Type::kDValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetDValue(
          MapPanToSteps(property.GetPan(), control.GetDValueMaxValue(mode)),
          mode);
    };
    return;
  }

  // To a cvalue output, we map the pan value from [-1,1] to [0,1].
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // To a text output, let the property convert the pan value to text.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a color output, let the property convert the pan value to a
  // color.
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetColor(property.GetColor(), mode);
  };
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

// Maps the volume to a DValue output's steps, up to `max`.
int MapVolumeToSteps(double volume, int max) {
  if (max == 1) {
    return MapVolumeToTwoStates(volume);
  }
  if (max == 2) {
    return MapVolumeToThreeStates(volume);
  }
  return MapVolumeToRange(volume, max);
}

}  // namespace

void ViewMapping::InitWriteVolumeSyncFunction(ControlOutput::Type output) {
  // To a dvalue output, we map the volume value from [0,kMaxVolume] to
  // the steps of the mode it is written in.
  if (output == ControlOutput::Type::kDValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetDValue(MapVolumeToSteps(property.GetVolume(),
                                         control.GetDValueMaxValue(mode)),
                        mode);
    };
    return;
  }

  // To a cvalue output, we map the volume value from [0,kMaxVolume] to
  // [0,1].
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      double volume = property.GetVolume();
      double normalized_volume = std::clamp(volume / kMaxVolume, 0.0, 1.0);
      control.SetCValue(normalized_volume, mode);
    };
    return;
  }

  // To a text output, let the property convert the volume value to
  // text.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a color output, let the property convert the volume value to a
  // color.
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetColor(property.GetColor(), mode);
  };
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

// Maps the normalized value to a DValue output's steps, up to `max`.
int MapNormalizedToSteps(double value, int max) {
  if (max == 1) {
    return MapNormalizedToTwoStates(value);
  }
  if (max == 2) {
    return MapNormalizedToThreeStates(value);
  }
  return MapNormalizedToRange(value, max);
}

// Writes the property's normalized value to the steps of the mode it is
// written in.
void WriteNormalizedSteps(ViewProperty& property, Control& control, int mode) {
  control.SetDValue(MapNormalizedToSteps(property.GetNormalized(),
                                         control.GetDValueMaxValue(mode)),
                    mode);
}

}  // namespace

void ViewMapping::InitWriteNormalizedSyncFunction(ControlOutput::Type output) {
  // To a dvalue output, we map the normalized value from [0,1] to
  // the steps of the mode it is written in.
  if (output == ControlOutput::Type::kDValue) {
    write_control_ = WriteNormalizedSteps;
    return;
  }

  // To a cvalue output, we just set the value and let the control
  // handle the conversion.
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // To a text output, let the property convert the normalized value to
  // text.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a color output, let the property convert the normalized value
  // to a color.
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetColor(property.GetColor(), mode);
  };
}

void ViewMapping::InitWriteTextSyncFunction() {
  // Text is written only as text (see GetWriteOutput()).
  write_control_ = WriteFittedText;
}

void ViewMapping::InitWriteColorSyncFunction(ControlOutput::Type output) {
  // To a color output, as it is.
  if (output == ControlOutput::Type::kColor) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetColor(property.GetColor(), mode);
    };
    return;
  }

  // To a text output, let the property convert the color value to
  // text.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a cvalue output, let the property convert the color value to a
  // normalized value.
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // To a dvalue output, let the property convert the color value to a
  // normalized value, and make it discrete in the same way as the normalized
  // property.
  write_control_ = WriteNormalizedSteps;
}

void ViewMapping::InitWriteTimelinePositionSyncFunction() {
  // A timeline position is written only as text (see GetWriteOutput()), in
  // the format its mode names (see Control::SetTimelineText()).
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetTimelineText(property.GetTimelinePosition(), mode);
  };
}

void ViewMapping::InitWriteEnumeratedSyncFunction(ControlOutput::Type output) {
  // To a dvalue output, we map the enumerated integer value directly
  // to the discrete output value.
  if (output == ControlOutput::Type::kDValue) {
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

  // To a cvalue output, we normalize the enumerated value to [0,1].
  if (output == ControlOutput::Type::kCValue) {
    write_control_ = [](ViewProperty& property, Control& control, int mode) {
      control.SetCValue(property.GetNormalized(), mode);
    };
    return;
  }

  // To a text output, let the property convert the value to text.
  if (output == ControlOutput::Type::kText) {
    write_control_ = WriteFittedText;
    return;
  }

  // To a color output, let the property convert the value to a color.
  write_control_ = [](ViewProperty& property, Control& control, int mode) {
    control.SetColor(property.GetColor(), mode);
  };
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
  // The condition determines whether the mapping is active, so it is watched
  // whenever the parent view is active, whether or not the mapping is.
  if (condition_ != nullptr) {
    condition_->Watch(parent_active);
  }
  parent_active_ = parent_active;

  // The input is registered whatever the condition, as ReadControl() checks it.
  bool should_read = enabled_ && parent_active && read_control_ != nullptr;
  if (should_read != input_handle_.IsRegistered()) {
    if (should_read) {
      input_handle_ = control_->RegisterInput(input_config_, &control_changed_);
    } else {
      const InputId id = input_handle_.GetId();
      input_handle_ = {};
      if (holding_) {
        // With the input gone, the control reads it as released, so this
        // applies the release.
        holding_ = false;
        read_control_(*property_, *control_, id);
      }
    }
  }

  bool should_be_active = enabled_ && parent_active &&
                          (condition_ == nullptr || condition_->IsMet());
  if (active_ == should_be_active) {
    return;
  }
  active_ = should_be_active;
  if (active_) {
    if (reads_property_) {
      property_changed_ = true;
      property_->RegisterFlag(&property_changed_);
      for (const PropertyModes& mode_override : mode_overrides_) {
        mode_override.property->RegisterFlag(&property_changed_);
      }
    }
    if (type_.IsSet(kWriteControl)) {
      output_handle_ = control_->RegisterOutputWriter();
    }
  } else {
    if (reads_property_) {
      property_->UnregisterFlag(&property_changed_);
      for (const PropertyModes& mode_override : mode_overrides_) {
        mode_override.property->UnregisterFlag(&property_changed_);
      }
    }
    output_handle_ = {};
  }
}

void ViewMapping::Sync() {
  if (condition_ != nullptr && condition_->HasChanged()) {
    RefreshActive(parent_active_);
  }
  if (control_changed_) {
    ReadControl();
  }
  if (active_ && property_changed_) {
    WriteControl();
  }
}

void ViewMapping::ReadControl() {
  control_changed_ = false;
  if (!input_handle_.IsRegistered()) {
    return;
  }
  InputId id = input_handle_.GetId();
  bool pressed = read_config_.press_release && control_->IsPressed(id);
  if (read_config_.press_release && !pressed) {
    // A release is applied only for a press that was, whether or not the
    // condition is met now, so what the mapping holds can't stick on.
    if (!holding_) {
      return;
    }
  } else if (!active_) {
    // With the input registered, the mapping is inactive only while its
    // condition isn't met.
    return;
  }
  read_control_(*property_, *control_, id);
  holding_ = pressed;
}

void ViewMapping::WriteControl() {
  property_changed_ = false;
  if (type_.IsSet(kWriteControl)) {
    write_control_(*property_, *control_, ResolveMode());
  }
}

int ViewMapping::ResolveMode() const {
  for (const PropertyModes& mode_override : mode_overrides_) {
    for (const auto& [map_value, map_mode] : mode_override.value_to_mode) {
      if (mode_override.property->Equals(map_value)) {
        return map_mode;
      }
    }
  }
  return mode_;
}

}  // namespace jpr
