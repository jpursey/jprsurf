// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
// EnumeratedValueProperty
//==============================================================================

// An enumerated property that holds its own value, rather than reflecting
// REAPER state. Like ToggleValueProperty, this is for state owned by the code
// that creates it, which changes it with SetInt(). Mappings that read a control
// into the property change it the same way.
class EnumeratedValueProperty final : public ViewProperty {
 public:
  // The value names, in order, which GetText() returns. Its values are 0 to
  // the number of names minus one (with no names, it has one empty name), and
  // it starts as `value` (clamped to them).
  EnumeratedValueProperty(std::string_view name,
                          std::vector<std::string> value_names, int value = 0)
      : ViewProperty(name, Type::kEnumerated),
        value_names_(value_names.empty() ? std::vector<std::string>{""}
                                         : std::move(value_names)),
        value_(std::clamp(value, 0, GetMaxValue())) {}
  ~EnumeratedValueProperty() override = default;

  // Overrides from ViewProperty.
  int GetMaxValue() const override {
    return static_cast<int>(value_names_.size()) - 1;
  }
  std::string GetText() const override { return value_names_[value_]; }

  // Sets the value with the name. Text that isn't a name is read as a number,
  // as for any enumerated property.
  void SetText(std::string_view value) override {
    auto it = std::find(value_names_.begin(), value_names_.end(), value);
    if (it == value_names_.end()) {
      ViewProperty::SetText(value);
      return;
    }
    SetInt(static_cast<int>(it - value_names_.begin()));
  }

 protected:
  // Overrides from ViewProperty.
  int ReadInt() const override { return value_; }
  void WriteInt(int value) override {
    if (value == value_) {
      return;
    }
    value_ = value;
    NotifyChanged();
  }

 private:
  const std::vector<std::string> value_names_;
  int value_;
};

//==============================================================================
// CallbackActionProperty
//==============================================================================

// An action property that invokes a callback when triggered.
//
// The callback runs while the scene is synchronizing its views, so it must not
// enable, disable, or otherwise restructure views directly. Instead, it can
// change a property that a view's condition depends on (see
// View::Config::condition), or record what needs to change and apply it after
// the scene has run.
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
