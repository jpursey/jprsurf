// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/types/span.h"
#include "jpr/device/control.h"
#include "jpr/device/device.h"
#include "jpr/device/spec_line.h"
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
  friend bool operator==(const WidgetSlice&, const WidgetSlice&) = default;

  std::string path;  // The array, such as "xtouch.strip".
  int first = 0;
  std::optional<int> count;
};

// The fields of the struct at a path, apart from the ones it excludes.
struct WidgetInclude {
  friend bool operator==(const WidgetInclude&, const WidgetInclude&) = default;

  std::string path;  // The struct, such as "xtouch".
  std::vector<std::string> excluded;
  SpecLine line;  // Where it was written, for errors.
};

struct WidgetFieldDefinition;

// A struct: the fields of its includes, in order, then its own.
struct WidgetStructDefinition {
  friend bool operator==(const WidgetStructDefinition&,
                         const WidgetStructDefinition&) = default;

  std::vector<WidgetInclude> includes;
  std::vector<WidgetFieldDefinition> fields;
};

// A joined array: the slices' elements, one after another.
struct WidgetJoinDefinition {
  friend bool operator==(const WidgetJoinDefinition&,
                         const WidgetJoinDefinition&) = default;

  std::vector<WidgetSlice> slices;
};

// A field of a struct, and how it is defined: the widget at a path (a control,
// a struct, or an array), a new struct, or a joined array.
struct WidgetFieldDefinition {
  friend bool operator==(const WidgetFieldDefinition&,
                         const WidgetFieldDefinition&) = default;

  std::string name;
  std::variant<std::string, WidgetStructDefinition, WidgetJoinDefinition>
      widget;
  SpecLine line;  // Where it was written, for errors.
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

  SpecLine line;  // Where its device was written, for errors.
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
// or the top-level struct, names it in its message instead. Before that, an
// error about a device, an include, or a field (its name, or its definition)
// starts with where it was written (see GetSpecError()). A device without a
// struct catalog is reported once, not again for each path from it.
//
// The shape of a path definition is the catalog's shape there, of a struct
// definition its fields in order, and of a join an array of its slices'
// element shape, sized by all its slices together.
std::optional<WidgetShape> ValidateWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors);

// A control in a config's widgets (see WidgetRoot::FindControl()).
struct WidgetControl {
  const WidgetShape* shape = nullptr;  // The control's shape, never null.
  Control* control = nullptr;          // Null if its device is absent.
};

// A config's widgets: the top-level struct, and its shape, which has every
// widget the catalogs give, whether or not its device is present. So the shape
// tells a path that names nothing from one whose device is absent, and has the
// mode names of a control that is absent. By default, the struct is empty.
struct WidgetRoot {
  // Returns the control at the path's text, such as "strip[3].fader", with its
  // shape, and the device's control, or null if its device is absent. Returns
  // nullopt, setting `error` to why, if the shape has no control there (see
  // FindControlShape()).
  std::optional<WidgetControl> FindControl(std::string_view path,
                                           std::string* error) const;

  WidgetShape shape = WidgetShape::ForStruct({});
  std::unique_ptr<const Widget> widget =
      Widget::ForStruct(std::vector<Widget::Field>());
};

// Validates the widgets as ValidateWidgets() does, and resolves them on the
// devices that are present. Returns the top-level struct and its shape, or
// nullopt with the errors validating found. The devices must outlive the
// widgets.
//
// Each device's instance of its catalog has the device's controls, or null for
// a control it doesn't have. An absent device's instance has every field at
// its default: its controls are null, its arrays are empty, and its structs
// keep their fields. So a join has the elements of the devices present, in
// order, and a path in a definition, such as "xtouch.strip[7]", addresses the
// catalog: it is a struct with null controls when the X-Touch is absent.
std::optional<WidgetRoot> ResolveWidgets(const WidgetStructDefinition& widgets,
                                         absl::Span<const WidgetDevice> devices,
                                         std::vector<std::string>* errors);

}  // namespace jpr
