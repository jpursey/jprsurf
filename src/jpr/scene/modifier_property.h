// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/modifiers.h"
#include "jpr/scene/view_property.h"

namespace jpr {

class ModifierProperty : public ViewProperty {
 public:
  // Default modifier properties for shift, ctrl, and alt keys.
  static constexpr std::string_view kShift = "mod_shift";
  static constexpr std::string_view kCtrl = "mod_ctrl";
  static constexpr std::string_view kAlt = "mod_alt";

  explicit ModifierProperty(std::string_view name, Modifiers modifier)
      : ViewProperty(name, Type::kToggle), modifier_(modifier) {}

 protected:
  bool ReadBool() const override { return AreModifiersOn(modifier_); }
  void WriteBool(bool value) override { 
    if (value == AreModifiersOn(modifier_)) {
      return;  // No change needed.
    }
    SetModifiers(modifier_, value); 
    NotifyChanged();
  }

 private:
  Modifiers modifier_;
};

}  // namespace jpr
