// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/const_property.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/strings/str_cat.h"
#include "jpr/common/color.h"

namespace jpr {

namespace {

// The number in the next constant's name. Constants are only created on
// REAPER's UI thread.
int g_next_const_id = 1;

// A property whose value never changes. CreateConstProperty() only creates it
// with a value that holds the type that is read for its property type.
class ConstProperty final : public ViewProperty {
 public:
  ConstProperty(std::string_view name, Type type, Value value)
      : ViewProperty(name, type), value_(std::move(value)) {}
  ~ConstProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override { return std::get<bool>(value_); }
  double ReadDouble() const override { return std::get<double>(value_); }
  std::string ReadString() const override {
    return std::get<std::string>(value_);
  }
  Color ReadColor() const override { return std::get<Color>(value_); }

 private:
  Value value_;
};

// Returns true if the value holds what the type reads, in the type's range.
bool IsConstValue(ViewProperty::Type type, const ViewProperty::Value& value) {
  switch (type) {
    case ViewProperty::Type::kToggle:
      return std::holds_alternative<bool>(value);
    case ViewProperty::Type::kNormalized: {
      const double* number = std::get_if<double>(&value);
      return number != nullptr && *number >= 0.0 && *number <= 1.0;
    }
    case ViewProperty::Type::kText:
      return std::holds_alternative<std::string>(value);
    case ViewProperty::Type::kColor:
      return std::holds_alternative<Color>(value);
    default:
      return false;
  }
}

}  // namespace

std::unique_ptr<ViewProperty> CreateConstProperty(ViewProperty::Type type,
                                                  ViewProperty::Value value) {
  if (!IsConstValue(type, value)) {
    return nullptr;
  }
  return std::make_unique<ConstProperty>(
      absl::StrCat(kConstNamespace, g_next_const_id++), type, std::move(value));
}

}  // namespace jpr
