// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control.h"

#include "absl/log/log.h"

namespace jpr {

namespace {

// Wait time in seconds to delay output updates after input events have stopped
// for controls with kDependent or kMotorized bindings.
constexpr double kDependentBindingDelay = 0.125;
constexpr double kMotorizedBindingDelay = 0.375;

// Duration in seconds that a button must be held to trigger a long press.
constexpr double kLongPressDuration = 0.35;

// Time window in seconds after a short press release to wait for a second
// press to trigger a double press.
constexpr double kDoublePressWindow = 0.15;

double GetBindingDelay(Control::Binding binding) {
  switch (binding) {
    case Control::Binding::kIndependent:
      return 0.0;
    case Control::Binding::kDependent:
      return kDependentBindingDelay;
    case Control::Binding::kMotorized:
      return kMotorizedBindingDelay;
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
      binding_(options.binding),
      binding_delay_(GetBindingDelay(options.binding)) {
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

double Control::GetValue(InputId id) const {
  auto it = registrations_.find(id);
  if (it == registrations_.end() ||
      it->second.config.input_type != ControlInput::Type::kValue) {
    return 0.0;
  }
  return it->second.value;
}

double Control::GetDelta(InputId id) const {
  auto it = registrations_.find(id);
  if (it == registrations_.end() ||
      it->second.config.input_type != ControlInput::Type::kDelta) {
    return 0.0;
  }
  return it->second.delta;
}

int Control::GetPressCount(InputId id) const {
  auto it = registrations_.find(id);
  if (it == registrations_.end() ||
      it->second.config.input_type != ControlInput::Type::kPress) {
    return 0;
  }
  return it->second.press_count;
}

bool Control::IsPressed(InputId id) const {
  auto it = registrations_.find(id);
  if (it == registrations_.end() ||
      it->second.config.input_type != ControlInput::Type::kPress) {
    return false;
  }
  return it->second.is_pressed;
}

int Control::GetModeCount() const {
  int max_count = 1;
  if (cvalue_output_ != nullptr) {
    max_count = std::max(max_count, cvalue_output_->GetModeCount());
  }
  if (dvalue_output_ != nullptr) {
    max_count = std::max(max_count, dvalue_output_->GetModeCount());
  }
  if (text_output_ != nullptr) {
    max_count = std::max(max_count, text_output_->GetModeCount());
  }
  if (color_output_ != nullptr) {
    max_count = std::max(max_count, color_output_->GetModeCount());
  }
  return max_count;
}

int Control::GetDValueMaxValue(int mode) const {
  if (dvalue_output_ == nullptr) {
    return 0;
  }
  return dvalue_output_->GetMaxValue(mode);
}

void Control::SetCValue(double value, int mode) {
  if (cvalue_output_ == nullptr) {
    return;
  }
  if (binding_ != Binding::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = PendingOutput{.value = value, .mode = mode};
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    cvalue_output_->SetValue(value, mode);
  }
}

void Control::SetDValue(int value, int mode) {
  if (dvalue_output_ == nullptr) {
    return;
  }
  if (binding_ != Binding::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = PendingOutput{.value = value, .mode = mode};
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    dvalue_output_->SetValue(value, mode);
  }
}

void Control::SetText(std::string_view text, int mode) {
  if (text_output_ == nullptr) {
    return;
  }
  if (binding_ != Binding::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = PendingOutput{.value = std::string(text), .mode = mode};
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    text_output_->SetText(text, mode);
  }
}

void Control::SetColor(Color color, int mode) {
  if (color_output_ == nullptr) {
    return;
  }
  if (binding_ != Binding::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = PendingOutput{.value = color, .mode = mode};
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    color_output_->SetColor(color, mode);
  }
}

ControlInputHandle Control::RegisterInput(const InputConfig& config,
                                          bool* flag) {
  InputId id = next_input_id_++;
  auto& reg = registrations_[id];
  reg.config = config;
  reg.flag = flag;

  // Initialize value input to current physical value.
  if (config.input_type == ControlInput::Type::kValue &&
      value_input_ != nullptr) {
    reg.value = value_input_->GetValue();
  }

  RecomputeModifierMasks(config.input_type);
  if (config.input_type == ControlInput::Type::kPress) {
    RebuildPressGroups();
  }

  // Ensure we have a listener for this input type.
  switch (config.input_type) {
    case ControlInput::Type::kValue:
      if (value_input_ != nullptr && !value_input_->HasListener()) {
        SetInputListener(ControlInput::Type::kValue);
      }
      break;
    case ControlInput::Type::kDelta:
      if (delta_input_ != nullptr && !delta_input_->HasListener()) {
        SetInputListener(ControlInput::Type::kDelta);
      }
      break;
    case ControlInput::Type::kPress:
      if (press_input_ != nullptr && !press_input_->HasListener()) {
        SetInputListener(ControlInput::Type::kPress);
      }
      break;
  }

  UpdateRunHandle();
  return ControlInputHandle(this, id);
}

void Control::UnregisterInput(InputId id) {
  auto it = registrations_.find(id);
  if (it == registrations_.end()) {
    return;
  }
  ControlInput::Type input_type = it->second.config.input_type;
  registrations_.erase(it);

  RecomputeModifierMasks(input_type);

  // Check if there are still registrations for this input type.
  bool has_type = false;
  for (const auto& [reg_id, reg] : registrations_) {
    if (reg.config.input_type == input_type) {
      has_type = true;
      break;
    }
  }

  if (!has_type) {
    ClearInputListener(input_type);
  }

  if (input_type == ControlInput::Type::kPress) {
    RebuildPressGroups();
  }

  UpdateRunHandle();
}

void Control::SetInputListener(ControlInput::Type input_type) {
  switch (input_type) {
    case ControlInput::Type::kValue:
      if (value_input_ != nullptr) {
        value_input_->SetListener(
            [this](ControlValueInput*) { OnValueInputChanged(); });
      }
      break;
    case ControlInput::Type::kDelta:
      if (delta_input_ != nullptr) {
        delta_input_->SetListener(
            [this](ControlDeltaInput*) { OnDeltaInputChanged(); });
      }
      break;
    case ControlInput::Type::kPress:
      if (press_input_ != nullptr) {
        press_input_->SetListener(
            [this](ControlPressInput*) { OnPressInputChanged(); });
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

void Control::RecomputeModifierMasks(ControlInput::Type input_type) {
  // Collect all required_modifiers for registrations of this input type.
  Modifiers all_modifiers = 0;
  for (const auto& [id, reg] : registrations_) {
    if (reg.config.input_type == input_type) {
      all_modifiers |= reg.config.required_modifiers;
    }
  }

  // For each registration, on_mask is its own required_modifiers, and off_mask
  // is all other registrations' modifiers that are not in its own.
  for (auto& [id, reg] : registrations_) {
    if (reg.config.input_type == input_type) {
      reg.on_mask = reg.config.required_modifiers;
      reg.off_mask = all_modifiers & ~reg.config.required_modifiers;
    }
  }
}

void Control::RebuildPressGroups() {
  press_groups_.clear();

  for (const auto& [id, reg] : registrations_) {
    if (reg.config.input_type != ControlInput::Type::kPress) {
      continue;
    }

    // Find or create the press group for this modifier combination.
    PressGroup* group = FindPressGroup(reg.on_mask, reg.off_mask);
    if (group == nullptr) {
      press_groups_.emplace_back();
      group = &press_groups_.back();
      group->on_mask = reg.on_mask;
      group->off_mask = reg.off_mask;
    }

    switch (reg.config.press_behavior) {
      case InputConfig::PressBehavior::kNormal:
        group->normal_ids.push_back(id);
        break;
      case InputConfig::PressBehavior::kLongPress:
        group->long_press_ids.push_back(id);
        break;
      case InputConfig::PressBehavior::kDoublePress:
        group->double_press_ids.push_back(id);
        break;
    }
  }
}

bool Control::CheckModifiers(const InputRegistration& reg) const {
  if (reg.on_mask != 0 && !AreModifiersOn(reg.on_mask)) {
    return false;
  }
  if (reg.off_mask != 0 && !AreModifiersOff(reg.off_mask)) {
    return false;
  }
  return true;
}

Control::PressGroup* Control::FindPressGroup(Modifiers on_mask,
                                             Modifiers off_mask) {
  for (auto& group : press_groups_) {
    if (group.on_mask == on_mask && group.off_mask == off_mask) {
      return &group;
    }
  }
  return nullptr;
}

void Control::OnValueInputChanged() {
  if (value_input_ == nullptr) {
    return;
  }
  last_input_time_ = last_run_time_;
  double physical_value = value_input_->GetValue();

  for (auto& [id, reg] : registrations_) {
    if (reg.config.input_type != ControlInput::Type::kValue) {
      continue;
    }
    if (!CheckModifiers(reg)) {
      continue;
    }
    reg.value = physical_value;
    *reg.flag = true;
  }
}

void Control::OnDeltaInputChanged() {
  if (delta_input_ == nullptr) {
    return;
  }
  last_input_time_ = last_run_time_;
  double physical_delta = delta_input_->ReadDelta();

  for (auto& [id, reg] : registrations_) {
    if (reg.config.input_type != ControlInput::Type::kDelta) {
      continue;
    }
    if (!CheckModifiers(reg)) {
      continue;
    }
    reg.delta += physical_delta;
    *reg.flag = true;
  }
}

void Control::OnPressInputChanged() {
  if (press_input_ == nullptr) {
    return;
  }
  last_input_time_ = last_run_time_;

  if (press_input_->HasRelease()) {
    OnPressInputChangedWithRelease();
  } else {
    OnPressInputChangedWithoutRelease();
  }
}

void Control::OnPressInputChangedWithRelease() {
  bool physical_pressed = press_input_->IsPressed();

  if (physical_pressed) {
    // On press, check modifiers to determine which group handles it.
    for (auto& group : press_groups_) {
      if (group.on_mask != 0 && !AreModifiersOn(group.on_mask)) {
        continue;
      }
      if (group.off_mask != 0 && !AreModifiersOff(group.off_mask)) {
        continue;
      }

      if (group.long_press_ids.empty() && group.double_press_ids.empty()) {
        DeliverNormalPress(group);
        for (InputId id : group.normal_ids) {
          auto it = registrations_.find(id);
          if (it != registrations_.end()) {
            it->second.is_pressed = true;
            *it->second.flag = true;
          }
        }
      } else if (group.state == PressGroup::State::kIdle) {
        group.state = (!group.long_press_ids.empty()
                           ? PressGroup::State::kPendingLong
                           : PressGroup::State::kPendingRelease);
        group.state_start_time = last_run_time_;
        group.pending_press_count = 1;
      } else if (group.state == PressGroup::State::kPendingDouble) {
        DeliverDoublePress(group);
        group.state = PressGroup::State::kIdle;
      }
    }
  } else {
    // On release, don't check modifiers. Release whichever group latched the
    // press, identified by is_pressed state or pending state machine state.
    for (auto& group : press_groups_) {
      if (group.long_press_ids.empty() && group.double_press_ids.empty()) {
        for (InputId id : group.normal_ids) {
          auto it = registrations_.find(id);
          if (it != registrations_.end()) {
            if (it->second.is_pressed) {
              it->second.is_pressed = false;
              *it->second.flag = true;
            }
          }
        }
      } else if (group.state == PressGroup::State::kPendingLong ||
                 group.state == PressGroup::State::kPendingRelease) {
        if (!group.double_press_ids.empty() &&
            last_run_time_ - group.state_start_time < kDoublePressWindow) {
          group.state = PressGroup::State::kPendingDouble;
          group.state_start_time = last_run_time_;
        } else {
          DeliverNormalPress(group);
          group.state = PressGroup::State::kIdle;
        }
      }
    }
  }
}

void Control::OnPressInputChangedWithoutRelease() {
  // Without release support, IsPressed() always returns false. We detect
  // presses via GetPressCount() instead. Long press is not possible without
  // release, so long_press_ids are ignored. Each listener callback represents
  // exactly one press event.
  int press_count = press_input_->GetPressCount();
  if (press_count == 0) {
    return;
  }

  for (auto& group : press_groups_) {
    if (group.on_mask != 0 && !AreModifiersOn(group.on_mask)) {
      continue;
    }
    if (group.off_mask != 0 && !AreModifiersOff(group.off_mask)) {
      continue;
    }

    // If the group has no double press (long press is impossible without
    // release), deliver immediately.
    if (group.double_press_ids.empty()) {
      DeliverNormalPress(group);
      continue;
    }

    // Deferred handling for double press detection.
    switch (group.state) {
      case PressGroup::State::kIdle:
        // First press: start the double press window.
        group.state = PressGroup::State::kPendingDouble;
        group.state_start_time = last_run_time_;
        group.pending_press_count = 1;
        break;

      case PressGroup::State::kPendingLong:
      case PressGroup::State::kPendingRelease:
        // Should not happen without release, but treat as first press.
        group.state = PressGroup::State::kPendingDouble;
        group.state_start_time = last_run_time_;
        group.pending_press_count = 1;
        break;

      case PressGroup::State::kPendingDouble:
        // Second press within the double press window.
        DeliverDoublePress(group);
        group.state = PressGroup::State::kIdle;
        break;
    }
  }
}

void Control::DeliverNormalPress(PressGroup& group) {
  for (InputId id : group.normal_ids) {
    auto it = registrations_.find(id);
    if (it != registrations_.end()) {
      it->second.press_count++;
      *it->second.flag = true;
    }
  }
}

void Control::DeliverLongPress(PressGroup& group) {
  for (InputId id : group.long_press_ids) {
    auto it = registrations_.find(id);
    if (it != registrations_.end()) {
      it->second.press_count++;
      *it->second.flag = true;
    }
  }
}

void Control::DeliverDoublePress(PressGroup& group) {
  for (InputId id : group.double_press_ids) {
    auto it = registrations_.find(id);
    if (it != registrations_.end()) {
      it->second.press_count++;
      *it->second.flag = true;
    }
  }
}

void Control::UpdateRunHandle() {
  bool need_run = HasRegistrations() || pending_output_.has_value();
  if (need_run && !run_handle_.IsRegistered()) {
    run_handle_ =
        run_registry_.AddRunnable([this](const RunTime& time) { OnRun(time); });
  } else if (!need_run && run_handle_.IsRegistered()) {
    run_handle_ = {};
  }
}

void Control::OnRun(const RunTime& time) {
  last_run_time_ = time.precise;
  ResetVirtualInputs();
  UpdatePressTimers(time.precise);
  SendPendingOutput();
  UpdateRunHandle();
}

void Control::UpdatePressTimers(double current_time) {
  for (auto& group : press_groups_) {
    switch (group.state) {
      case PressGroup::State::kIdle:
        break;

      case PressGroup::State::kPendingLong:
        if (current_time - group.state_start_time >= kLongPressDuration) {
          DeliverLongPress(group);
          group.state = PressGroup::State::kIdle;
        }
        break;

      case PressGroup::State::kPendingRelease:
        if (current_time - group.state_start_time >= kDoublePressWindow) {
          // Held too long for a double press. Deliver as a normal press.
          DeliverNormalPress(group);
          group.state = PressGroup::State::kIdle;
        }
        break;

      case PressGroup::State::kPendingDouble:
        if (current_time - group.state_start_time >= kDoublePressWindow) {
          // Double press window expired without a second press.
          // Deliver as a normal press.
          DeliverNormalPress(group);
          group.state = PressGroup::State::kIdle;
        }
        break;
    }
  }
}

void Control::ResetVirtualInputs() {
  if (delta_input_ != nullptr) {
    delta_input_->ResetDelta();
  }
  if (press_input_ != nullptr) {
    press_input_->ResetCounts();
  }
  for (auto& [id, reg] : registrations_) {
    if (reg.config.input_type == ControlInput::Type::kDelta) {
      reg.delta = 0.0;
    } else if (reg.config.input_type == ControlInput::Type::kPress) {
      reg.press_count = 0;
    }
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
             last_run_time_ - last_input_time_.value() < binding_delay_) {
    return;
  }

  // Send the pending output and clear it.
  const auto& pending = pending_output_.value();
  int mode = pending.mode;
  std::visit(
      [this, mode](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, double>) {
          cvalue_output_->SetValue(value, mode);
        } else if constexpr (std::is_same_v<T, int>) {
          dvalue_output_->SetValue(value, mode);
        } else if constexpr (std::is_same_v<T, std::string>) {
          text_output_->SetText(value, mode);
        } else if constexpr (std::is_same_v<T, Color>) {
          color_output_->SetColor(value, mode);
        } else {
          static_assert(false, "non-exhaustive visitor!");
        }
      },
      pending.value);
  pending_output_.reset();
}

}  // namespace jpr
