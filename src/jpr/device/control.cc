// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control.h"

#include "absl/log/log.h"
#include "gb/profile/profile_point.h"

namespace jpr {

namespace {

// Wait time in seconds to delay output updates after input events have stopped
// for controls with kDependent or kMotorized bindings.
constexpr double kDependentBindingDelaySecs = 0.125;
constexpr double kMotorizedBindingDelaySecs = 0.375;

double GetBindingDelaySecs(Control::Binding binding) {
  switch (binding) {
    case Control::Binding::kIndependent:
      return 0.0;
    case Control::Binding::kDependent:
      return kDependentBindingDelaySecs;
    case Control::Binding::kMotorized:
      return kMotorizedBindingDelaySecs;
  }
  // Should never reach here, but return zero to avoid compiler warning.
  return 0.0;
}

constexpr ControlInput::Type kInputTypes[] = {ControlInput::Type::kValue,
                                              ControlInput::Type::kDelta,
                                              ControlInput::Type::kPress};

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
      binding_delay_secs_(GetBindingDelaySecs(options.binding)) {
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
  if (touch_flags_.contains(id)) {
    return touch_count_;
  }
  auto it = registrations_.find(id);
  if (it == registrations_.end() ||
      it->second.config.input_type != ControlInput::Type::kPress) {
    return 0;
  }
  return it->second.press_count;
}

bool Control::IsPressed(InputId id) const {
  if (touch_flags_.contains(id)) {
    return touched_;
  }
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

int Control::GetTextWidth() const {
  if (text_output_ == nullptr) {
    return 0;
  }
  return text_output_->GetWidth();
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

void Control::SetTimelineText(TimelinePosition position, int mode) {
  if (text_output_ == nullptr) {
    return;
  }
  if (binding_ != Binding::kIndependent) {
    bool had_pending = pending_output_.has_value();
    pending_output_ = PendingOutput{.value = position, .mode = mode};
    if (!had_pending) {
      UpdateRunHandle();
    }
  } else {
    text_output_->SetTimelineText(position, mode);
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

ControlOutputHandle Control::RegisterOutputWriter() {
  ++output_writer_count_;
  return ControlOutputHandle(this);
}

void Control::UnregisterOutputWriter() {
  if (--output_writer_count_ == 0) {
    // Any pending output came from a writer that has been removed. If a new
    // writer is registered before the next run, it will write its own value.
    pending_output_.reset();
    clear_check_pending_ = true;
    UpdateRunHandle();
  }
}

void Control::ClearOutputs() {
  if (cvalue_output_ != nullptr) {
    SetCValue(0.0);
  }
  if (dvalue_output_ != nullptr) {
    SetDValue(dvalue_output_->GetClearedValue(),
              dvalue_output_->GetClearedMode());
  }
  if (text_output_ != nullptr) {
    SetText("");
  }
  if (color_output_ != nullptr) {
    SetColor({0, 0, 0});
  }
}

ControlInputHandle Control::RegisterInput(const InputConfig& config,
                                          bool* flag) {
  if (config.touch) {
    return RegisterTouch(flag);
  }
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
  UpdateInputListeners();
  UpdateRunHandle();
  return ControlInputHandle(this, id);
}

void Control::UnregisterInput(InputId id) {
  if (touch_flags_.erase(id) > 0) {
    UpdateInputListeners();
    UpdateRunHandle();
    return;
  }

  auto it = registrations_.find(id);
  if (it == registrations_.end()) {
    return;
  }
  ControlInput::Type input_type = it->second.config.input_type;
  registrations_.erase(it);

  RecomputeModifierMasks(input_type);
  if (input_type == ControlInput::Type::kPress) {
    RebuildPressGroups();
  }
  UpdateInputListeners();
  UpdateRunHandle();
}

ControlInputHandle Control::RegisterTouch(bool* flag) {
  // Touch is only followed while there is a touch registration, so the first
  // one starts it from the press input's state.
  if (touch_flags_.empty()) {
    touched_ = HasPressRelease() && press_input_->IsPressed();
    touch_count_ = 0;
    touch_hold_pending_ = false;
  }
  InputId id = next_input_id_++;
  touch_flags_[id] = flag;
  if (touched_) {
    *flag = true;
  }
  UpdateInputListeners();
  UpdateRunHandle();
  return ControlInputHandle(this, id);
}

bool Control::IsInputRead(ControlInput::Type input_type) const {
  // A control with press and release follows touch on its press input alone.
  if (!touch_flags_.empty() &&
      (!HasPressRelease() || input_type == ControlInput::Type::kPress)) {
    return true;
  }
  for (const auto& [id, reg] : registrations_) {
    if (reg.config.input_type == input_type) {
      return true;
    }
  }
  return false;
}

void Control::UpdateInputListeners() {
  for (ControlInput::Type input_type : kInputTypes) {
    if (!input_types_.IsSet(input_type)) {
      continue;
    }
    const bool read = IsInputRead(input_type);
    switch (input_type) {
      case ControlInput::Type::kValue:
        if (read != value_input_->HasListener()) {
          value_input_->SetListener(
              read ? ControlValueInput::Listener(
                         [this](ControlValueInput*) { OnValueInputChanged(); })
                   : nullptr);
        }
        break;
      case ControlInput::Type::kDelta:
        if (read != delta_input_->HasListener()) {
          delta_input_->SetListener(
              read ? ControlDeltaInput::Listener(
                         [this](ControlDeltaInput*) { OnDeltaInputChanged(); })
                   : nullptr);
        }
        break;
      case ControlInput::Type::kPress:
        if (read != press_input_->HasListener()) {
          press_input_->SetListener(
              read ? ControlPressInput::Listener(
                         [this](ControlPressInput*) { OnPressInputChanged(); })
                   : nullptr);
        }
        break;
    }
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
      case InputConfig::PressBehavior::kTap:
        group->tap_ids.push_back(id);
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
  gb::ProfileCount<"control inputs">(1);
  OnInput();
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
  gb::ProfileCount<"control inputs">(1);
  OnInput();
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
  gb::ProfileCount<"control inputs">(1);
  OnInput();

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

      if (!group.tap_ids.empty()) {
        group.tap_press_time_secs = last_run_time_secs_;
      }
      if (group.long_press_ids.empty() && group.double_press_ids.empty()) {
        DeliverPress(group.normal_ids, /*hold=*/true);
      } else if (group.state == PressGroup::State::kIdle) {
        group.state = (!group.long_press_ids.empty()
                           ? PressGroup::State::kPendingLong
                           : PressGroup::State::kPendingRelease);
        group.state_start_time_secs = last_run_time_secs_;
        group.pending_press_count = 1;
      } else if (group.state == PressGroup::State::kPendingDouble) {
        DeliverPress(group.double_press_ids);
        group.state = PressGroup::State::kIdle;
      }
    }
  } else {
    // On release, don't check modifiers. Release whichever group latched the
    // press, identified by is_pressed state or pending state machine state.
    for (auto& group : press_groups_) {
      if (group.tap_press_time_secs.has_value()) {
        if (last_run_time_secs_ - *group.tap_press_time_secs <
            kLongPressDurationSecs) {
          DeliverPress(group.tap_ids);
        }
        group.tap_press_time_secs.reset();
      }
      if (group.long_press_ids.empty() && group.double_press_ids.empty()) {
        ReleasePressed(group.normal_ids);
      } else if (group.state == PressGroup::State::kPendingLong ||
                 group.state == PressGroup::State::kPendingRelease) {
        if (!group.double_press_ids.empty() &&
            last_run_time_secs_ - group.state_start_time_secs <
                kDoublePressWindowSecs) {
          group.state = PressGroup::State::kPendingDouble;
          group.state_start_time_secs = last_run_time_secs_;
        } else {
          DeliverPress(group.normal_ids);
          group.state = PressGroup::State::kIdle;
        }
      } else {
        // A delivered long press leaves the group idle, but stays pressed until
        // release.
        ReleasePressed(group.long_press_ids);
      }
    }
  }
}

void Control::OnPressInputChangedWithoutRelease() {
  // Without release support, IsPressed() always returns false. We detect
  // presses via GetPressCount() instead. Long press is not possible without
  // release, so long_press_ids are ignored, and every press is a tap. Each
  // listener callback represents exactly one press event.
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

    DeliverPress(group.tap_ids);

    // If the group has no double press (long press is impossible without
    // release), deliver immediately.
    if (group.double_press_ids.empty()) {
      DeliverPress(group.normal_ids);
      continue;
    }

    // Deferred handling for double press detection.
    switch (group.state) {
      case PressGroup::State::kIdle:
        // First press: start the double press window.
        group.state = PressGroup::State::kPendingDouble;
        group.state_start_time_secs = last_run_time_secs_;
        group.pending_press_count = 1;
        break;

      case PressGroup::State::kPendingLong:
      case PressGroup::State::kPendingRelease:
        // Should not happen without release, but treat as first press.
        group.state = PressGroup::State::kPendingDouble;
        group.state_start_time_secs = last_run_time_secs_;
        group.pending_press_count = 1;
        break;

      case PressGroup::State::kPendingDouble:
        // Second press within the double press window.
        DeliverPress(group.double_press_ids);
        group.state = PressGroup::State::kIdle;
        break;
    }
  }
}

void Control::DeliverPress(const std::vector<InputId>& ids, bool hold) {
  for (InputId id : ids) {
    auto it = registrations_.find(id);
    if (it != registrations_.end()) {
      it->second.press_count++;
      if (hold) {
        it->second.is_pressed = true;
      }
      *it->second.flag = true;
    }
  }
}

void Control::ReleasePressed(const std::vector<InputId>& ids) {
  for (InputId id : ids) {
    auto it = registrations_.find(id);
    if (it != registrations_.end() && it->second.is_pressed) {
      it->second.is_pressed = false;
      *it->second.flag = true;
    }
  }
}

void Control::OnInput() {
  last_input_time_secs_ = last_run_time_secs_;
  if (touch_flags_.empty()) {
    return;
  }
  if (HasPressRelease()) {
    SetTouched(press_input_->IsPressed());
  } else {
    SetTouched(true);
    touch_hold_pending_ = true;
  }
}

void Control::UpdateTouchHold() {
  if (!touched_ || HasPressRelease()) {
    return;
  }

  // The hold starts on the run after the input, as the input arrived between
  // runs, and last_run_time_secs_ may be from before the control last ran.
  if (touch_hold_pending_) {
    touch_hold_pending_ = false;
    touch_hold_start_secs_ = last_run_time_secs_;
  } else if (last_run_time_secs_ - touch_hold_start_secs_ >= kTouchHoldSecs) {
    SetTouched(false);
  }
}

void Control::SetTouched(bool touched) {
  if (touched_ == touched) {
    return;
  }
  touched_ = touched;
  if (touched) {
    ++touch_count_;
  }
  for (const auto& [id, flag] : touch_flags_) {
    *flag = true;
  }
}

void Control::UpdateRunHandle() {
  bool need_run =
      HasRegistrations() || pending_output_.has_value() || clear_check_pending_;
  if (need_run && !run_handle_.IsRegistered()) {
    run_handle_ =
        run_registry_.AddRunnable([this](const RunTime& time) { OnRun(time); });
  } else if (!need_run && run_handle_.IsRegistered()) {
    run_handle_ = {};
  }
}

void Control::OnRun(const RunTime& time) {
  last_run_time_secs_ = time.precise_secs;
  ResetVirtualInputs();
  UpdatePressTimers(time.precise_secs);
  UpdateTouchHold();
  if (clear_check_pending_) {
    clear_check_pending_ = false;
    if (output_writer_count_ == 0) {
      ClearOutputs();
    }
  }
  SendPendingOutput();
  UpdateRunHandle();
}

void Control::UpdatePressTimers(double current_time_secs) {
  for (auto& group : press_groups_) {
    switch (group.state) {
      case PressGroup::State::kIdle:
        break;

      case PressGroup::State::kPendingLong:
        if (current_time_secs - group.state_start_time_secs >=
            kLongPressDurationSecs) {
          DeliverPress(group.long_press_ids, /*hold=*/true);
          group.state = PressGroup::State::kIdle;
        }
        break;

      case PressGroup::State::kPendingRelease:
        if (current_time_secs - group.state_start_time_secs >=
            kDoublePressWindowSecs) {
          // Held too long for a double press. Deliver as a normal press.
          DeliverPress(group.normal_ids);
          group.state = PressGroup::State::kIdle;
        }
        break;

      case PressGroup::State::kPendingDouble:
        if (current_time_secs - group.state_start_time_secs >=
            kDoublePressWindowSecs) {
          // Double press window expired without a second press.
          // Deliver as a normal press.
          DeliverPress(group.normal_ids);
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
  touch_count_ = 0;
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
  } else if (last_input_time_secs_.has_value() &&
             last_run_time_secs_ - last_input_time_secs_.value() <
                 binding_delay_secs_) {
    return;
  }

  // Send the pending output and clear it.
  gb::ProfileCount<"control outputs">(1);
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
        } else if constexpr (std::is_same_v<T, TimelinePosition>) {
          text_output_->SetTimelineText(value, mode);
        } else {
          static_assert(false, "non-exhaustive visitor!");
        }
      },
      pending.value);
  pending_output_.reset();
}

}  // namespace jpr
