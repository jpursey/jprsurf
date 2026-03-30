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
// IsRulerModeProperty
//==============================================================================

void IsRulerModeProperty::UpdateState() {
  bool new_value = IsCurrentRulerMode(mode_);
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void IsRulerModeProperty::WriteBool(bool value) {
  if (value) {
    SetRulerMode(mode_);
    UpdateState();
  }
}

//==============================================================================
// IsSecondaryRulerModeProperty
//==============================================================================

void IsSecondaryRulerModeProperty::UpdateState() {
  bool new_value = IsCurrentRulerSecondaryMode(mode_);
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void IsSecondaryRulerModeProperty::WriteBool(bool value) {
  if (value) {
    SetRulerSecondaryMode(mode_);
  } else {
    if (value_) {
      ClearRulerSecondaryMode();
    }
  }
  UpdateState();
}

//==============================================================================
// RulerModeProperty
//==============================================================================

void RulerModeProperty::UpdateState() {
  int new_value = 0;
  switch (GetRulerMode()) {
    case TimelineMode::kBeats:
      new_value = 0;
      break;
    case TimelineMode::kTime:
      new_value = 1;
      break;
    case TimelineMode::kFrames:
      new_value = 2;
      break;
    case TimelineMode::kSamples:
      new_value = 3;
      break;
  }
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void RulerModeProperty::WriteInt(int value) {
  switch (value) {
    case 0:
      SetRulerMode(TimelineMode::kBeats);
      break;
    case 1:
      SetRulerMode(TimelineMode::kTime);
      break;
    case 2:
      SetRulerMode(TimelineMode::kFrames);
      break;
    case 3:
      SetRulerMode(TimelineMode::kSamples);
      break;
  }
  UpdateState();
}

std::string RulerModeProperty::GetText() const {
  switch (value_) {
    case 0:
      return "Beats";
    case 1:
      return "Time";
    case 2:
      return "Frames";
    case 3:
      return "Samples";
  }
  return "Beats";
}

//==============================================================================
// SecondaryRulerModeProperty
//==============================================================================

void SecondaryRulerModeProperty::UpdateState() {
  int new_value = 0;
  std::optional<TimelineMode> mode = GetRulerSecondaryMode();
  if (mode.has_value()) {
    switch (*mode) {
      case TimelineMode::kBeats:
        new_value = 0;
        break;
      case TimelineMode::kTime:
        new_value = 1;
        break;
      case TimelineMode::kFrames:
        new_value = 2;
        break;
      case TimelineMode::kSamples:
        new_value = 3;
        break;
    }
  }
  if (new_value != value_) {
    value_ = new_value;
    NotifyChanged();
  }
}

void SecondaryRulerModeProperty::WriteInt(int value) {
  switch (value) {
    case 0:
      ClearRulerSecondaryMode();
      break;
    case 1:
      SetRulerSecondaryMode(TimelineMode::kTime);
      break;
    case 2:
      SetRulerSecondaryMode(TimelineMode::kFrames);
      break;
    case 3:
      SetRulerSecondaryMode(TimelineMode::kSamples);
      break;
  }
  UpdateState();
}

std::string SecondaryRulerModeProperty::GetText() const {
  switch (value_) {
    case 0:
      return "None";
    case 1:
      return "Time";
    case 2:
      return "Frames";
    case 3:
      return "Samples";
  }
  return "None";
}

}  // namespace jpr
