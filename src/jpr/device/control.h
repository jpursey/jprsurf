// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gb/base/flags.h"
#include "jpr/common/color.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output.h"

namespace jpr {

//==============================================================================
// Control
//==============================================================================

// A control represents a single physical control on a device, such as a button,
// a fader, a knob, or a display.
//
// It defines the inputs and outputs of the control, and the behavior of the
// outputs relative to the inputs. A control may have multiple inputs and
// outputs, but it should be configured with the minimum set of inputs and
// outputs that best describes the hardware control.
//
// Examples:
//   - A simple button that doesn't light up would be a control with a Press
//     input and no outputs.
//   - A simple button with an indicator light would be a control with a Press
//     input and a DValue output for the light.
//   - A motorized touch knob or fader would be a control with a Value input and
//     a Press input for the touch signal, and a Value output for the motorized
//     feedback.
//   - An endless encoder with a light ring would be a control with a single
//     Delta input and a DValue output for the light ring.
//   - A text screen display would be a control with a Text output and no
//     inputs.
class Control final {
 public:
  //----------------------------------------------------------------------------
  // Options
  //----------------------------------------------------------------------------

  // The binding for a control defines how the output(s) of the control behave
  // relative to the inputs. This is important for controls that have outputs
  // that directly affect the physical control and/or the hardware input state.
  enum class Binding {
    // The output is completely independent of the input, and can be set at any
    // time without affecting the physical control or subsequent input values.
    // For example, an indicator led on a button or a light ring around an
    // endless encoder.
    kIndependent,

    // The output is directly tied to the input, and setting the output will
    // affect the input value. If a Press input is present, the output will only
    // be updated when it is not pressed. If no Press input is present, the
    // output will be delayed until after input events have stopped for several
    // frames.
    kDependent,

    // The output is directly tied to the input, and setting the output will
    // affect the input value. This is typically used for motorized faders or
    // other feedback-driven controls. This is behaviorly the same as
    // kDependent, except that if a Press input is not present, a much longer
    // delay is used after input events have stopped.
    kMotorized,
  };

  struct Options {
    // Name of the control.
    //
    // This should be a short, human-readable string that describes the control,
    // such as "Play" or "Fader1". This is required to be unique within a
    // Device, but does not need to be globally unique.
    std::string_view name;

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
    //   - If both a Value and Delta input are specified, then the control
    //     supports both an absolute min/max value and it can emulate an encoder
    //     delta. For instance, some endless encoders have an internal "value"
    //     tracked on the hardware, but also will continue sending the min or
    //     max value when twisted farther. This allows delta motion to be
    //     emulated.
    //   - If a Press input is specified along with either a Value or Delta
    //     input, then the control may be touched or pressed. This configuration
    //     is recommended only in combination with the kDependent or kMotorized
    //     bindings, and only when it is a true capacitive touch control.
    //     On the other hand, this should generally not be used to define a
    //     pushable endless encoder. Those should be more flexibly represented
    //     as two separate controls so they can be mapped independently, as
    //     mappings are per-control.
    //
    // Control mappings from Reaper parameters and actions can generally work
    // with any input type, but will choose the input(s) that best maps to the
    // Reaper parameter being mapped. For instance, the "pan" mapping will
    // prefer a Value input over a Delta input, over a Press input. The "play"
    // action however will prefer Press over Delta over Value. In both cases,
    // they will all work however.
    std::unique_ptr<ControlValueInput> value_input;
    std::unique_ptr<ControlDeltaInput> delta_input;
    std::unique_ptr<ControlPressInput> press_input;

    // The binding for this control, which defines how the output(s) of this
    // control behave relative to the inputs. See Binding for details.
    Binding binding = Binding::kIndependent;

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
    // Control mappings from Reaper parameters and actions (with status values)
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

  // Settings for which inputs and outputs a control has. This is used by
  // control mappings to determine which controls they can map to, and how to
  // best map to them.
  using Inputs = gb::Flags<ControlInput::Type>;
  using Outputs = gb::Flags<ControlOutput::Type>;

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  // Constructs a control with the given options.
  Control(RunRegistry& run_registry, Options options);
  Control(const Control&) = delete;
  Control& operator=(const Control&) = delete;
  ~Control();

  //----------------------------------------------------------------------------
  // Properties
  //----------------------------------------------------------------------------

  // Access to configuration options for this control.
  std::string_view GetName() const { return name_; }
  Inputs GetInputs() const { return input_types_; }
  bool HasPressRelease() const {
    return press_input_ != nullptr && press_input_->HasRelease();
  }
  Outputs GetOutputs() const { return output_types_; }
  Binding GetBinding() const { return binding_; }

  //----------------------------------------------------------------------------
  // Input accessors
  //----------------------------------------------------------------------------

  // Registers an input with the given configuration. The returned handle
  // manages the lifetime of the registration; when the handle is destroyed,
  // the input is automatically unregistered.
  //
  // The flag pointer will be set to true whenever the virtual input has new
  // data available. The flag pointer must remain valid for the lifetime of the
  // returned handle.
  //
  // The returned handle's GetId() can be passed to GetValue(), GetDelta(),
  // GetPressCount(), and IsPressed() to query the virtual input state.
  ControlInputHandle RegisterInput(const InputConfig& config, bool* flag);

  // Unregisters the input with the given ID. This is called automatically by
  // ControlInputHandle's destructor; it should not normally be called directly.
  void UnregisterInput(InputId id);

