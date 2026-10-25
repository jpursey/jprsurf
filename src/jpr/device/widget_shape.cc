// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_shape.h"

#include <algorithm>
#include <optional>
#include <variant>

namespace jpr {

namespace {

const WidgetShape::Field* FindField(absl::Span<const WidgetShape::Field> fields,
                                    std::string_view name) {
  for (const WidgetShape::Field& field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

}  // namespace

// Only the members of the shape's kind are set.
struct WidgetShape::Contents {
  WidgetKind kind = WidgetKind::kControl;

  // Control.
  Control::Inputs inputs;
  Control::Outputs outputs;
  std::vector<std::string> mode_names;

  // Struct.
  std::vector<Field> fields;

  // Array.
  std::optional<WidgetShape> element;
  int size = 0;
};

WidgetShape WidgetShape::ForControl(Control::Inputs inputs,
                                    Control::Outputs outputs,
                                    std::vector<std::string> mode_names) {
  auto contents = std::make_shared<Contents>();
  contents->kind = WidgetKind::kControl;
  contents->inputs = inputs;
  contents->outputs = outputs;
  contents->mode_names = std::move(mode_names);
  return WidgetShape(std::move(contents));
}

WidgetShape WidgetShape::ForStruct(std::vector<Field> fields) {
  auto contents = std::make_shared<Contents>();
  contents->kind = WidgetKind::kStruct;
  contents->fields = std::move(fields);
  return WidgetShape(std::move(contents));
}

WidgetShape WidgetShape::ForArray(WidgetShape element, int size) {
  auto contents = std::make_shared<Contents>();
  contents->kind = WidgetKind::kArray;
  contents->element = std::move(element);
  contents->size = std::max(size, 0);
  return WidgetShape(std::move(contents));
}

WidgetKind WidgetShape::GetKind() const { return contents_->kind; }

Control::Inputs WidgetShape::GetInputs() const { return contents_->inputs; }

Control::Outputs WidgetShape::GetOutputs() const { return contents_->outputs; }

absl::Span<const std::string> WidgetShape::GetModeNames() const {
  return contents_->mode_names;
}

absl::Span<const WidgetShape::Field> WidgetShape::GetFields() const {
  return contents_->fields;
}

const WidgetShape* WidgetShape::GetField(std::string_view name) const {
  const Field* field = FindField(contents_->fields, name);
  return field != nullptr ? &field->shape : nullptr;
}

int WidgetShape::GetSize() const { return contents_->size; }

const WidgetShape* WidgetShape::GetElement() const {
  return contents_->element.has_value() ? &*contents_->element : nullptr;
}

const WidgetShape* WidgetShape::Find(const WidgetPath& path) const {
  const WidgetShape* shape = this;
  for (const WidgetPath::Step& step : path.GetSteps()) {
    if (const std::string* name = std::get_if<std::string>(&step)) {
      shape = shape->GetField(*name);
    } else if (const int* index = std::get_if<int>(&step)) {
      shape = *index >= 0 && *index < shape->GetSize() ? shape->GetElement()
                                                       : nullptr;
    }
    if (shape == nullptr) {
      return nullptr;
    }
  }
  return shape;
}

bool operator==(const WidgetShape& a, const WidgetShape& b) {
  if (a.contents_ == b.contents_) {
    return true;
  }
  const WidgetShape::Contents& x = *a.contents_;
  const WidgetShape::Contents& y = *b.contents_;
  if (x.kind != y.kind) {
    return false;
  }
  switch (x.kind) {
    case WidgetKind::kControl:
      return x.inputs == y.inputs && x.outputs == y.outputs &&
             x.mode_names == y.mode_names;
    case WidgetKind::kStruct:
      if (x.fields.size() != y.fields.size()) {
        return false;
      }
      for (const WidgetShape::Field& field : x.fields) {
        const WidgetShape* other = b.GetField(field.name);
        if (other == nullptr || !(field.shape == *other)) {
          return false;
        }
      }
      return true;
    case WidgetKind::kArray:
      return x.size == y.size && *x.element == *y.element;
  }
  return false;
}

}  // namespace jpr
