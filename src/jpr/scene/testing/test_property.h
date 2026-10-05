// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "jpr/common/color.h"
#include "jpr/common/timeline.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// TestProperty
//
// A property of any type that holds a value of that type (see
// ViewProperty::Type), which its setters change, notifying only when it
// changes. It is for testing what reads and writes properties, without
// depending on any particular one.
//==============================================================================

class TestProperty final : public ViewProperty {
 public:
  // `value` must hold the type's underlying value. `max_value` is an
  // enumerated property's highest value.
  TestProperty(Type type, Value value, int max_value = 0,
               std::string_view name = "user:value")
      : ViewProperty(name, type),
        value_(std::move(value)),
        max_value_(max_value) {}

  int GetMaxValue() const override { return max_value_; }

 protected:
  bool ReadBool() const override { return std::get<bool>(value_); }
  void WriteBool(bool value) override { Write(value); }
  double ReadDouble() const override { return std::get<double>(value_); }
  void WriteDouble(double value) override { Write(value); }
  std::string ReadString() const override {
    return std::get<std::string>(value_);
  }
  void WriteString(std::string_view value) override {
    Write(std::string(value));
  }
  Color ReadColor() const override { return std::get<Color>(value_); }
  void WriteColor(const Color& value) override { Write(value); }
  TimelinePosition ReadTimelinePosition() const override {
    return std::get<TimelinePosition>(value_);
  }
  void WriteTimelinePosition(TimelinePosition value) override { Write(value); }
  int ReadInt() const override { return std::get<int>(value_); }
  void WriteInt(int value) override { Write(value); }

 private:
  void Write(Value value) {
    if (value != value_) {
      value_ = std::move(value);
      NotifyChanged();
    }
  }

  Value value_;
  int max_value_;
};

}  // namespace jpr
