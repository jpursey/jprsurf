// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include "jpr/scene/view_control.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

void NoOpSyncFunction(ViewProperty& property, ViewControl& control) {}

// 10dB above unity gain, which matches the X-Touch faders.
constexpr double kMaxVolume = 3.16228;

}  // namespace

ViewMapping::ViewMapping(TypeFlags type, ViewProperty* property,
                         ViewControl* control)
    : type_(type),
      property_(property),
      control_(control),
      read_control_(NoOpSyncFunction),
      write_control_(NoOpSyncFunction) {
  InitReadControl();
  InitWriteControl();
}

ViewMapping::~ViewMapping() { Deactivate(); }

void ViewMapping::InitReadControl() {
  if (!type_.IsSet(kReadControl)) {
    return;
  }
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      InitReadActionSyncFunction();
      return;
    case ViewProperty::Type::kToggle:
      InitReadToggleSyncFunction();
      break;
    case ViewProperty::Type::kPan:
      InitReadPanSyncFunction();
      break;
    case ViewProperty::Type::kVolume:
      InitReadVolumeSyncFunction();
      break;
    case ViewProperty::Type::kNormalized:
      InitReadNormalizedSyncFunction();
      break;
    case ViewProperty::Type::kText:
      InitReadTextSyncFunction();
      break;
    case ViewProperty::Type::kColor:
      InitReadColorSyncFunction();
      break;
  }
}

void ViewMapping::InitReadActionSyncFunction() {
  Control* control = control_->GetControl();

  // Only press inputs make sense for triggering actions.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    // If there is a press input, we trigger the action on every press event.
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      if (press_input != nullptr && press_input->GetPressCount() > 0) {
        property.TriggerAction();
        press_input->ResetCounts();
      }
    };
    return;
  }
}

void ViewMapping::InitReadToggleSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a press input, we toggle the property on each press event.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      if (press_input->GetPressCount() % 2 != 0) {
        property.SetBool(!property.GetBool());
      }
      press_input->ResetCounts();
    };
    return;
  }

  // If there is a delta input, we set the property to false when the delta is
  // negative, and true when the delta is positive.
  if (auto* delta_input = control->GetDeltaInput(); delta_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double delta = control.GetControl()->GetDeltaInput()->ReadDelta();
      if (delta != 0) {
        property.SetBool(delta > 0);
      }
    };
    return;
  }

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      property.SetNormalized(control.GetControl()->GetValueInput()->GetValue());
    };
    return;
  }
}

void ViewMapping::InitReadPanSyncFunction() {
  Control* control = control_->GetControl();

  // The value input is preferred for pan controls, if it is available, which
  // maps [0,1] to [-1,1].
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double value = control.GetControl()->GetValueInput()->GetValue();
      property.SetPan(value * 2.0 - 1.0);
    };
    return;
  }

  // If there is a delta input, we add the delta to the current pan value.
  if (auto* delta_input = control->GetDeltaInput(); delta_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double delta = control.GetControl()->GetDeltaInput()->ReadDelta();
      if (delta != 0) {
        property.SetPan(property.GetPan() + delta);
      }
    };
    return;
  }

  // If there is a press input, we alternate between left, center, and right pan
  // on each press.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();

      // Get the current press count and reset it.
      int press_count = press_input->GetPressCount() % 3;
      press_input->ResetCounts();
      if (press_count == 0) {
        return;
      }

      // Calculate the new pan value based on the current pan and the number of
      // presses. We cycle through left, center, and right pan on each press.
      double current_pan = property.GetPan();
      double new_pan = current_pan;
      for (; press_count > 0; --press_count) {
        if (current_pan < 0.0) {
          new_pan = 0.0;
        } else if (current_pan < 1.0) {
          new_pan = 1.0;
        } else {
          new_pan = -1.0;
        }
      }

      // Set the new pan value if it has changed.
      if (new_pan != current_pan) {
        property.SetPan(new_pan);
      }
    };
    return;
  }
}

void ViewMapping::InitReadVolumeSyncFunction() {
  Control* control = control_->GetControl();
  // The value input is preferred for volume controls, if it is available, which
  // maps [0,1] to [0,kMaxVolume].
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double value = control.GetControl()->GetValueInput()->GetValue();
      property.SetVolume(value * kMaxVolume);
    };
    return;
  }

  // If there is a delta input, we add the delta to the current volume value.
  if (auto* delta_input = control->GetDeltaInput(); delta_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double delta = control.GetControl()->GetDeltaInput()->ReadDelta();
      if (delta != 0) {
        property.SetVolume(property.GetVolume() + delta);
      }
    };
    return;
  }

  // If there is a press input, we toggle between silence and unity gain.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      int press_count = press_input->GetPressCount() % 2;
      press_input->ResetCounts();
      if (press_count == 0) {
        return;
      }
      property.SetVolume(property.GetVolume() > 0.0 ? 0.0 : 1.0);
    };
    return;
  }
}

