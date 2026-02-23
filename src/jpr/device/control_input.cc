// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input.h"

namespace jpr {

namespace {

template <typename InputType>
void NotifyListener(InputType* input, typename InputType::Listener& listener) {
  if (listener != nullptr) {
    listener(input);
  }
}

}  // namespace

void ControlValueInput::SetValue(double value) {
  value_ = value;
  NotifyListener(this, listener_);
}

void ControlDeltaInput::AddDelta(double delta) {
  delta_ += delta;
  NotifyListener(this, listener_);
}

void ControlPressInput::Press() {
  if (!pressed_) {
    // If there is no release signal, pressed_ is always false.
    pressed_ = has_release_;
    ++press_count_;
    NotifyListener(this, listener_);
  }
}

void ControlPressInput::Release() {
  if (pressed_ && has_release_) {
    pressed_ = false;
    ++release_count_;
    NotifyListener(this, listener_);
  }
}

}  // namespace jpr