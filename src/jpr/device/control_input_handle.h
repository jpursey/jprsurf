//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/modifiers.h"
#include "jpr/device/control_input.h"

namespace jpr {

class Control;

// Unique identifier for a registered input on a Control. This is used to query
// the virtual input state (value, delta, press count, pressed state) for a
// specific registration.
using InputId = int;

// Configuration for registering an input on a Control.
//
// This defines what physical input type to listen to, what modifier keys must
// be active for the input to trigger, and for press inputs, the press behavior
// (normal, long press, or double press).
struct InputConfig {
  // The type of physical input to listen to.
  ControlInput::Type input_type = ControlInput::Type::kPress;

  // The modifier keys that must be active for this input to trigger.
  //
  // When multiple inputs are registered for the same physical input type, the
  // set of required modifiers across all registrations is used to determine
  // mutual exclusivity. Specifically, each registration will only trigger when
  // its required modifiers are on AND all modifiers required by other
  // registrations (but not this one) are off.
  Modifiers required_modifiers = 0;

  // The press behavior for this input. This is only meaningful for press input
  // types and is ignored for value and delta inputs.
  enum class PressBehavior {
    // Normal press behavior. The press is delivered immediately unless a
    // sibling
    // registration (same input type and modifier set) requires deferral (e.g.
    // a long press or double press sibling exists).
    kNormal,

    // Long press behavior. The press is delivered only after the button has
    // been
    // held for a configurable duration.
    kLongPress,

    // Double press behavior. The press is delivered only after the button has
    // been pressed twice within a configurable time window.
    kDoublePress,
  };
  PressBehavior press_behavior = PressBehavior::kNormal;
};

// RAII handle for a registered input on a Control.
//
// This class manages the lifetime of an input registration. When the handle is
// destroyed, the input is automatically unregistered from the Control. The
// handle also provides access to the InputId which is used to query the virtual
// input state via Control::GetValue(), Control::GetDelta(),
// Control::GetPressCount(), and Control::IsPressed().
//
// ControlInputHandle is move-only.
class ControlInputHandle final {
 public:
  ControlInputHandle() = default;
  ControlInputHandle(const ControlInputHandle&) = delete;
  ControlInputHandle& operator=(const ControlInputHandle&) = delete;
  ControlInputHandle(ControlInputHandle&& other) noexcept;
  ControlInputHandle& operator=(ControlInputHandle&& other) noexcept;
  ~ControlInputHandle();

  // Returns true if this handle is associated with a registered input.
  bool IsRegistered() const { return control_ != nullptr; }

  // Returns the InputId for this registration. This is only valid if
  // IsRegistered() returns true.
  InputId GetId() const { return id_; }

 private:
  friend class Control;
  ControlInputHandle(Control* control, InputId id)
      : control_(control), id_(id) {}

  Control* control_ = nullptr;
  InputId id_ = 0;
};

}  // namespace jpr
