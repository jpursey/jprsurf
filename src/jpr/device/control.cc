// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control.h"

namespace jpr {

namespace {

// Wait time in seconds to delay output updates after input events have stopped
// for controls with kDependent or kMotorized output modes.
constexpr double kDependentOutputDelay = 0.125;
constexpr double kMotorizedOutputDelay = 0.375;

double GetOutputDelay(Control::OutputMode output_mode) {
  switch (output_mode) {
    case Control::OutputMode::kIndependent:
      return 0.0;
    case Control::OutputMode::kDependent:
      return kDependentOutputDelay;
    case Control::OutputMode::kMotorized:
      return kMotorizedOutputDelay;
  }
  // Should never reach here, but return zero to avoid compiler warning.
  return 0.0;
}

}  // namespace

Control::Control(RunRegistry& run_registry, Options options)
    : run_registry_(run_registry),
      name_(options.name),
      value_input_(std::move(options.value_input)),
      delta_input_(std::move(options.delta_input)),
      press_input_(std::move(options.press_input)),
      cvalue_output_(std::move(options.cvalue_output)),
      dvalue_output_(std::move(options.dvalue_output)),
      text_output_(std::move(options.text_output)),
      color_output_(std::move(options.color_output)),
      output_mode_(options.output_mode),
      output_delay_(GetOutputDelay(options.output_mode)) {
  if (value_input_ != nullptr) {
    input_types_.Set(ControlInput::Type::kValue);
  }
  if (delta_input_ != nullptr) {
    input_types_.Set(ControlInput::Type::kDelta);
  }
  if (press_input_ != nullptr) {
    input_types_.Set(ControlInput::Type::kPress);
  }

  if (cvalue_output_ != nullptr) {
    output_types_.Set(ControlOutput::Type::kCValue);
  }
  if (dvalue_output_ != nullptr) {
    output_types_.Set(ControlOutput::Type::kDValue);
  }
  if (text_output_ != nullptr) {
    output_types_.Set(ControlOutput::Type::kText);
  }
  if (color_output_ != nullptr) {
    output_types_.Set(ControlOutput::Type::kColor);
  }
}

Control::~Control() = default;

double Control::GetValue() const {
  if (value_input_ == nullptr) {
    return 0.0;
  }
  return value_input_->GetValue();
}

double Control::GetDelta() const {
  if (delta_input_ == nullptr) {
    return 0.0;
  }
  return delta_input_->PeekDelta();
}

int Control::GetPressCount() const {
  if (press_input_ == nullptr) {
    return 0;
  }
  return press_input_->GetPressCount();
}

bool Control::IsPressed() const {
  if (press_input_ == nullptr) {
    return false;
  }
  return press_input_->IsPressed();
}

int Control::GetDValueMaxValue() const {
  if (dvalue_output_ == nullptr) {
    return 0;
  }
  return dvalue_output_->GetMaxValue();
}

void Control::SetCValue(double value) {
  if (cvalue_output_ == nullptr) {
    return;
  }
  if (output_mode_ != OutputMode::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = value;
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    cvalue_output_->SetValue(value);
  }
}

void Control::SetDValue(int value) {
  if (dvalue_output_ == nullptr) {
    return;
  }
  if (output_mode_ != OutputMode::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = value;
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    dvalue_output_->SetValue(value);
  }
}

void Control::SetText(std::string_view text) {
  if (text_output_ == nullptr) {
    return;
  }
  if (output_mode_ != OutputMode::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = std::string(text);
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    text_output_->SetText(text);
  }
}

void Control::SetColor(Color color) {
  if (color_output_ == nullptr) {
    return;
  }
  if (output_mode_ != OutputMode::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = color;
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    color_output_->SetColor(color);
  }
}

void Control::RegisterInputFlag(ControlInput::Type input_type, bool* flag) {
  absl::flat_hash_set<bool*>* flags = nullptr;
  switch (input_type) {
    case ControlInput::Type::kValue:
      flags = &value_input_flags_;
      break;
    case ControlInput::Type::kDelta:
      flags = &delta_input_flags_;
      break;
    case ControlInput::Type::kPress:
      flags = &press_input_flags_;
      break;
  }
  bool was_empty = flags->empty();
  flags->insert(flag);
  if (was_empty) {
    SetInputListener(input_type);
    if (input_type != ControlInput::Type::kValue) {
      UpdateRunHandle();
    }
  }
}

void Control::UnregisterInputFlag(ControlInput::Type input_type, bool* flag) {
  absl::flat_hash_set<bool*>* flags = nullptr;
  switch (input_type) {
    case ControlInput::Type::kValue:
      flags = &value_input_flags_;
      break;
    case ControlInput::Type::kDelta:
      flags = &delta_input_flags_;
      break;
    case ControlInput::Type::kPress:
      flags = &press_input_flags_;
      break;
  }
  flags->erase(flag);
  if (flags->empty()) {
    ClearInputListener(input_type);
    if (input_type != ControlInput::Type::kValue) {
      UpdateRunHandle();
    }
  }
}

void Control::SetInputListener(ControlInput::Type input_type) {
  switch (input_type) {
    case ControlInput::Type::kValue:
      if (value_input_ != nullptr) {
        value_input_->SetListener([this](ControlValueInput*) {
          NotifyInputFlags(ControlInput::Type::kValue);
        });
      }
      break;
    case ControlInput::Type::kDelta:
      if (delta_input_ != nullptr) {
        delta_input_->SetListener([this](ControlDeltaInput*) {
          NotifyInputFlags(ControlInput::Type::kDelta);
        });
      }
      break;
    case ControlInput::Type::kPress:
      if (press_input_ != nullptr) {
        press_input_->SetListener([this](ControlPressInput*) {
          NotifyInputFlags(ControlInput::Type::kPress);
        });
      }
      break;
  }
}

void Control::ClearInputListener(ControlInput::Type input_type) {
  switch (input_type) {
    case ControlInput::Type::kValue:
      if (value_input_ != nullptr) {
        value_input_->SetListener(nullptr);
      }
      break;
    case ControlInput::Type::kDelta:
      if (delta_input_ != nullptr) {
        delta_input_->SetListener(nullptr);
      }
      break;
    case ControlInput::Type::kPress:
      if (press_input_ != nullptr) {
        press_input_->SetListener(nullptr);
      }
      break;
  }
}

void Control::NotifyInputFlags(ControlInput::Type input_type) {
  const absl::flat_hash_set<bool*>* flags = nullptr;
  switch (input_type) {
    case ControlInput::Type::kValue:
      flags = &value_input_flags_;
      break;
    case ControlInput::Type::kDelta:
      flags = &delta_input_flags_;
      break;
    case ControlInput::Type::kPress:
      flags = &press_input_flags_;
      break;
  }
  for (bool* flag : *flags) {
    *flag = true;
  }
}

void Control::UpdateRunHandle() {
  bool need_run = !delta_input_flags_.empty() || !press_input_flags_.empty() ||
                  pending_output_.has_value();
  if (need_run && !run_handle_.IsRegistered()) {
    run_handle_ =
        run_registry_.AddRunnable([this](const RunTime& time) { OnRun(time); });
  } else if (!need_run && run_handle_.IsRegistered()) {
    run_handle_ = {};
  }
}

void Control::OnRun(const RunTime& time) {
  last_run_time_ = time.precise;
  ResetInputs();
  SendPendingOutput();
  UpdateRunHandle();
}

void Control::ResetInputs() {
  if (delta_input_ != nullptr) {
    delta_input_->ResetDelta();
  }
  if (press_input_ != nullptr) {
    press_input_->ResetCounts();
  }
}

void Control::SendPendingOutput() {
  if (!pending_output_.has_value()) {
    return;
  }

  // If there is a press input then its press state determines whether the
  // output can be updated, otherwise we must wait until the delay time has
  // passed since the last input event.
  if (press_input_ != nullptr) {
    if (press_input_->IsPressed()) {
      return;
    }
  } else if (last_input_time_.has_value() &&
             last_run_time_ - last_input_time_.value() < output_delay_) {
    return;
  }

  // Send the pending output and clear it.
  std::visit(
      [this](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, double>) {
          cvalue_output_->SetValue(value);
        } else if constexpr (std::is_same_v<T, int>) {
          dvalue_output_->SetValue(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
          text_output_->SetText(value);
        } else if constexpr (std::is_same_v<T, Color>) {
          color_output_->SetColor(value);
        } else {
          static_assert(false, "non-exhaustive visitor!");
        }
      },
      pending_output_.value());
  pending_output_.reset();
}

}  // namespace jpr