void ViewMapping::InitReadNormalizedSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      property.SetNormalized(control.GetControl()->GetValueInput()->GetValue());
    };
    return;
  }

  // If there is a delta input, we add the delta to the current value.
  if (auto* delta_input = control->GetDeltaInput(); delta_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double delta = control.GetControl()->GetDeltaInput()->ReadDelta();
      if (delta != 0) {
        property.SetNormalized(property.GetNormalized() + delta);
      }
    };
    return;
  }

  // If there is a press input, we toggle between the minimum and maximum value.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      int press_count = press_input->GetPressCount() % 2;
      press_input->ResetCounts();
      if (press_count == 0) {
        return;
      }
      property.SetNormalized(property.GetNormalized() > 0.0 ? 0.0 : 1.0);
    };
    return;
  }
}

void ViewMapping::InitReadTextSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      property.SetNormalized(control.GetControl()->GetValueInput()->GetValue());
    };
    return;
  }

  // The other input types don't make much sense for text properties, so we
  // don't support them.
}

void ViewMapping::InitReadColorSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a value input, we just set the value and let the property
  // handle the conversion.
  if (auto* value_input = control->GetValueInput(); value_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      property.SetNormalized(control.GetControl()->GetValueInput()->GetValue());
    };
    return;
  }

  // For a delta input, we let the property convert it to and from a normalized
  // value.
  if (auto* delta_input = control->GetDeltaInput(); delta_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      double delta = control.GetControl()->GetDeltaInput()->ReadDelta();
      if (delta != 0) {
        property.SetNormalized(property.GetNormalized() + delta);
      }
    };
    return;
  }

  // For a press input, we let the property convert it to and from a boolean
  // value.
  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      int press_count = press_input->GetPressCount() % 2;
      press_input->ResetCounts();
      if (press_count == 1) {
        property.SetBool(!property.GetBool());
      }
    };
    return;
  }
}

void ViewMapping::InitWriteControl() {
  if (!type_.IsSet(kWriteControl)) {
    return;
  }
  switch (property_->GetType()) {
    case ViewProperty::Type::kAction:
      // Action properties have no state, so they don't need to write to the
      // control.
      return;
    case ViewProperty::Type::kToggle:
      InitWriteToggleSyncFunction();
      break;
    case ViewProperty::Type::kPan:
      InitWritePanSyncFunction();
      break;
    case ViewProperty::Type::kVolume:
      InitWriteVolumeSyncFunction();
      break;
    case ViewProperty::Type::kNormalized:
      InitWriteNormalizedSyncFunction();
      break;
    case ViewProperty::Type::kText:
      InitWriteTextSyncFunction();
      break;
    case ViewProperty::Type::kColor:
      InitWriteColorSyncFunction();
      break;
  }
}

void ViewMapping::InitWriteToggleSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a dvalue output, we set it to the max value when the property
  // is true, and 0 when the property is false.
  if (auto* dvalue_output = control->GetDValueOutput();
      dvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      auto dvalue_output = control.GetControl()->GetDValueOutput();
      dvalue_output->SetValue(property.GetBool() ? dvalue_output->GetMaxValue()
                                                 : 0);
    };
    return;
  }

  // If there is a cvalue output, we set it to 1.0 when the property is true,
  // and 0.0 when the property is false.
  if (auto* cvalue_output = control->GetCValueOutput();
      cvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetCValueOutput()->SetValue(
          property.GetNormalized());
    };
    return;
  }

  // If there is a text output, we set it to "On" when the property is true, and
  // "Off" when the property is false.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // If there is a color output, we set it to white when the property is true,
  // and black when the property is false.
  if (auto* color_output = control->GetColorOutput(); color_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetColorOutput()->SetColor(property.GetColor());
    };
    return;
  }
}

