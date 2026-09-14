// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_handle.h"

#include <utility>

#include "jpr/device/control.h"

namespace jpr {

ControlOutputHandle::ControlOutputHandle(ControlOutputHandle&& other) noexcept
    : control_(std::exchange(other.control_, nullptr)) {}

ControlOutputHandle& ControlOutputHandle::operator=(
    ControlOutputHandle&& other) noexcept {
  if (this != &other) {
    if (control_ != nullptr) {
      control_->UnregisterOutputWriter();
    }
    control_ = std::exchange(other.control_, nullptr);
  }
  return *this;
}

ControlOutputHandle::~ControlOutputHandle() {
  if (control_ != nullptr) {
    control_->UnregisterOutputWriter();
  }
}

}  // namespace jpr
