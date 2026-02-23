// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/types/span.h"
#include "jpr/device/control.h"

namespace jpr {

//==============================================================================
// Device
//==============================================================================

// A device represents a physical hardware device or external software emulating
// a device that is represented as a bunch of named controls.
//
// Registered devices are detected and connected by the ControlSurface at start
// up, and the controls of connected devices are exposed for mapping to Reaper
// parameters and actions.
class Device {
 public:
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  virtual ~Device() = default;

  // Returns all controls for this device.
  absl::Span<const std::unique_ptr<Control>> GetControls() const {
    return controls_;
  }

  // Returns the control with the given name, or nullptr if no such control
  // exists.
  Control* GetControl(std::string_view name) const;

 protected:
  Device() = default;

  // Derived classes should call this to add a control to the device.
  void AddControl(std::unique_ptr<Control> control);

 private:
  std::vector<std::unique_ptr<Control>> controls_;
  absl::flat_hash_map<std::string, Control*> control_map_;
};

}  // namespace jpr