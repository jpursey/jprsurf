// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

namespace jpr {

//==============================================================================
// ControlOutput
//==============================================================================

// This is the base class for all control output types. It defines what types of
// values a control can output and the behavior of the output.
class ControlOutput {
 public:
  enum class Type {
    // Continuous absolute value in the range [0.0, 1.0]. Implemented by
    // ControlCValueOutput.
    kCValue,

    // Discrete integer value in the range [0, N], where N is defined by the
    // control. Implemented by ControlDValueOutput.
    kDValue,

    // Text value. Implemented by ControlTextOutput.
    kText,

    // Color value. Implemented by ControlColorOutput.
    kColor,
  };

  ControlOutput(const ControlOutput&) = delete;
  ControlOutput& operator=(const ControlOutput&) = delete;
  virtual ~ControlOutput() = default;

  // Returns the type of this control output, which defines the behavior of the
  // control. Callers can use this to cast to the appropriate derived class to
  // set the value of the output.
  virtual Type GetType() const = 0;

 protected:
  ControlOutput() = default;
};

//==============================================================================
// ControlCValueOutput
//==============================================================================

// This control output type represents a continuous absolute value in the range
// [0.0, 1.0].
//
// This output generally corresponds to the position of a physical control (e.g.
// how far a fader is pushed or where a knob is turned to).
class ControlCValueOutput : public ControlOutput {
 public:
  virtual ~ControlCValueOutput() = default;

  // Sets the value of this output, which must be in the range [0.0, 1.0].
  // Values outside of this range will be clamped to that range.
  void SetValue(double value);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kCValue; }

 protected:
  ControlCValueOutput() = default;

  // Derived classes should call this to update the value of the physical
  // output. The value is guaranteed to be in the range [0.0, 1.0].
  virtual void OnValueChanged(double value) = 0;
};

//==============================================================================
// ControlDValueOutput
//==============================================================================

// This control output type represents a discrete integer value in the range
// [0, N], where N is defined by the control.
//
// This output generally corresponds to a control with a fixed number of
// positions (e.g. a rotary encoder with detents, or a meter with a set number
// of options).
//
// Note: Many controls can be implemented with either a ControlCValueOutput or
// ControlDValueOutput, and the choice of which to use is generally up to the
// control surface implementation. However, ControlDValueOutput may yield better
// behavior when mapping to Reaper values, as the mapping can ensure the best
// representation based on the type of value. For instance, a pan control with a
// ControlDValueOutput can reserve a specific value for hard left, center, and
// hard right, giving better precision for those common positions, while a
// ControlCValueOutput would result in close values visually showing as hard
// left, center, or hard right, even if they are not exactly.
class ControlDValueOutput : public ControlOutput {
 public:
  virtual ~ControlDValueOutput() = default;

  // Returns the maximum value of this output, which is the upper bound of the
  // valid range for SetValue().
  int GetMaxValue() const { return max_value_; }

  // Sets the value of this output, which must be in the range [0,
  // GetMaxValue()]. Values outside of this range will be clamped to that range.
  void SetValue(int value);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kDValue; }

 protected:
  ControlDValueOutput(int max_value) : max_value_(max_value) {}

  // Derived classes should call this to update the value of the physical
  // output. The value is guaranteed to be in the range [0, GetMaxValue()].
  virtual void OnValueChanged(int value) = 0;

 private:
  int max_value_ = 0;
};

//==============================================================================
// ControlTextOutput
//==============================================================================

// This control output type represents a text value.
//
// This output generally corresponds to a display that can show arbitrary text.
// However, it may truncate or alter characters as needed to accomodate the
// actual control.
class ControlTextOutput : public ControlOutput {
 public:
  virtual ~ControlTextOutput() = default;

  // Sets the text of this output. The control may truncate or alter the text as
  // needed to accomodate the actual control.
  void SetText(std::string_view text);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kText; }

 protected:
  ControlTextOutput() = default;

  // Derived classes should call this to update the text of the physical output.
  virtual void OnTextChanged(std::string_view text) = 0;
};

//==============================================================================
// ControlColorOutput
//==============================================================================

// This struct represents a color value, with 8 bits for each of red, green, and
// blue.
struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// This control output type represents a color value.
//
// This output generally corresponds to a control that can display color (e.g.
// an RGB LED). The control may alter the color as needed to accomodate the
// actual control (for instance, if the control only supports a limited number
// of colors).
class ControlColorOutput : public ControlOutput {
 public:
  virtual ~ControlColorOutput() = default;

  // Sets the color of this output. The control may alter the color as needed to
  // accomodate the actual control.
  void SetColor(Color color);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kColor; }

 protected:
  ControlColorOutput() = default;

  // Derived classes should call this to update the color of the physical
  // output.
  virtual void OnColorChanged(Color color) = 0;
};

}  // namespace jpr
