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
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_override.h"

namespace jpr {

//==============================================================================
// Device
//==============================================================================

// A device represents a physical hardware device or external software emulating
// a device that is represented as a bunch of named controls.
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
  // `overrides` name the inputs and outputs of controls the unit doesn't use,
  // which a derived class doesn't build (see IsUsed()). Each control is named
  // by its path in the device's catalog, as the overrides name it.
  explicit Device(RunRegistry& run_registry,
                  absl::Span<const ControlOverride> overrides = {});

  // Returns true if the unit uses the control's input or output of the type:
  // that is, unless the control's override drops it. A derived class builds
  // only the inputs and outputs that are used.
  bool IsUsed(std::string_view control, ControlInput::Type type) const;
  bool IsUsed(std::string_view control, ControlOutput::Type type) const;

  // Derived classes should call this to add a control to the device. Returns
  // the control, or null (logging an error) if its name is already taken.
  Control* AddControl(Control::Options options);

 private:
  RunRegistry& run_registry_;
  std::vector<std::unique_ptr<Control>> controls_;
  absl::flat_hash_map<std::string, Control*> control_map_;
  absl::flat_hash_map<std::string, ControlOverride> overrides_;  // By control.
};

}  // namespace jpr