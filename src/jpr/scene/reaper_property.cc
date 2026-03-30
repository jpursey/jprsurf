// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/reaper_property.h"

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
// AnyTrackSoloProperty
//==============================================================================

void AnyTrackSoloProperty::UpdateState() {
  bool value = AnyTrackSolo(nullptr);
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool AnyTrackSoloProperty::ReadBool() const { return value_; }

void AnyTrackSoloProperty::WriteBool(bool value) {
  if (!value) {
    Main_OnCommand(40340, 0);  // Unsolo all tracks.
    UpdateState();
  }
}

}  // namespace jpr
