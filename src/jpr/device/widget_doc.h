// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

//==============================================================================
// Catalog documentation
//
// What a catalog's shape can't say, for someone writing a config: the
// hardware, what each widget is, and what each of a control's modes looks
// like. It is kept apart from the shapes, which hold only what behavior needs.
//==============================================================================

// Documentation of one widget in a catalog.
struct WidgetDoc {
  struct Mode {
    std::string_view name;         // One of the control's mode names.
    std::string_view description;  // Such as "One LED lit at the value."
  };

  // Returns the mode with the name, or null if there is none.
  const Mode* FindMode(std::string_view name) const;

  // Its path in the catalog by field names only, such as "strip.pot", as
  // every element of an array has the same shape, and so the same doc. An
  // array's own doc is at its path, such as "strip", which also documents its
  // element, if that is a control.
  std::string_view path;

  // Such as "The rotary encoder above the fader."
  std::string_view description;

  // Every mode, for a control with modes, in the order of the control's mode
  // names (its mode numbers). Controls with the same modes share
  // one array of them, which their page lists once.
  absl::Span<const Mode> modes;
};

// Documentation of a catalog: the hardware, and each widget.
struct CatalogDoc {
  // Returns the doc of the widget at the path (as WidgetDoc::path names it),
  // or null if there is none.
  const WidgetDoc* FindWidget(std::string_view path) const;

  std::string_view description;  // The hardware, and the ports it shows as.
  std::vector<WidgetDoc> widgets;
};

// Returns true if the doc has a description, and documents every widget in
// the catalog and every mode of each control with modes, in its order, each
// with a description. Otherwise, an error is added to `errors` (if provided)
// for each widget or mode that isn't documented, or has no description, for
// each doc of a path or mode that names nothing or is repeated, and for modes
// out of order, starting with the widget's path, such as `strip.pot: mode dot
// isn't documented`.
bool ValidateCatalogDoc(const WidgetShape& catalog, const CatalogDoc& doc,
                        std::vector<std::string>* errors);

}  // namespace jpr
