// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/command_properties.h"

#include <memory>

#include "absl/strings/numbers.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

//==============================================================================
// CommandActionProperty
//==============================================================================

void CommandActionProperty::TriggerAction() { Main_OnCommand(command_id_, 0); }

//==============================================================================
// CommandToggleProperty
//==============================================================================

void CommandToggleProperty::UpdateState() {
  bool value = (GetToggleCommandState(command_id_) > 0);
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool CommandToggleProperty::ReadBool() const { return value_; }

void CommandToggleProperty::WriteBool(bool value) {
  if (value != value_) {
    Main_OnCommand(command_id_, 0);
    UpdateState();
  }
}

//==============================================================================
// CreateCommandProperty
//==============================================================================

std::unique_ptr<ViewProperty> CreateCommandProperty(Scene* scene,
                                                    std::string_view name) {
  int command_id = 0;
  if (!absl::SimpleAtoi(name.substr(kCmdNamespace.size()), &command_id) ||
      command_id == 0) {
    return nullptr;
  }
  int state = GetToggleCommandState(command_id);
  if (state < 0) {
    return std::make_unique<CommandActionProperty>(name, command_id);
  }
  return std::make_unique<CommandToggleProperty>(scene, name, command_id,
                                                 state > 0);
}

}  // namespace jpr
