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
      // TODO
      return;
    case ViewProperty::Type::kToggle:
      InitReadToggleSyncFunction();
      break;
    case ViewProperty::Type::kPan:
      // TODO
      break;
    case ViewProperty::Type::kVolume:
      // TODO
      break;
    case ViewProperty::Type::kNormalized:
      // TODO
      break;
    case ViewProperty::Type::kText:
      // TODO
      break;
    case ViewProperty::Type::kColor:
      // TODO
      break;
  }
}

void ViewMapping::InitReadToggleSyncFunction() {
  Control* control = control_->GetControl();

  if (auto* press_input = control->GetPressInput(); press_input != nullptr) {
    // If the press input supports release events, we can track the pressed
    // state directly.
    if (press_input->HasRelease()) {
      read_control_ = [](ViewProperty& property, ViewControl& control) {
        property.SetBool(control.GetControl()->GetPressInput()->IsPressed());
      };
      return;
    }
    // Toggle the value on every press, since there are no release events to
    // track the
    read_control_ = [](ViewProperty& property, ViewControl& control) {
      auto* press_input = control.GetControl()->GetPressInput();
      if (press_input->GetPressCount() > 0) {
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
      // TODO
      break;
    case ViewProperty::Type::kVolume:
      // TODO
      break;
    case ViewProperty::Type::kNormalized:
      // TODO
      break;
    case ViewProperty::Type::kText:
      // TODO
      break;
    case ViewProperty::Type::kColor:
      // TODO
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
