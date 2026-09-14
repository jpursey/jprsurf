// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

namespace jpr {

class Control;

// RAII handle for a registered output writer on a Control.
//
// While a handle is registered, the Control has at least one writer and will
// not clear its outputs. When the handle is destroyed or reset, the writer is
// automatically unregistered (see Control::RegisterOutputWriter()).
//
// ControlOutputHandle is move-only.
class ControlOutputHandle final {
 public:
  ControlOutputHandle() = default;
  ControlOutputHandle(const ControlOutputHandle&) = delete;
  ControlOutputHandle& operator=(const ControlOutputHandle&) = delete;
  ControlOutputHandle(ControlOutputHandle&& other) noexcept;
  ControlOutputHandle& operator=(ControlOutputHandle&& other) noexcept;
  ~ControlOutputHandle();

  // Returns true if this handle is associated with a registered writer.
  bool IsRegistered() const { return control_ != nullptr; }

 private:
  friend class Control;
  explicit ControlOutputHandle(Control* control) : control_(control) {}

  Control* control_ = nullptr;
};

}  // namespace jpr
