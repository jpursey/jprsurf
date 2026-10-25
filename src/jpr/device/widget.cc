// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget.h"

#include <iterator>
#include <utility>
#include <variant>

#include "absl/memory/memory.h"

namespace jpr {

std::unique_ptr<const Widget> Widget::ForControl(Control* control) {
  auto widget = absl::WrapUnique(new Widget(WidgetKind::kControl));
  widget->control_ = control;
  return widget;
}

std::unique_ptr<const Widget> Widget::ForStruct(std::vector<Field> fields) {
  auto widget = absl::WrapUnique(new Widget(WidgetKind::kStruct));
  widget->fields_ = std::move(fields);
  return widget;
}

std::unique_ptr<const Widget> Widget::ForArray(
    std::vector<std::unique_ptr<const Widget>> elements) {
  auto widget = absl::WrapUnique(new Widget(WidgetKind::kArray));
  widget->elements_ = std::move(elements);
  return widget;
}

Widget::Fields Widget::GetFields() const { return fields_; }

const Widget* Widget::GetField(std::string_view name) const {
  for (const Field& field : fields_) {
    if (field.name == name) {
      return field.widget.get();
    }
  }
  return nullptr;
}

const Widget* Widget::GetElement(int index) const {
  return index >= 0 && index < std::ssize(elements_) ? elements_[index].get()
                                                     : nullptr;
}

const Widget* Widget::Find(const WidgetPath& path) const {
  const Widget* widget = this;
  for (const WidgetPath::Step& step : path.GetSteps()) {
    if (const std::string* name = std::get_if<std::string>(&step)) {
      widget = widget->GetField(*name);
    } else if (const int* index = std::get_if<int>(&step)) {
      widget = widget->GetElement(*index);
    }
    if (widget == nullptr) {
      return nullptr;
    }
  }
  return widget;
}

}  // namespace jpr
