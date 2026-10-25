// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "jpr/device/control.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

//==============================================================================
// Widget
//==============================================================================

// A widget on the devices that are present: a control, a struct of named
// widgets, or an array of widgets. ResolveWidgets() builds them from a
// config's definitions (see widget_definition.h), and tests can build them by
// hand. A widget holds its devices' controls by pointer, so the devices must
// outlive it.
//
// Widgets never change once built, and are held by std::unique_ptr, so every
// widget in a tree keeps its address for the tree's lifetime. A widget doesn't
// hold its shape: WidgetShape::Find() of the same path gives it, as every
// element of an array has the same shape.
class Widget {
 public:
  // A field of a struct widget (defined below).
  struct Field;

  using Fields = absl::Span<const Field>;
  using Elements = absl::Span<const std::unique_ptr<const Widget>>;

  // Returns a control widget, which is absent if the control is null.
  static std::unique_ptr<const Widget> ForControl(Control* control);

  // Returns a struct widget with the fields, in order, given as a vector or as
  // arguments (such as `ForStruct(Field{"play", ForControl(play)})`).
  static std::unique_ptr<const Widget> ForStruct(std::vector<Field> fields);
  template <typename... Args>
  static std::unique_ptr<const Widget> ForStruct(Args&&... fields)
    requires(std::same_as<std::remove_cvref_t<Args>, Field> && ...);

  // Returns an array widget with the elements present, in order, given as a
  // vector or as arguments.
  static std::unique_ptr<const Widget> ForArray(
      std::vector<std::unique_ptr<const Widget>> elements);
  template <typename... Args>
  static std::unique_ptr<const Widget> ForArray(Args&&... elements)
    requires(std::convertible_to<Args, std::unique_ptr<const Widget>> && ...);

  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  WidgetKind GetKind() const { return kind_; }

  // For a control, the device's control, or null if it is absent. Null for
  // any other kind.
  Control* GetControl() const { return control_; }

  // For a struct, its fields in order, and the field with a name, or null if
  // it has none. A struct keeps every field it is defined with, whatever is
  // absent. Empty and null for any other kind.
  Fields GetFields() const;
  const Widget* GetField(std::string_view name) const;

  // For an array, the elements that are present, and the element at an index,
  // or null if there is none. Empty and null for any other kind.
  Elements GetElements() const { return elements_; }
  const Widget* GetElement(int index) const;

  // Returns the widget at the path from this one, or null if there is none: a
  // field its struct doesn't have, an index past the elements present, or a
  // step into a control.
  const Widget* Find(const WidgetPath& path) const;

 private:
  explicit Widget(WidgetKind kind) : kind_(kind) {}

  const WidgetKind kind_;
  Control* control_ = nullptr;
  std::vector<Field> fields_;
  std::vector<std::unique_ptr<const Widget>> elements_;
};

struct Widget::Field {
  std::string name;
  std::unique_ptr<const Widget> widget;
};

template <typename... Args>
std::unique_ptr<const Widget> Widget::ForStruct(Args&&... fields)
  requires(std::same_as<std::remove_cvref_t<Args>, Field> && ...)
{
  std::vector<Field> field_list;
  field_list.reserve(sizeof...(fields));
  (field_list.push_back(std::forward<Args>(fields)), ...);
  return ForStruct(std::move(field_list));
}

template <typename... Args>
std::unique_ptr<const Widget> Widget::ForArray(Args&&... elements)
  requires(std::convertible_to<Args, std::unique_ptr<const Widget>> && ...)
{
  std::vector<std::unique_ptr<const Widget>> element_list;
  element_list.reserve(sizeof...(elements));
  (element_list.push_back(std::forward<Args>(elements)), ...);
  return ForArray(std::move(element_list));
}

}  // namespace jpr
