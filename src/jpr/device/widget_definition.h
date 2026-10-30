// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/types/span.h"
#include "jpr/device/device.h"
#include "jpr/device/widget.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

//==============================================================================
// Widget definitions
//==============================================================================

// A config defines its widgets as the fields of a top-level struct. Every path
// in a definition starts at a device (see WidgetDevice), such as
// "xtouch.strip[7]", and addresses its catalog, so it names the same widget
// whatever devices are present.

// A slice of an array: `count` elements from `first`, or the rest of the array
// if `count` isn't set.
struct WidgetSlice {
  std::string path;  // The array, such as "xtouch.strip".
  int first = 0;
  std::optional<int> count;
};

// The fields of the struct at a path, apart from the ones it excludes.
struct WidgetInclude {
  std::string path;  // The struct, such as "xtouch".
  std::vector<std::string> excluded;
};

struct WidgetFieldDefinition;

// A struct: the fields of its includes, in order, then its own.
struct WidgetStructDefinition {
  std::vector<WidgetInclude> includes;
  std::vector<WidgetFieldDefinition> fields;
};

// A joined array: the slices' elements, one after another.
struct WidgetJoinDefinition {
  std::vector<WidgetSlice> slices;
};

// A field of a struct, and how it is defined: the widget at a path (a control,
// a struct, or an array), a new struct, or a joined array.
struct WidgetFieldDefinition {
  std::string name;
  std::variant<std::string, WidgetStructDefinition, WidgetJoinDefinition>
      widget;
};

//==============================================================================
// WidgetDevice
//==============================================================================

// A device that paths can start at: one unit, named by the config. Several
// devices of the same type share its catalog, under different names.
struct WidgetDevice {
  std::string name;  // As paths start with it, such as "xtouch".

  // The device type's catalog, a struct shape, which must outlive the call.
  const WidgetShape* catalog = nullptr;

  // The device, or null if it is absent. Its controls are named by their paths
  // in its catalog, such as "strip[0].fader". ValidateWidgets() ignores it.
  Device* device = nullptr;
};

//==============================================================================
// Validating and resolving
//==============================================================================

// Validates the config's widgets, the fields of its top-level struct, against
// the devices' catalogs. Returns the struct's shape, or nullopt with an error
// added to `errors` (if provided) for each problem found. Validating doesn't
// stop at the first problem, and each error starts with what it is about: a
// device and a path in its catalog (such as `xtouch: strip[0]`), or the struct
// or field being defined (such as `transport.play`). An error about a device,
// or the top-level struct, names it in its message instead. A device without a
// struct catalog is reported once, not again for each path from it.
//
// The shape of a path definition is the catalog's shape there, of a struct
// definition its fields in order, and of a join an array of its slices'
// element shape, sized by all its slices together.
std::optional<WidgetShape> ValidateWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors);

// Validates the widgets as ValidateWidgets() does, and resolves them on the
// devices that are present. Returns the top-level struct, or null with the
// errors validating found. The devices must outlive the widgets.
//
// Each device's instance of its catalog has the device's controls, or null for
// a control it doesn't have. An absent device's instance has every field at
// its default: its controls are null, its arrays are empty, and its structs
// keep their fields. So a join has the elements of the devices present, in
// order, and a path in a definition, such as "xtouch.strip[7]", addresses the
// catalog: it is a struct with null controls when the X-Touch is absent.
std::unique_ptr<const Widget> ResolveWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors);

}  // namespace jpr
