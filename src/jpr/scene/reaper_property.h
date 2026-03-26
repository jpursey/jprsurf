// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/scene/scene_state_property.h"

namespace jpr {

//==============================================================================
// CommandActionProperty
//==============================================================================

// This property represents a REAPER action that can be triggered. It has no
// state, and so can only be mapped to control inputs.
class CommandActionProperty final : public ViewProperty {
 public:
  CommandActionProperty(std::string_view name, int command_id)
      : ViewProperty(name, Type::kAction), command_id_(command_id) {}
  ~CommandActionProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void TriggerAction() override;

 private:
  int command_id_;
};

//==============================================================================
// CommandToggleProperty
//==============================================================================

// This property represents a REAPER action with a toggle state. It can be
// mapped to control inputs and outputs that represent a binary value.
class CommandToggleProperty final : public SceneStateProperty {
 public:
  CommandToggleProperty(Scene* scene, std::string_view name, int command_id,
                        bool value)
      : SceneStateProperty(scene, name, Type::kToggle),
        command_id_(command_id),
        value_(value) {}
  ~CommandToggleProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override;
  void WriteBool(bool value) override;

 private:
  int command_id_;
  bool value_;
};

}  // namespace jpr
