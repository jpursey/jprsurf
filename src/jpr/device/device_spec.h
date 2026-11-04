// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
#include "jpr/device/widget_definition.h"

namespace jpr {

//==============================================================================
// The device list
//==============================================================================

// One device of the surface. Nothing is inferred from its type or from the
// ports that exist, so several units of one type are several entries.
struct DeviceSpec {
  std::string name;  // As widget paths start with it, such as "xtouch_ext".
  std::string type;  // A device type, such as "XTouchExt".

  // Its ports, as REAPER lists them, such as "X-Touch-Ext".
  std::string input_port;
  std::string output_port;

  // Whether the surface needs it. Without a required device, no device is
  // created.
  bool required = true;

  // The inputs and outputs of its controls that the unit doesn't use.
  std::vector<ControlOverride> overrides;
};

// The devices created from a device list.
struct DeviceList {
  // The devices present, in the list's order.
  std::vector<std::unique_ptr<Device>> devices;

  // A device for each spec, in order, to resolve widgets on: its name, its
  // type's catalog, and its device, or null if it is absent.
  std::vector<WidgetDevice> widget_devices;
};

// Returns a widget device for each spec, in order, with its name and its
// type's catalog, and no device, so that widgets can be validated on them
// without ports or devices (see ValidateWidgets()).
//
// Returns nullopt if a spec isn't valid: its type isn't a device type, an
// override isn't valid for the type's catalog (see ValidateControlOverrides()),
// a port is also an earlier spec's, or its name isn't a widget name or is
// repeated. For each problem, an error is added to `errors` (if provided),
// starting with the device's name, such as `xtouch: type "XTouh" isn't a
// device type`, except that an error about the name names it in its message
// instead.
std::optional<std::vector<WidgetDevice>> CreateWidgetDevices(
    absl::Span<const DeviceSpec> specs, std::vector<std::string>* errors);

// Creates each device whose ports are both found, on those ports (see
// CreateDevice()).
//
// A device whose ports don't both open is absent, with neither left open. Each
// device keeps its ports open while it lives.
//
// Returns nullopt, opening no ports, if a spec isn't valid (see
// CreateWidgetDevices()) or a required device is absent, and nullopt, with
// every port it opened closed again, if a required device's ports fail to
// open. For each problem, an error is added to `errors` (if provided),
// starting with the device's name.
std::optional<DeviceList> CreateDevices(absl::Span<const DeviceSpec> specs,
                                        MidiPorts& ports,
                                        RunRegistry& run_registry,
                                        std::vector<std::string>* errors);

}  // namespace jpr
