// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/anchor.h"

#include <utility>

namespace jpr {

//==============================================================================
// AnchorHold
//==============================================================================

AnchorHold::AnchorHold(AnchorBase* anchor) : anchor_(anchor) {
  anchor_->hold_ = this;
}

AnchorHold::AnchorHold(AnchorHold&& other)
    : anchor_(std::exchange(other.anchor_, nullptr)) {
  if (anchor_ != nullptr) {
    anchor_->hold_ = this;
  }
}

AnchorHold& AnchorHold::operator=(AnchorHold&& other) {
  if (this != &other) {
    Reset();
    anchor_ = std::exchange(other.anchor_, nullptr);
    if (anchor_ != nullptr) {
      anchor_->hold_ = this;
    }
  }
  return *this;
}

void AnchorHold::Reset() {
  if (anchor_ != nullptr) {
    anchor_->Clear();
  }
}

//==============================================================================
// AnchorBase
//==============================================================================

void AnchorBase::Clear() {
  if (held_ == nullptr) {
    return;
  }
  hold_->anchor_ = nullptr;
  hold_ = nullptr;
  held_ = nullptr;
  SetModifiers(std::exchange(modifier_, 0), false);
}

AnchorHold AnchorBase::DoHold(void* object, Modifiers modifier) {
  if (object == nullptr || held_ != nullptr) {
    return {};
  }
  held_ = object;
  modifier_ = modifier;
  SetModifiers(modifier_, true);
  return AnchorHold(this);
}

}  // namespace jpr
