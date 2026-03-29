// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/timeline_property.h"

namespace jpr {

//==============================================================================
// TimelinePositionProperty
//==============================================================================

void TimelinePositionProperty::UpdateState() {
  TimelinePosition new_position;
  switch (source_) {
    case Source::kCurrent:
      new_position = TimelinePosition::Get();
      break;
    case Source::kPlayback:
      new_position = TimelinePosition::GetPlayback();
      break;
    case Source::kEdit:
      new_position = TimelinePosition::GetEdit();
      break;
  }
  bool changed = false;
  if (new_position.GetValue() != position_.GetValue()) {
    position_ = new_position;
    changed = true;
  }

  // We treate a mode change as a position property change, as mappings on the
  // position may also depend on the mode. This ensures they get updated in that
  // case. Timeline mode changes are extremely infrequent, so even if they
  // don't this is pretty harmless.
  TimelineMode new_mode = GetRulerMode();
  if (!mode_.has_value() || *mode_ != new_mode) {
    mode_ = new_mode;
    changed = true;
  }

  if (changed) {
    NotifyChanged();
  }
}

//==============================================================================
// RulerModeProperty
//==============================================================================

void RulerModeProperty::UpdateState() {
  bool new_value = IsCurrentRulerMode(mode_);
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void RulerModeProperty::WriteBool(bool value) {
  if (value) {
    SetRulerMode(mode_);
    UpdateState();
  }
}

//==============================================================================
// SecondaryRulerModeProperty
//==============================================================================

void SecondaryRulerModeProperty::UpdateState() {
  bool new_value = IsCurrentRulerSecondaryMode(mode_);
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void SecondaryRulerModeProperty::WriteBool(bool value) {
  if (value) {
    SetRulerSecondaryMode(mode_);
  } else {
    if (value_) {
      ClearRulerSecondaryMode();
    }
  }
  UpdateState();
}

}  // namespace jpr
