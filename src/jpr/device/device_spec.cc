// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_spec.h"

#include <iterator>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/strings/str_cat.h"
#include "jpr/common/midi_port.h"
#include "jpr/device/device_types.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

namespace {

// Stands in for the catalog of a spec whose type isn't a device type, so that
// its name is still validated.
const WidgetShape& GetNoCatalog() {
  static const absl::NoDestructor<WidgetShape> s_no_catalog(
      WidgetShape::ForStruct({}));
  return *s_no_catalog;
}

// The creation of one device list.
class DeviceListCreator {
 public:
  DeviceListCreator(MidiPorts& ports, RunRegistry& run_registry,
                    std::vector<std::string>* errors)
      : ports_(ports), run_registry_(run_registry), errors_(errors) {}

  std::optional<DeviceList> Create(absl::Span<const DeviceSpec> specs);

 private:
  // Returns the spec's type, or null if it isn't one. Validates the spec's
  // type and overrides, but not its name.
  const DeviceType* ValidateSpec(const DeviceSpec& spec);

  // Adds an error for each port of the spec at `index` that an earlier spec
  // already has.
  void ValidateUnsharedPorts(absl::Span<const DeviceSpec> specs, int index);

  // Adds an error for each port of a required device that isn't found.
  void ValidatePorts(const DeviceSpec& spec);

  // Returns true if both of the spec's ports are found.
  bool HasPorts(const DeviceSpec& spec) const;

  void AddError(std::string error);

  MidiPorts& ports_;
  RunRegistry& run_registry_;
  std::vector<std::string>* const errors_;
  bool valid_ = true;
};

std::optional<DeviceList> DeviceListCreator::Create(
    absl::Span<const DeviceSpec> specs) {
  DeviceList list;
  std::vector<const DeviceType*> types;
  list.widget_devices.reserve(specs.size());
  types.reserve(specs.size());
  for (int i = 0; i < std::ssize(specs); ++i) {
    const DeviceType* type = ValidateSpec(specs[i]);
    ValidateUnsharedPorts(specs, i);
    list.widget_devices.push_back(
        {.name = specs[i].name,
         .catalog = type != nullptr ? type->catalog : &GetNoCatalog()});
    types.push_back(type);
  }
  // Validating the widgets of no definitions validates the devices' names.
  if (!ValidateWidgets({}, list.widget_devices, errors_).has_value()) {
    valid_ = false;
  }
  if (!valid_) {
    return std::nullopt;
  }

  for (const DeviceSpec& spec : specs) {
    ValidatePorts(spec);
  }
  if (!valid_) {
    return std::nullopt;
  }

  for (int i = 0; i < std::ssize(specs); ++i) {
    const DeviceSpec& spec = specs[i];
    if (!HasPorts(spec)) {
      continue;
    }
    MidiIn* input = ports_.OpenInput(spec.input_port);
    MidiOut* output = ports_.OpenOutput(spec.output_port);
    if (input == nullptr || output == nullptr) {
      if (spec.required) {
        AddError(absl::StrCat(spec.name,
                              ": is required, but its ports failed to open"));
      }
      continue;
    }
    list.devices.push_back(
        CreateDevice(*types[i], run_registry_, input, output, spec.overrides));
    list.widget_devices[i].device = list.devices.back().get();
  }
  if (!valid_) {
    return std::nullopt;
  }
  return list;
}

const DeviceType* DeviceListCreator::ValidateSpec(const DeviceSpec& spec) {
  const DeviceType* type = FindDeviceType(spec.type);
  if (type == nullptr) {
    AddError(absl::StrCat(spec.name, ": type \"", spec.type,
                          "\" isn't a device type"));
    return nullptr;
  }
  std::vector<std::string> override_errors;
  ValidateControlOverrides(*type->catalog, spec.overrides, &override_errors);
  for (const std::string& error : override_errors) {
    AddError(absl::StrCat(spec.name, ": ", error));
  }
  return type;
}

void DeviceListCreator::ValidateUnsharedPorts(
    absl::Span<const DeviceSpec> specs, int index) {
  const DeviceSpec& spec = specs[index];
  for (int i = 0; i < index; ++i) {
    if (specs[i].input_port == spec.input_port) {
      AddError(absl::StrCat(spec.name, ": input port \"", spec.input_port,
                            "\" is also ", specs[i].name, "'s"));
    }
    if (specs[i].output_port == spec.output_port) {
      AddError(absl::StrCat(spec.name, ": output port \"", spec.output_port,
                            "\" is also ", specs[i].name, "'s"));
    }
  }
}

void DeviceListCreator::ValidatePorts(const DeviceSpec& spec) {
  if (!spec.required) {
    return;
  }
  if (!ports_.HasInput(spec.input_port)) {
    AddError(absl::StrCat(spec.name, ": is required, but its input port \"",
                          spec.input_port, "\" isn't found"));
  }
  if (!ports_.HasOutput(spec.output_port)) {
    AddError(absl::StrCat(spec.name, ": is required, but its output port \"",
                          spec.output_port, "\" isn't found"));
  }
}

bool DeviceListCreator::HasPorts(const DeviceSpec& spec) const {
  return ports_.HasInput(spec.input_port) && ports_.HasOutput(spec.output_port);
}

void DeviceListCreator::AddError(std::string error) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(std::move(error));
  }
}

}  // namespace

std::optional<DeviceList> CreateDevices(absl::Span<const DeviceSpec> specs,
                                        MidiPorts& ports,
                                        RunRegistry& run_registry,
                                        std::vector<std::string>* errors) {
  return DeviceListCreator(ports, run_registry, errors).Create(specs);
}

}  // namespace jpr
