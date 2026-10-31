// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_doc.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "jpr/device/widget_path.h"

namespace jpr {

namespace {

// The validation of a catalog's doc.
class DocValidator {
 public:
  explicit DocValidator(std::vector<std::string>* errors) : errors_(errors) {}

  bool IsValid() const { return valid_; }

  // Adds the widgets in the struct, which is at the doc path (empty for the
  // catalog itself), to those to document.
  void AddWidgets(const WidgetShape& shape, const std::string& path);

  // Validates the doc against the widgets added.
  void Validate(const CatalogDoc& doc);

 private:
  // A widget to document. Every element of an array has the same shape, so is
  // one widget, with the array.
  struct Widget {
    std::string path;  // As WidgetDoc::path names it.

    // The widget, or for an array, the first element in it that isn't one.
    const WidgetShape* shape;
  };

  // Returns the widget at the doc path, or null if there is none.
  const Widget* FindWidget(std::string_view path) const;

  void ValidateWidget(const WidgetDoc& doc);
  void ValidateModes(const WidgetDoc& doc, const WidgetShape& shape);
  void AddError(std::string_view path, std::string_view message);

  std::vector<std::string>* const errors_;
  std::vector<Widget> widgets_;                       // In catalog order.
  absl::flat_hash_set<std::string_view> documented_;  // By path.
  bool valid_ = true;
};

void DocValidator::AddWidgets(const WidgetShape& shape,
                              const std::string& path) {
  for (const WidgetShape::Field& field : shape.GetFields()) {
    const WidgetShape* widget = &field.shape;
    while (widget->GetKind() == WidgetKind::kArray) {
      widget = widget->GetElement();
    }
    std::string widget_path = GetFieldPathText(path, field.name);
    widgets_.push_back({.path = widget_path, .shape = widget});
    AddWidgets(*widget, widget_path);
  }
}

void DocValidator::Validate(const CatalogDoc& doc) {
  if (doc.description.empty()) {
    AddError("catalog", "has no description");
  }
  for (const WidgetDoc& widget : doc.widgets) {
    ValidateWidget(widget);
  }
  for (const Widget& widget : widgets_) {
    if (!documented_.contains(widget.path)) {
      AddError(widget.path, "isn't documented");
    }
  }
}

const DocValidator::Widget* DocValidator::FindWidget(
    std::string_view path) const {
  for (const Widget& widget : widgets_) {
    if (widget.path == path) {
      return &widget;
    }
  }
  return nullptr;
}

void DocValidator::ValidateWidget(const WidgetDoc& doc) {
  const Widget* widget = FindWidget(doc.path);
  if (widget == nullptr) {
    AddError(doc.path, "names nothing");
    return;
  }
  if (!documented_.insert(widget->path).second) {
    AddError(doc.path, "is documented more than once");
    return;
  }
  if (doc.description.empty()) {
    AddError(doc.path, "has no description");
  }
  ValidateModes(doc, *widget->shape);
}

void DocValidator::ValidateModes(const WidgetDoc& doc,
                                 const WidgetShape& shape) {
  // Only a control has mode names.
  const absl::Span<const std::string> names = shape.GetModeNames();
  absl::flat_hash_set<std::string_view> documented;
  for (const WidgetDoc::Mode& mode : doc.modes) {
    if (!absl::c_linear_search(names, mode.name)) {
      AddError(doc.path, absl::StrCat("mode ", mode.name, " names nothing"));
    } else if (!documented.insert(mode.name).second) {
      AddError(doc.path, absl::StrCat("mode ", mode.name,
                                      " is documented more than once"));
    } else if (mode.description.empty()) {
      AddError(doc.path,
               absl::StrCat("mode ", mode.name, " has no description"));
    }
  }
  bool all_documented = true;
  for (const std::string& name : names) {
    if (!documented.contains(name)) {
      AddError(doc.path, absl::StrCat("mode ", name, " isn't documented"));
      all_documented = false;
    }
  }

  // The page lists the modes in the doc's order, so it must be the control's.
  if (all_documented && documented.size() == doc.modes.size() &&
      !absl::c_equal(names, doc.modes,
                     [](const std::string& name, const WidgetDoc::Mode& mode) {
                       return name == mode.name;
                     })) {
    AddError(doc.path, "has modes out of the control's order");
  }
}

void DocValidator::AddError(std::string_view path, std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(absl::StrCat(path, ": ", message));
  }
}

}  // namespace

const WidgetDoc::Mode* WidgetDoc::FindMode(std::string_view name) const {
  for (const Mode& mode : modes) {
    if (mode.name == name) {
      return &mode;
    }
  }
  return nullptr;
}

const WidgetDoc* CatalogDoc::FindWidget(std::string_view path) const {
  for (const WidgetDoc& widget : widgets) {
    if (widget.path == path) {
      return &widget;
    }
  }
  return nullptr;
}

bool ValidateCatalogDoc(const WidgetShape& catalog, const CatalogDoc& doc,
                        std::vector<std::string>* errors) {
  DocValidator validator(errors);
  validator.AddWidgets(catalog, "");
  validator.Validate(doc);
  return validator.IsValid();
}

}  // namespace jpr
