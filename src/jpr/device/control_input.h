// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/functional/any_invocable.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// ControlInput
//==============================================================================

// This is the base class for all control input types. It defines what types of
// values a control will return and the behavior of the input.
class ControlInput {
 public:
  // The type of input, which defines the behavior of the control. This directly
  // corresponds to a derived class which implements the input.
  enum class Type {
    // Continusous absolute value in the range [0.0, 1.0]. Implemented by
    // ControlValueInput.
    kValue,

    // Relative value which can be positive or negative. Implemented by
    // ControlDeltaInput.
    kDelta,

    // Value that can be pressed and optionally released. Implemented by
    // ControlPressInput.
    kPress,
  };

  ControlInput(const ControlInput&) = delete;
  ControlInput& operator=(const ControlInput&) = delete;
  virtual ~ControlInput() = default;

  // Returns the type of this control input, which defines the behavior of the
  // control. Callers can use this to cast to the appropriate derived class to
  // access the value of the input.
  virtual Type GetType() const = 0;

 protected:
  ControlInput() = default;
};

//==============================================================================
// ControlValueInput
//==============================================================================

// This control input type represents a continuous absolute value in the range
// [0.0, 1.0].
//
// This input generaly corresponds to the position of a physical control (e.g.
// how far a fader is pushed or where a knob is turned to).
class ControlValueInput : public ControlInput {
 public:
  using Listener = absl::AnyInvocable<void(ControlValueInput* input)>;

  virtual ~ControlValueInput() = default;

  // Sets the listener for this input, which will be called whenever the value
  // of the input changes.
  //
  // The listener will be passed a pointer to this input, and can query the
  // current value of the input with GetValue().
  //
  // The listener must outlive the input or until SetListener() is called again
  // to change the listener (which may be nullptr).
  void SetListener(Listener listener) { listener_ = std::move(listener); }

  // Returns true if a listener is currently set for this input.
  bool HasListener() const { return listener_ != nullptr; }

  // Returns the current value of this input, which is always in the range
  // [0.0, 1.0].
  double GetValue() const { return value_; }

  // Implements ControlInput.
  Type GetType() const override { return Type::kValue; }

 protected:
  ControlValueInput() = default;

  // Derived classes should call this to update the value of the input.
  void SetValue(double value);

 private:
  Listener listener_;
  double value_ = 0.0;
};

//==============================================================================
// ControlDeltaInput
//==============================================================================

// This control input type represents relative value, which can be positive or
// negative.
//
// This input generally corresponds to the movement of an endless encoder, with
// values indicating the last distance it was moved. The amount moved may change
// from event to event and is accumulated until it is retrieved.
class ControlDeltaInput : public ControlInput {
 public:
  using Listener = absl::AnyInvocable<void(ControlDeltaInput*)>;

  virtual ~ControlDeltaInput() = default;

  // Sets the listener for this input, which will be called whenever the value
  // of the input changes.
  //
  // The listener will be passed a pointer to this input, and can read and clear
  // the current accumulated delta value of the input with ReadDelta().
  //
  // The listener must outlive the input or until SetListener() is called again
  // to change the listener (which may be nullptr).
  void SetListener(Listener listener) { listener_ = std::move(listener); }

  // Returns true if a listener is currently set for this input.
  bool HasListener() const { return listener_ != nullptr; }

  // Returns the current accumulated delta value of this input, which can be
  // positive or negative, and resets the delta back to zero.
  double ReadDelta() { return std::exchange(delta_, 0.0); }

  // Returns the current accumulated delta value of this input, which can be
  // positive or negative. It does not reset the value.
  double PeekDelta() const { return delta_; }

  // Resets the accumulated delta value to zero.
  void ResetDelta() { delta_ = 0.0; }

  // Implements ControlInput.
  Type GetType() const override { return Type::kDelta; }

 protected:
  ControlDeltaInput() = default;

  // Derived classes should call this to update the accumulated delta value of
  // the input.
  void AddDelta(double delta);

 private:
  Listener listener_;
  double delta_ = 0.0;
};

//==============================================================================
// ControlPressInput
//==============================================================================

// This control input type represents a value can be pressed and released.
//
// This input generally corresponds to a momentary button, with the value
// indicating whether the button is currently pressed.
//
// Note that not all press inputs support a release signal (this can be queried
// with HasRelease()), in which case it will always report that it is *not*
// pressed. However, in all cases this input accumulates all press and release
// events which can be queried.
class ControlPressInput : public ControlInput {
 public:
  using Listener = absl::AnyInvocable<void(ControlPressInput*)>;

  ControlPressInput() = default;
  virtual ~ControlPressInput() = default;

  // Sets the listener for this input, which will be called whenever the value
  // of the input changes.
  //
  // The listener will be passed a pointer to this input, and can query the
  // current pressed state of the input with IsPressed(), and the total number
  // of press and release events with GetPressCount() and GetReleaseCount().
  //
  // The listener must outlive the input or until SetListener() is called again
  // to change the listener (which may be nullptr).
  void SetListener(Listener listener) { listener_ = std::move(listener); }

  // Returns true if a listener is currently set for this input.
  bool HasListener() const { return listener_ != nullptr; }

  // Returns true if this input supports release signals. If this returns false,
  // the input will always report that it is not pressed, and the caller should
  // only rely on press events.
  bool HasRelease() const { return has_release_; }

  // Returns true if the input is currently pressed. This is only meaningful if
  // the input supports release signals (which can be queried with
  // HasRelease()). If release signals are not supported, this will always
  // return false.
  bool IsPressed() const { return pressed_; }

  // Returns the total number of times this input has been pressed since the
  // last time it was reset.
  int GetPressCount() const { return press_count_; }

  // Returns the total number of times this input has been released since the
  // last time it was reset.
  int GetReleaseCount() const { return release_count_; }

  // Resets all press and release counts to zero.
  void ResetCounts() {
    press_count_ = 0;
    release_count_ = 0;
  }

  // Implements ControlInput.
  Type GetType() const override { return Type::kPress; }

 protected:
  explicit ControlPressInput(bool has_release) : has_release_(has_release) {}

  // Derived classes should call this whenever the control is pressed. If the
  // control is already pressed, this will not do anything.
  void Press();

  // Derived classes should call this whenever the control is released. If the
  // control is not currently pressed, or it is configured without release
  // support, this will not do anything.
  void Release();

 private:
  Listener listener_;
  bool has_release_ = false;
  bool pressed_ = false;
  int press_count_ = 0;
  int release_count_ = 0;
};

}  // namespace jpr
