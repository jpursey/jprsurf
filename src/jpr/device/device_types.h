// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
#include "jpr/device/widget_doc.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

// A kind of hardware JPRSurf supports, by name.
struct DeviceType {
  std::string_view name;  // Such as "XTouch".

  // The type's controls: a struct shape, which never changes.
  const WidgetShape* catalog;

  // The documentation of the catalog, from which the type's page is
  // generated.
  const CatalogDoc* catalog_doc;

  // Creates the device on its ports, with its controls named by their paths
  // in the catalog, without the overrides' inputs and outputs.
  std::unique_ptr<Device> (*create)(
      RunRegistry& run_registry, MidiIn* midi_in, MidiOut* midi_out,
      absl::Span<const ControlOverride> overrides);
};

// Returns every device type.
absl::Span<const DeviceType> GetDeviceTypes();

// Returns the device type with the name, or null if there is none.
const DeviceType* FindDeviceType(std::string_view name);

// Creates a device of the type, and validates that the controls it built match
// the catalog (see ValidateDeviceControls()), logging an error for each that
// doesn't. The overrides must be valid for the catalog (see
// ValidateControlOverrides(), which CreateDevices() validates them with).
std::unique_ptr<Device> CreateDevice(
    const DeviceType& type, RunRegistry& run_registry, MidiIn* midi_in,
    MidiOut* midi_out, absl::Span<const ControlOverride> overrides = {});

// Returns true if the device's controls match the catalog, without the
// overrides: every control in the catalog, named by its path, and no others,
// each with the catalog's inputs and outputs less its override's, and as many
// modes as the catalog names (or one, if it names none). A control whose
// override drops outputs may have fewer modes, as dropping an output can lower
// them. Otherwise, an error is added to `errors` (if provided) for
// each problem found, starting with the control, such as `strip[2].fader: is
// missing`.
bool ValidateDeviceControls(const Device& device, const WidgetShape& catalog,
                            absl::Span<const ControlOverride> overrides,
                            std::vector<std::string>* errors);

}  // namespace jpr