namespace {

int MapPanToTwoStates(double pan) {
  if (pan < 0.0) {
    return 0;  // Left of center
  }
  return 1;  // Center or right of center
}

int MapPanToThreeStates(double pan) {
  if (pan < -0.65) {
    return 0;  // Left
  } else if (pan > 0.65) {
    return 2;  // Right
  }
  return 1;  // Center
}

int MapPanToEvenRange(double pan, int max) {
  if (pan <= -1.0) {
    return 0;
  }
  if (pan >= 1.0) {
    return max;
  }

  // Determine the number of remaining intermediate values.
  int value_count = max - 1;
  int value = (pan + 1.0) * 0.5;  // Shift pan to the range (0,1)
  return static_cast<int>(std::ceil(value * value_count));
}

int MapPanToOddRange(double pan, int max) {
  int center = (max + 1) / 2;

  // Reserve specific values for hard left, center, and hard right to ensure
  // they always map to a single index.
  if (pan <= -1.0) {
    return 0;
  }
  if (pan >= 1.0) {
    return max;
  }
  if (pan == 0.0) {
    return center;
  }

  // Determine the number of values on either side of the center.
  int value_count = center - 1;

  // Determine the number of steps from the center, symmetrically for left and
  // right. This will always be greater than zero, as we check for pan == 0.0
  // above.
  int value = static_cast<int>(std::ceil(std::abs(pan) * value_count));
  if (pan < 0.0) {
    return center - value;
  }
  return center + value;
}

}  // namespace

void ViewMapping::InitWritePanSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a dvalue output, we clamp the pan value hard left, center, and
  // hard right to explicit values if possible, and then interpolate for values
  // in between.
  if (auto* value_output = control->GetDValueOutput();
      value_output != nullptr) {
    int max_value = value_output->GetMaxValue();
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapPanToTwoStates(property.GetPan()));
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapPanToThreeStates(property.GetPan()));
      };
    } else if (max_value % 2 == 1) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        auto* dvalue_output = control.GetControl()->GetDValueOutput();
        dvalue_output->SetValue(
            MapPanToEvenRange(property.GetPan(), dvalue_output->GetMaxValue()));
      };
    } else {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        auto* dvalue_output = control.GetControl()->GetDValueOutput();
        dvalue_output->SetValue(
            MapPanToOddRange(property.GetPan(), dvalue_output->GetMaxValue()));
      };
    }
    return;
  }

  // If there is a cvalue output, we map the pan value from [-1,1] to [0,1].
  if (auto* cvalue_output = control->GetCValueOutput();
      cvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetCValueOutput()->SetValue(
          property.GetNormalized());
    };
    return;
  }

  // If there is a text output, let the property convert the pan value to text.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // If there is a color output, let the property convert the pan value to a
  // color.
  if (auto* color_output = control->GetColorOutput(); color_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetColorOutput()->SetColor(property.GetColor());
    };
    return;
  }
}

namespace {

int MapVolumeToTwoStates(double volume) {
  // Unless it is totally silent, consider the volume to be on.
  return volume > 0.0 ? 1 : 0;
}

int MapVolumeToThreeStates(double volume) {
  if (volume <= 0.0) {
    return 0;  // Silent
  }
  if (volume > 1.0) {
    return 2;  // Greater than unity gain
  }
  return 1;  // Less than or equal to unity gain
}

int MapVolumeToRange(double volume, int max) {
  if (volume <= 0.0) {
    return 0;
  }
  if (volume >= kMaxVolume) {
    return max;
  }
  double value = volume / kMaxVolume;
  return static_cast<int>(std::ceil(value * (max - 1)));
}

}  // namespace

void ViewMapping::InitWriteVolumeSyncFunction() {
  Control* control = control_->GetControl();
  // If there is a dvalue output, we map the volume value from [0,kMaxVolume] to
  // discrete steps based on the max value.
  if (auto* value_output = control->GetDValueOutput();
      value_output != nullptr) {
    int max_value = value_output->GetMaxValue();
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapVolumeToTwoStates(property.GetVolume()));
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapVolumeToThreeStates(property.GetVolume()));
      };
    } else {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        auto* dvalue_output = control.GetControl()->GetDValueOutput();
        dvalue_output->SetValue(MapVolumeToRange(property.GetVolume(),
                                                 dvalue_output->GetMaxValue()));
      };
    }
    return;
  }

  // If there is a cvalue output, we map the volume value from [0,kMaxVolume] to
  // [0,1].
  if (auto* cvalue_output = control->GetCValueOutput();
      cvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      double volume = property.GetVolume();
      double normalized_volume = std::clamp(volume / kMaxVolume, 0.0, 1.0);
      control.GetControl()->GetCValueOutput()->SetValue(normalized_volume);
    };
    return;
  }

  // If there is a text output, let the property convert the volume value to
  // text.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // If there is a color output, let the property convert the volume value to a
  // color.
  if (auto* color_output = control->GetColorOutput(); color_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetColorOutput()->SetColor(property.GetColor());
    };
    return;
  }
}

