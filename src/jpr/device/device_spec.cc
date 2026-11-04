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
const WidgetShape* GetNoCatalog() {
  static const absl::NoDestructor<WidgetShape> s_no_catalog(
      WidgetShape::ForStruct({}));
  return s_no_catalog.get();
}

// Returns the spec's type, or null if it isn't one. Validates the spec's type
// and overrides, but not its name, adding an error to `errors` for each
// problem.
const DeviceType* ValidateSpec(const DeviceSpec& spec,
                               std::vector<std::string>& errors) {
  const DeviceType* type = FindDeviceType(spec.type);
  if (type == nullptr) {
    errors.push_back(absl::StrCat(spec.name, ": type \"", spec.type,
                                  "\" isn't a device type"));
    return nullptr;
  }
  std::vector<std::string> override_errors;
  ValidateControlOverrides(*type->catalog, spec.overrides, &override_errors);
  for (const std::string& error : override_errors) {
    errors.push_back(absl::StrCat(spec.name, ": ", error));
  }
  return type;
}

// Adds an error to `errors` for each port of the spec at `index` that an
// earlier spec already has.
void ValidateUnsharedPorts(absl::Span<const DeviceSpec> specs, int index,
                           std::vector<std::string>& errors) {
  const DeviceSpec& spec = specs[index];
  for (int i = 0; i < index; ++i) {
    if (specs[i].input_port == spec.input_port) {
      errors.push_back(absl::StrCat(spec.name, ": input port \"",
                                    spec.input_port, "\" is also ",
                                    specs[i].name, "'s"));
    }
    if (specs[i].output_port == spec.output_port) {
      errors.push_back(absl::StrCat(spec.name, ": output port \"",
                                    spec.output_port, "\" is also ",
                                    specs[i].name, "'s"));
    }
  }
}

// The creation of one device list.
class DeviceListCreator {
 public:
  DeviceListCreator(MidiPorts& ports, RunRegistry& run_registry,
                    std::vector<std::string>* errors)
      : ports_(ports), run_registry_(run_registry), errors_(errors) {}

  std::optional<DeviceList> Create(absl::Span<const DeviceSpec> specs);

 private:
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
  std::optional<std::vector<WidgetDevice>> widget_devices =
      CreateWidgetDevices(specs, errors_);
  if (!widget_devices.has_value()) {
    return std::nullopt;
  }
  DeviceList list = {.widget_devices = std::move(*widget_devices)};

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
    // The ports of a device that isn't created close as their handles go.
    MidiInHandle input = ports_.OpenInput(spec.input_port);
    MidiOutHandle output = ports_.OpenOutput(spec.output_port);
    if (!input.IsOpen() || !output.IsOpen()) {
      if (spec.required) {
        AddError(absl::StrCat(spec.name,
                              ": is required, but its ports failed to open"));
      }
      continue;
    }
    // Validating found the type.
    list.devices.push_back(CreateDevice(*FindDeviceType(spec.type),
                                        run_registry_, std::move(input),
                                        std::move(output), spec.overrides));
    list.widget_devices[i].device = list.devices.back().get();
  }
  if (!valid_) {
    return std::nullopt;  // Its devices close their ports as they go.
  }
  return list;
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

std::optional<std::vector<WidgetDevice>> CreateWidgetDevices(
    absl::Span<const DeviceSpec> specs, std::vector<std::string>* errors) {
  std::vector<std::string> spec_errors;
  std::vector<WidgetDevice> widget_devices;
  widget_devices.reserve(specs.size());
  for (int i = 0; i < std::ssize(specs); ++i) {
    const DeviceType* type = ValidateSpec(specs[i], spec_errors);
    ValidateUnsharedPorts(specs, i, spec_errors);
    widget_devices.push_back(
        {.name = specs[i].name,
         .catalog = type != nullptr ? type->catalog : GetNoCatalog()});
  }
  // Validating the widgets of no definitions validates the devices' names.
  ValidateWidgets({}, widget_devices, &spec_errors);
  if (spec_errors.empty()) {
    return widget_devices;
  }
  if (errors != nullptr) {
    errors->insert(errors->end(), std::make_move_iterator(spec_errors.begin()),
                   std::make_move_iterator(spec_errors.end()));
  }
  return std::nullopt;
}

std::optional<DeviceList> CreateDevices(absl::Span<const DeviceSpec> specs,
                                        MidiPorts& ports,
                                        RunRegistry& run_registry,
                                        std::vector<std::string>* errors) {
  return DeviceListCreator(ports, run_registry, errors).Create(specs);
}

}  // namespace jpr
