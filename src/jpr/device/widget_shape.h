// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "jpr/device/control.h"
#include "jpr/device/widget_path.h"

namespace jpr {

//==============================================================================
// WidgetShape
//==============================================================================

// The kinds of widget (see WidgetShape).
enum class WidgetKind { kControl, kStruct, kArray };

// The shape of a widget: what a mapping can use a control for, a struct's
// fields and their shapes, or an array's size and the shape of every element.
// A device type's catalog is a struct shape.
//
// Shapes are immutable values, and cheap to copy, as copies share their
// contents. Building one never fails: names are kept as given, so a struct's
// field names, or a control's mode names, may not be widget names (see
// IsWidgetName()), or may be repeated. Validating a device's catalog reports
// both. Until a shape is validated, GetField() and Find() take the
// first field of a repeated name, and equality is only meaningful between
// shapes without repeated names.
class WidgetShape {
 public:
  // A field of a struct shape (defined below).
  struct Field;

  // Returns the shape of a control, with its inputs, its outputs, and the
  // names of its output modes, by mode number.
  static WidgetShape ForControl(Control::Inputs inputs,
                                Control::Outputs outputs,
                                std::vector<std::string> mode_names = {});

  // Returns the shape of a struct with the fields, in order.
  static WidgetShape ForStruct(std::vector<Field> fields);

  // Returns the shape of an array of `size` elements, each of the element's
  // shape. A negative size is 0.
  static WidgetShape ForArray(WidgetShape element, int size);

  // Copies share their contents. A shape moved from may only be destroyed or
  // assigned to.
  WidgetShape(const WidgetShape&) = default;
  WidgetShape& operator=(const WidgetShape&) = default;
  WidgetShape(WidgetShape&&) = default;
  WidgetShape& operator=(WidgetShape&&) = default;

  WidgetKind GetKind() const;

  // For a control, its inputs, its outputs, and the names of its output modes,
  // by mode number. Empty for any other kind.
  Control::Inputs GetInputs() const;
  Control::Outputs GetOutputs() const;
  absl::Span<const std::string> GetModeNames() const;

  // For a struct, its fields in order, and the field with a name, or null if
  // it has none. Empty and null for any other kind.
  absl::Span<const Field> GetFields() const;
  const WidgetShape* GetField(std::string_view name) const;

  // For an array, its size, and the shape of its elements. 0 and null for any
  // other kind.
  int GetSize() const;
  const WidgetShape* GetElement() const;

  // Returns the shape at the path from this one, or null if there is no widget
  // there: a field its struct doesn't have, an index past the end of its
  // array, or a step into a control.
  const WidgetShape* Find(const WidgetPath& path) const;

  // Equal shapes are the same kind, and: controls with the same inputs,
  // outputs, and mode names; structs with the same field names, each of an
  // equal shape, in any order; or arrays of the same size with equal element
  // shapes.
  friend bool operator==(const WidgetShape& a, const WidgetShape& b);

 private:
  struct Contents;

  explicit WidgetShape(std::shared_ptr<const Contents> contents)
      : contents_(std::move(contents)) {}

  std::shared_ptr<const Contents> contents_;
};

struct WidgetShape::Field {
  std::string name;
  WidgetShape shape;
};

}  // namespace jpr
