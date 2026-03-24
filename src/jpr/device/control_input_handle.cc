//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input_handle.h"

#include "jpr/device/control.h"

namespace jpr {

ControlInputHandle::ControlInputHandle(ControlInputHandle&& other) noexcept
    : control_(other.control_), id_(other.id_) {
  other.control_ = nullptr;
  other.id_ = 0;
}

ControlInputHandle& ControlInputHandle::operator=(
    ControlInputHandle&& other) noexcept {
  if (this != &other) {
    if (control_ != nullptr) {
      control_->UnregisterInput(id_);
    }
    control_ = std::exchange(other.control_, nullptr);
    id_ = std::exchange(other.id_, 0);
  }
  return *this;
}

ControlInputHandle::~ControlInputHandle() {
  if (control_ != nullptr) {
    control_->UnregisterInput(id_);
  }
}

}  // namespace jpr
