// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// ToggleValueProperty
//==============================================================================

// A toggle property that holds its own value, rather than reflecting REAPER
// state. This is for state owned by the code that creates it (for instance, the
// plugin), which changes it with SetBool(). Mappings that read a control into
// the property change it the same way.
class ToggleValueProperty final : public ViewProperty {
 public:
  explicit ToggleValueProperty(std::string_view name, bool value = false)
      : ViewProperty(name, Type::kToggle), value_(value) {}
  ~ToggleValueProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override { return value_; }
  void WriteBool(bool value) override {
    if (value == value_) {
      return;
    }
    value_ = value;
    NotifyChanged();
  }

 private:
  bool value_;
};

//==============================================================================
// CallbackActionProperty
//==============================================================================

// An action property that invokes a callback when triggered.
//
// The callback runs while the scene is synchronizing its views, so it must not
// enable, disable, or otherwise restructure views directly. Instead, it should
// record what needs to change, and apply it after the scene has run.
class CallbackActionProperty final : public ViewProperty {
 public:
  using Callback = absl::AnyInvocable<void()>;

  CallbackActionProperty(std::string_view name, Callback callback)
      : ViewProperty(name, Type::kAction), callback_(std::move(callback)) {}
  ~CallbackActionProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void TriggerAction() override { callback_(); }

 private:
  Callback callback_;
};

//==============================================================================
// CallbackToggleProperty
//==============================================================================

// A toggle property that invokes a callback with each value written to it. It
// holds no value, and always reads false.
//
// This is for mapping a control's press and release to code (for instance, with
// a press_release mapping). The callback is called for every write, even if the
// value is the same as the last one, so it should do nothing if the value is
// already in effect.
//
// Like CallbackActionProperty, the callback runs while the scene is
// synchronizing its views, so it must not enable, disable, or otherwise
// restructure views directly.
class CallbackToggleProperty final : public ViewProperty {
 public:
  using Callback = absl::AnyInvocable<void(bool)>;

  CallbackToggleProperty(std::string_view name, Callback callback)
      : ViewProperty(name, Type::kToggle), callback_(std::move(callback)) {}
  ~CallbackToggleProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void WriteBool(bool value) override { callback_(value); }

 private:
  Callback callback_;
};

}  // namespace jpr
