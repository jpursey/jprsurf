// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_types.h"

#include <algorithm>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gb/base/flags.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/device/widget_path.h"

namespace jpr {

namespace {

//------------------------------------------------------------------------------
// Device types
//------------------------------------------------------------------------------

// Creates an X-Touch of the model.
template <DeviceXTouch::Type kType>
std::unique_ptr<Device> CreateXTouch(
    RunRegistry& run_registry, MidiIn* midi_in, MidiOut* midi_out,
    absl::Span<const ControlOverride> overrides) {
  return std::make_unique<DeviceXTouch>(kType, run_registry, midi_in, midi_out,
                                        overrides);
}

//------------------------------------------------------------------------------
// Validating controls
//------------------------------------------------------------------------------

// Returns the names of the inputs or outputs, such as "value, press", or
// "none".
template <typename Base>
std::string GetTypeNames(gb::Flags<typename Base::Type> flags) {
  std::vector<std::string_view> names;
  for (typename Base::Type type : Base::kTypes) {
    if (flags.IsSet(type)) {
      names.push_back(Base::GetTypeName(type));
    }
  }
  return names.empty() ? std::string("none") : absl::StrJoin(names, ", ");
}

// The validation of one device's controls against its catalog.
class ControlValidator {
 public:
  ControlValidator(const Device& device,
                   absl::Span<const ControlOverride> overrides,
                   std::vector<std::string>* errors);

  bool IsValid() const { return valid_; }

  // Validates every control of the shape, which is at the path (empty for the
  // catalog itself).
  void Validate(const WidgetShape& shape, const std::string& path);

  // Validates that the device has no controls but those validated.
  void ValidateNoOthers();

 private:
  void ValidateControl(const WidgetShape& shape, const std::string& path);
  void AddError(std::string_view control, std::string_view message);

  const Device& device_;
  std::vector<std::string>* const errors_;
  const absl::flat_hash_map<std::string, ControlOverride> overrides_;
  absl::flat_hash_set<const Control*> validated_;
  bool valid_ = true;
};

ControlValidator::ControlValidator(const Device& device,
                                   absl::Span<const ControlOverride> overrides,
                                   std::vector<std::string>* errors)
    : device_(device),
      errors_(errors),
      overrides_(GetOverridesByControl(overrides)) {}

void ControlValidator::Validate(const WidgetShape& shape,
                                const std::string& path) {
  switch (shape.GetKind()) {
    case WidgetKind::kControl:
      ValidateControl(shape, path);
      break;
    case WidgetKind::kStruct:
      for (const WidgetShape::Field& field : shape.GetFields()) {
        Validate(field.shape, GetFieldPathText(path, field.name));
      }
      break;
    case WidgetKind::kArray:
      for (int i = 0; i < shape.GetSize(); ++i) {
        Validate(*shape.GetElement(), GetElementPathText(path, i));
      }
      break;
  }
}

void ControlValidator::ValidateNoOthers() {
  for (const std::unique_ptr<Control>& control : device_.GetControls()) {
    if (!validated_.contains(control.get())) {
      AddError(control->GetName(), "isn't in the catalog");
    }
  }
}

void ControlValidator::ValidateControl(const WidgetShape& shape,
                                       const std::string& path) {
  const Control* control = device_.GetControl(path);
  if (control == nullptr) {
    AddError(path, "is missing");
    return;
  }
  validated_.insert(control);

  Control::Inputs inputs = shape.GetInputs();
  Control::Outputs outputs = shape.GetOutputs();
  if (auto it = overrides_.find(path); it != overrides_.end()) {
    inputs -= it->second.inputs;
    outputs -= it->second.outputs;
  }
  if (control->GetInputs() != inputs) {
    AddError(path,
             absl::StrCat("has inputs ",
                          GetTypeNames<ControlInput>(control->GetInputs()),
                          ", not ", GetTypeNames<ControlInput>(inputs)));
  }
  if (control->GetOutputs() != outputs) {
    AddError(path,
             absl::StrCat("has outputs ",
                          GetTypeNames<ControlOutput>(control->GetOutputs()),
                          ", not ", GetTypeNames<ControlOutput>(outputs)));
  }
  // Dropping an output can only lower the mode count, as it is the most modes
  // of any output.
  const int mode_count =
      std::max(1, static_cast<int>(shape.GetModeNames().size()));
  if (outputs == shape.GetOutputs() ? control->GetModeCount() != mode_count
                                    : control->GetModeCount() > mode_count) {
    AddError(path, absl::StrCat("has a mode count of ", control->GetModeCount(),
                                ", not ", mode_count));
  }
}

void ControlValidator::AddError(std::string_view control,
                                std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(absl::StrCat(control, ": ", message));
  }
}

}  // namespace

//------------------------------------------------------------------------------
// Device types
//------------------------------------------------------------------------------

absl::Span<const DeviceType> GetDeviceTypes() {
  static const DeviceType kTypes[] = {
      {.name = "XTouch",
       .catalog = &DeviceXTouch::GetCatalog(DeviceXTouch::Type::kFull),
       .create = &CreateXTouch<DeviceXTouch::Type::kFull>},
      {.name = "XTouchExt",
       .catalog = &DeviceXTouch::GetCatalog(DeviceXTouch::Type::kExtender),
       .create = &CreateXTouch<DeviceXTouch::Type::kExtender>},
  };
  return kTypes;
}

const DeviceType* FindDeviceType(std::string_view name) {
  for (const DeviceType& type : GetDeviceTypes()) {
    if (type.name == name) {
      return &type;
    }
  }
  return nullptr;
}

std::unique_ptr<Device> CreateDevice(
    const DeviceType& type, RunRegistry& run_registry, MidiIn* midi_in,
    MidiOut* midi_out, absl::Span<const ControlOverride> overrides) {
  std::unique_ptr<Device> device =
      type.create(run_registry, midi_in, midi_out, overrides);
  std::vector<std::string> errors;
  ValidateControlOverrides(*type.catalog, overrides, &errors);
  for (const std::string& error : errors) {
    LOG(ERROR) << type.name << " has an invalid override: " << error;
  }
  errors.clear();
  ValidateDeviceControls(*device, *type.catalog, overrides, &errors);
  for (const std::string& error : errors) {
    LOG(ERROR) << type.name << " doesn't match its catalog: " << error;
  }
  return device;
}

bool ValidateDeviceControls(const Device& device, const WidgetShape& catalog,
                            absl::Span<const ControlOverride> overrides,
                            std::vector<std::string>* errors) {
  ControlValidator validator(device, overrides, errors);
  validator.Validate(catalog, "");
  validator.ValidateNoOthers();
  return validator.IsValid();
}

}  // namespace jpr
