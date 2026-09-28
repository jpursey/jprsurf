// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/command_properties.h"

#include <memory>
#include <string>

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

namespace {

// Returns the command id for the id in a "cmd:<id>" name, or 0 if REAPER has
// no such command.
int LookupCommandId(std::string_view id) {
  if (id.starts_with('_')) {
    return NamedCommandLookup(std::string(id).c_str());
  }
  int command_id = 0;
  if (!absl::SimpleAtoi(id, &command_id) || command_id <= 0) {
    return 0;
  }
  // REAPER has no text for a numeric id it doesn't know.
  const char* text = kbd_getTextFromCmd(command_id, nullptr);
  if (text == nullptr || text[0] == '\0') {
    return 0;
  }
  return command_id;
}

}  // namespace

std::unique_ptr<ViewProperty> CreateCommandProperty(Scene* scene,
                                                    std::string_view name) {
  int command_id = LookupCommandId(name.substr(kCmdNamespace.size()));
  if (command_id == 0) {
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