  // Returns the current value of the Value input for the given registered
  // input, or 0.0 if the input ID is not valid or not a Value input.
  double GetValue(InputId id) const;

  // Returns the current accumulated delta value of the Delta input for the
  // given registered input, or 0.0 if the input ID is not valid or not a Delta
  // input, or it has not changed since the last sync.
  double GetDelta(InputId id) const;

  // Returns the press count of the Press input for the given registered input,
  // or 0 if the input ID is not valid or not a Press input, or it has not been
  // pressed since the last sync.
  int GetPressCount(InputId id) const;

  // Returns true if the Press input for the given registered input is currently
  // pressed, or false if the input ID is not valid or not a Press input, or it
  // is not currently pressed, or it does not support release signals.
  bool IsPressed(InputId id) const;

  //----------------------------------------------------------------------------
  // Output accessors
  //----------------------------------------------------------------------------

  // Returns the number of modes supported by this control's outputs. This is
  // the maximum mode count across all output types.
  int GetModeCount() const;

  // Sets the value of the CValue output, if it exists. If there is no CValue
  // output, this does nothing.
  void SetCValue(double value, int mode = 0);

  // Returns the maximum value of the DValue output for the given mode, or 0 if
  // there is no DValue output.
  int GetDValueMaxValue(int mode = 0) const;

  // Sets the value of the DValue output, if it exists. If there is no DValue
  // output, this does nothing.
  void SetDValue(int value, int mode = 0);

  // Sets the text of the Text output, if it exists. If there is no Text
  // output, this does nothing.
  void SetText(std::string_view text, int mode = 0);

  // Sets the color of the Color output, if it exists. If there is no Color
  // output, this does nothing.
  void SetColor(Color color, int mode = 0);

 private:
  friend class ControlInputHandle;

  // Per-registration state for a virtual input.
  struct InputRegistration {
    InputConfig config;
    bool* flag = nullptr;

    // Computed modifier masks (recomputed when registrations change).
    // This registration triggers when all on_mask modifiers are on AND all
    // off_mask modifiers are off.
    Modifiers on_mask = 0;
    Modifiers off_mask = 0;

    // Virtual input state.
    double value = 0.0;       // For kValue inputs.
    double delta = 0.0;       // For kDelta inputs.
    int press_count = 0;      // For kPress inputs.
    bool is_pressed = false;  // For kPress inputs.
  };

  // Press timing state for a group of press registrations that share the same
  // modifier on/off masks. This tracks whether we need to defer press delivery
  // due to long-press or double-press siblings.
  struct PressGroup {
    // Press timing state machine.
    enum class State {
      kIdle,
      // Physical button is held, waiting for release or long press timeout.
      kPendingLong,
      // Physical button is held, waiting for release only (no long-press
      // sibling). Times out after the double press window, delivering a normal
      // press, since a double press is no longer possible.
      kPendingRelease,
      // Short press released, waiting for second press or double-press timeout.
      kPendingDouble,
    };

    Modifiers on_mask = 0;
    Modifiers off_mask = 0;

    // IDs of registrations in this group, by press behavior.
    std::vector<InputId> normal_ids;
    std::vector<InputId> long_press_ids;
    std::vector<InputId> double_press_ids;

    State state = State::kIdle;
    double state_start_time = 0.0;

    // The number of press events that occurred during the first press (should
    // be 1, but tracked for correctness).
    int pending_press_count = 0;
  };

  struct PendingOutput {
    std::variant<double, int, std::string, Color> value;
    int mode = 0;
  };

  void OnRun(const RunTime& time);
  void UpdateRunHandle();
  void ResetVirtualInputs();
  void UpdatePressTimers(double current_time);
  void SendPendingOutput();
  void SetInputListener(ControlInput::Type input_type);
  void ClearInputListener(ControlInput::Type input_type);
  void OnValueInputChanged();
  void OnDeltaInputChanged();
  void OnPressInputChanged();
  void OnPressInputChangedWithRelease();
  void OnPressInputChangedWithoutRelease();
  void RecomputeModifierMasks(ControlInput::Type input_type);
  void RebuildPressGroups();
  bool CheckModifiers(const InputRegistration& reg) const;
  PressGroup* FindPressGroup(Modifiers on_mask, Modifiers off_mask);
  void DeliverNormalPress(PressGroup& group);
  void DeliverLongPress(PressGroup& group);
  void DeliverDoublePress(PressGroup& group);
  bool HasRegistrations() const { return !registrations_.empty(); }

  // Constructed state.
  RunRegistry& run_registry_;
  const std::string name_;
  const std::unique_ptr<ControlValueInput> value_input_;
  const std::unique_ptr<ControlDeltaInput> delta_input_;
  const std::unique_ptr<ControlPressInput> press_input_;
  const std::unique_ptr<ControlCValueOutput> cvalue_output_;
  const std::unique_ptr<ControlDValueOutput> dvalue_output_;
  const std::unique_ptr<ControlTextOutput> text_output_;
  const std::unique_ptr<ControlColorOutput> color_output_;
  const Binding binding_;
  const double binding_delay_;
  Inputs input_types_;
  Outputs output_types_;

  // Runtime state.
  RunHandle run_handle_;
  double last_run_time_ = 0.0;
  std::optional<double> last_input_time_;
  std::optional<PendingOutput> pending_output_;

  // Input registration state.
  int next_input_id_ = 1;
  absl::flat_hash_map<InputId, InputRegistration> registrations_;

  // Press timing groups (rebuilt when press registrations change).
  std::vector<PressGroup> press_groups_;
};

}  // namespace jpr