namespace {

int MapNormalizedToTwoStates(double value) { return value > 0.5 ? 1 : 0; }

int MapNormalizedToThreeStates(double value) {
  if (value <= 0.0) {
    return 0;
  }
  if (value >= 1.0) {
    return 2;
  }
  return 1;
}

int MapNormalizedToRange(double value, int max) {
  if (value <= 0.0) {
    return 0;
  }
  if (value >= 1.0) {
    return max;
  }
  return static_cast<int>(std::ceil(value * (max - 1)));
}

}  // namespace

void ViewMapping::InitWriteNormalizedSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a dvalue output, we map the normalized value from [0,1] to
  // to discrete steps depending on the max value.
  if (auto* value_output = control->GetDValueOutput();
      value_output != nullptr) {
    int max_value = value_output->GetMaxValue();
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapNormalizedToTwoStates(property.GetNormalized()));
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapNormalizedToThreeStates(property.GetNormalized()));
      };
    } else {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        auto* dvalue_output = control.GetControl()->GetDValueOutput();
        dvalue_output->SetValue(MapNormalizedToRange(
            property.GetNormalized(), dvalue_output->GetMaxValue()));
      };
    }
    return;
  }

  // If there is a cvalue output, we just set the value and let the control
  // handle the conversion.
  if (auto* cvalue_output = control->GetCValueOutput();
      cvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetCValueOutput()->SetValue(
          property.GetNormalized());
    };
    return;
  }

  // If there is a text output, let the property convert the normalized value to
  // text.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // If there is a color output, let the property convert the normalized value
  // to a color.
  if (auto* color_output = control->GetColorOutput(); color_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetColorOutput()->SetColor(property.GetColor());
    };
    return;
  }
}

void ViewMapping::InitWriteTextSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a text output, use it.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // The other output types don't make much sense for text properties, so we
  // don't support them.
}

void ViewMapping::InitWriteColorSyncFunction() {
  Control* control = control_->GetControl();

  // If there is a color output, use it.
  if (auto* color_output = control->GetColorOutput(); color_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetColorOutput()->SetColor(property.GetColor());
    };
    return;
  }

  // If there is a text output, let the property convert the color value to
  // text.
  if (auto* text_output = control->GetTextOutput(); text_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetTextOutput()->SetText(property.GetText());
    };
    return;
  }

  // If there is a cvalue output, let the property convert the color value to a
  // normalized value.
  if (auto* cvalue_output = control->GetCValueOutput();
      cvalue_output != nullptr) {
    write_control_ = [](ViewProperty& property, ViewControl& control) {
      control.GetControl()->GetCValueOutput()->SetValue(
          property.GetNormalized());
    };
    return;
  }

  // If there is a dvalue output, let the property convert the color value to a
  // normalized value, and make it discrete in the same way as the normalized
  // property.
  if (auto* dvalue_output = control->GetDValueOutput();
      dvalue_output != nullptr) {
    int max_value = dvalue_output->GetMaxValue();
    if (max_value == 1) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapNormalizedToTwoStates(property.GetNormalized()));
      };
    } else if (max_value == 2) {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        control.GetControl()->GetDValueOutput()->SetValue(
            MapNormalizedToThreeStates(property.GetNormalized()));
      };
    } else {
      write_control_ = [](ViewProperty& property, ViewControl& control) {
        auto* dvalue_output = control.GetControl()->GetDValueOutput();
        dvalue_output->SetValue(MapNormalizedToRange(
            property.GetNormalized(), dvalue_output->GetMaxValue()));
      };
    }
    return;
  }
}

void ViewMapping::Activate() {
  if (active_) {
    return;
  }
  active_ = true;
  property_changed_ = type_.IsSet(kWriteControl);
  property_->AddMapping(this);
  control_->AddMapping(this);
}

void ViewMapping::Deactivate() {
  if (!active_) {
    return;
  }
  active_ = false;
  property_->RemoveMapping(this);
  control_->RemoveMapping(this);
}

void ViewMapping::Sync() {
  if (!active_) {
    return;
  }
  if (control_changed_) {
    ReadControl();
  }
  if (property_changed_) {
    WriteControl();
  }
}

void ViewMapping::ReadControl() {
  control_changed_ = false;
  if (type_.IsSet(kReadControl)) {
    read_control_(*property_, *control_);
  }
}

void ViewMapping::WriteControl() {
  property_changed_ = false;
  if (!type_.IsSet(kWriteControl)) {
    write_control_(*property_, *control_);
  }
}

}  // namespace jpr
