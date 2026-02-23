// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "control_input.h"
#include "control_output.h"

namespace jpr {

class Control {
 public:
  enum class OutputMode {
    // The output is completely independent of the input, and can be set at any
    // time without affecting the physical control or subsequent input values.
    // For example, an indicator led on a button or a light ring around an
    // endless encoder.
    kIndependent,

    // The output is directly tied to the input, and setting the output will
    // affect the input value. If a press_input is present, the ouptut will only
    // be updated when it is not pressed. If no press_input is present, the
    // output will be delayed until after input events have stopped for several
    // frames.
    kDependent,

    // The output is directly tied to the input, and setting the output will
    // affect the input value. This is typically used for motorized faders or
    // other feedback-driven controls. This is behaviorly the same as
    // kDependent, except that if a press_input is not present, a much longer
    // delay is used after input events have stopped.
    kMotorized,
  };

  struct Options {
    // Name of the control.
    //
    // This should be a short, human-readable string that describes the control,
    // such as "Play" or "Fader1". This is required to be unique within a
    // Device, but does not need to be globally unique.
    std::string name;

    // Set of inputs for this control.
    //
    // There may be at most one input of each type, and there may be no inputs
    // at all if this is a display-only control.
    //
    // Controls should be configured with the minimum set of inputs that most
    // precisely describes the hardware control. Different input configurations
    // mean different things. Specifically:
    //   - Only one input set: This is the most common case, the input describes
    //     the hardware capabilities. A pot or fader is a usually value input,
    //     an endless encoder is a delta input, and a button is a press input.
    //   - If both a value and delta input are specified, then the control
    //     supports both an absolute min/max value and it can emulate an encoder
    //     delta. For instance, some endless encoders have an internal "value"
    //     tracked on the hardware, but also will continue sending the min or
    //     max value when twisted farther. This allows delta motion to be
    //     emulated.
    //   - If a press input is specified along with either a value or delta
    //     input, then the control may be touched or pressed. This configuration
    //     is recommended only in combination with the kDependent or kMotorized
    //     output modes, and only when it is a true capacitive touch control.
    //     On the other hand, this should generally not be used to define a
    //     pushable endless encoder. Those should be more flexibly represented
    //     as two separate controls so they can be mapped independently, as
    //     mappings are per-control.
    //
    // Control mappings from Reaper parameters and actions can generally work
    // with any input type, but will choose the input(s) that best maps to the
    // Reaper parameter being mapped. For instance, the "pan" mapping will
    // prefer a value input over a delta input, over a press input. The "play"
    // action however will prefer press over delta over value. In both cases,
    // they will all work however.
    std::unique_ptr<ControlValueInput> value_input;
    std::unique_ptr<ControlDeltaInput> delta_input;
    std::unique_ptr<ControlPressInput> press_input;

    // The output mode for this control, which defines how the output(s) of this
    // control behave relative to the inputs. See OutputMode for details.
    OutputMode output_mode = OutputMode::kIndependent;

    // Set of outputs for this control.
    //
    // There may be at most one output of each type, and there may be no outputs
    // at all if this is a control that only generates input like a button that
    // doesn't light up.
    //
    // If there are multiple outputs, they should ultimately map to the same
    // underlying control or display value. As with inputs, controls should be
    // configured with the minimum set of outputs that best describes the
    // hardware control.
    //
    // Control mappings from Reaper parameters and actions with status values
    // will generally work with any output type. However, each control mapping
    // will choose the output that best maps to what is being mapped. For
    // instance, numeric value mappings will always prefer a DValue over a
    // CValue output if it exists for precise display of min, max, and sometimes
    // center values (like pan). For this reason, there is no reason to specify
    // a CValue if a DValue is appropriate, and conversely a DValue should never
    // be specified if a CValue would result in better feedback. However, it can
    // make sense to have both a Color output and a DValue or CValue output, if
    // there is a small selection of preset colors or a well defined gradient
    // from min to max is desired for scalar values.
    std::unique_ptr<ControlCValueOutput> cvalue_output;
    std::unique_ptr<ControlDValueOutput> dvalue_output;
    std::unique_ptr<ControlTextOutput> text_output;
    std::unique_ptr<ControlColorOutput> color_output;
  };

  // Constructs a control with the given options.
  explicit Control(Options options) : options_(std::move(options)) {}
  Control(const Control&) = delete;
  Control& operator=(const Control&) = delete;
  virtual ~Control() = default;

  // Access to configuration options for this control.
  std::string_view GetName() const { return options_.name; }
  ControlValueInput* GetValueInput() const {
    return options_.value_input.get();
  }
  ControlDeltaInput* GetDeltaInput() const {
    return options_.delta_input.get();
  }
  ControlPressInput* GetPressInput() const {
    return options_.press_input.get();
  }
  OutputMode GetOutputMode() const { return options_.output_mode; }
  ControlCValueOutput* GetCValueOutput() const {
    return options_.cvalue_output.get();
  }
  ControlDValueOutput* GetDValueOutput() const {
    return options_.dvalue_output.get();
  }
  ControlTextOutput* GetTextOutput() const {
    return options_.text_output.get();
  }
  ControlColorOutput* GetColorOutput() const {
    return options_.color_output.get();
  }

 private:
  Options options_;
};

}  // namespace jpr
