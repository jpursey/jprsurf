// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_property.h"

#include <string_view>

#include "jpr/scene/view_mapping.h"

namespace jpr {

ViewProperty::ViewProperty(std::string_view name, Type type)
    : name_(name), type_(type) {}

void ViewProperty::AddMapping(ViewMapping* mapping) {
  mappings_.insert(mapping);
}

void ViewProperty::RemoveMapping(ViewMapping* mapping) {
  mappings_.erase(mapping);
}

ViewProperty::Value ViewProperty::GetValue() const {
  switch (type_) {
    case Type::kAction:
      return std::monostate{};
    case Type::kToggle:
      return ReadBool();
    case Type::kPan:
    case Type::kVolume:
    case Type::kValue:
      return ReadDouble();
    case Type::kText:
      return ReadString();
    case Type::kColor:
      return ReadColor();
  }
  // Should never reach here since all cases are handled.
  return std::monostate{};
}

void ViewProperty::SetValue(const Value& value) {
  switch (type_) {
    case Type::kAction:
      if (std::holds_alternative<std::monostate>(value)) {
        TriggerAction();
      }
      break;
    case Type::kToggle:
      if (std::holds_alternative<bool>(value)) {
        WriteBool(std::get<bool>(value));
      }
      break;
    case Type::kPan:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::clamp(-1.0, 1.0, std::get<double>(value)));
      }
      break;
    case Type::kVolume:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::max(0.0, std::get<double>(value)));
      }
      break;
    case Type::kValue:
      if (std::holds_alternative<double>(value)) {
        WriteDouble(std::clamp(0.0, 1.0, std::get<double>(value)));
      }
      break;
    case Type::kText:
      if (std::holds_alternative<std::string>(value)) {
        WriteString(std::get<std::string>(value));
      }
      break;
    case Type::kColor:
      if (std::holds_alternative<Color>(value)) {
        WriteColor(std::get<Color>(value));
      }
      break;
  }
}

}  // namespace jpr