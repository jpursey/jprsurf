// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"

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

  // Returns the number of modes supported by this output. The mode is an
  // optional parameter to the Set* methods, and must be in the range
  // [0, GetModeCount()). If the output only has one mode, this returns 1.
  int GetModeCount() const { return mode_count_; }

 protected:
  explicit ControlOutput(int mode_count = 1) : mode_count_(mode_count) {}

 private:
  int mode_count_ = 1;
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
  // Values outside of this range will be clamped to that range. The mode
  // parameter selects the output mode, and is clamped to the valid range.
  void SetValue(double value, int mode = 0);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kCValue; }

 protected:
  explicit ControlCValueOutput(int mode_count = 1)
      : ControlOutput(mode_count) {}

  // Derived classes should override this to update the value of the physical
  // output. The value is guaranteed to be in the range [0.0, 1.0], and the
  // mode is guaranteed to be in the range [0, GetModeCount()).
  virtual void OnValueChanged(double value, int mode) = 0;
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

  // Returns the maximum value of this output for the given mode. The mode
  // parameter is clamped to the valid range [0, GetModeCount()).
  int GetMaxValue(int mode = 0) const;

  // Sets the value of this output, which must be in the range [0,
  // GetMaxValue(mode)]. Values outside of this range will be clamped to that
  // range. The mode parameter selects the output mode, and is clamped to the
  // valid range.
  void SetValue(int value, int mode = 0);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kDValue; }

 protected:
  explicit ControlDValueOutput(int max_value);
  explicit ControlDValueOutput(absl::Span<const int> max_values);

  // Derived classes should override this to update the value of the physical
  // output. The value is guaranteed to be in the range
  // [0, GetMaxValue(mode)], and the mode is guaranteed to be in the range
  // [0, GetModeCount()).
  virtual void OnValueChanged(int value, int mode) = 0;

 private:
  std::vector<int> max_values_;
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
  // needed to accomodate the actual control. The mode parameter selects the
  // output mode, and is clamped to the valid range.
  void SetText(std::string_view text, int mode = 0);

  // Sets the text of this output based on the given timeline position. The mode
  // maps to the TimelineMode as follows:
  //   mode = 0: GetRulerMode()
  //   mode = 1: TimelineMode::kBeats
  //   mode = 2: TimelineMode::kTime
  //   mode = 3: TimelineMode::kFrames
  //   mode = 4: TimelineMode::kSamples
  void SetTimelineText(TimelinePosition position, int mode = 0);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kText; }

 protected:
  explicit ControlTextOutput(int mode_count = 1) : ControlOutput(mode_count) {}

  // Derived classes can optionally override this to update the text of the
  // physical output based on a timeline position. If this is not overridden,
  // the default implementation will format the position automatically based on
  // the mode and call OnTextChanged with a mode of zero.
  virtual void OnTimelineTextChanged(TimelinePosition position,
                                     TimelineMode mode);

  // Derived classes should override this to update the text of the physical
  // output. The mode is guaranteed to be in the range [0, GetModeCount()).
  virtual void OnTextChanged(std::string_view text, int mode) = 0;
};

//==============================================================================
// ControlColorOutput
//==============================================================================

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
  // accomodate the actual control. The mode parameter selects the output mode,
  // and is clamped to the valid range.
  void SetColor(Color color, int mode = 0);

  // Implements ControlOutput.
  Type GetType() const override { return Type::kColor; }

 protected:
  explicit ControlColorOutput(int mode_count = 1) : ControlOutput(mode_count) {}

  // Derived classes should override this to update the color of the physical
  // output. The mode is guaranteed to be in the range [0, GetModeCount()).
  virtual void OnColorChanged(Color color, int mode) = 0;
};

}  // namespace jpr
