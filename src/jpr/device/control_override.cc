// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_override.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_path.h"

namespace jpr {

namespace {

// The validation of one list of overrides.
class OverrideValidator {
 public:
  OverrideValidator(const WidgetShape& catalog, std::string_view device_name,
                    std::vector<std::string>* errors)
      : catalog_(catalog), device_name_(device_name), errors_(errors) {}

  bool IsValid() const { return valid_; }

  void Validate(const ControlOverride& control_override);

 private:
  void AddError(const ControlOverride& control_override,
                std::string_view message);

  const WidgetShape& catalog_;
  const std::string_view device_name_;
  std::vector<std::string>* const errors_;
  absl::flat_hash_set<std::string> controls_;  // By path text.
  bool valid_ = true;
};

void OverrideValidator::Validate(const ControlOverride& control_override) {
  WidgetPath path;
  std::string error;
  const WidgetShape* shape =
      FindControlShape(catalog_, control_override.control, &path, &error);
  if (shape == nullptr) {
    AddError(control_override, error);
    return;
  }
  if (!controls_.insert(path.ToString()).second) {
    AddError(control_override, "is overridden more than once");
  }
  if (control_override.inputs.IsEmpty() && control_override.outputs.IsEmpty()) {
    AddError(control_override, "drops no inputs or outputs");
  }
  for (ControlInput::Type type : ControlInput::kTypes) {
    if (control_override.inputs.IsSet(type) &&
        !shape->GetInputs().IsSet(type)) {
      AddError(
          control_override,
          absl::StrCat("has no ", ControlInput::GetTypeName(type), " input"));
    }
  }
  for (ControlOutput::Type type : ControlOutput::kTypes) {
    if (control_override.outputs.IsSet(type) &&
        !shape->GetOutputs().IsSet(type)) {
      AddError(
          control_override,
          absl::StrCat("has no ", ControlOutput::GetTypeName(type), " output"));
    }
  }
}

void OverrideValidator::AddError(const ControlOverride& control_override,
                                 std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(GetSpecError(
        control_override.line,
        absl::StrCat(device_name_, ": ", control_override.control), message));
  }
}

}  // namespace

bool ValidateControlOverrides(const WidgetShape& catalog,
                              absl::Span<const ControlOverride> overrides,
                              std::string_view device_name,
                              std::vector<std::string>* errors) {
  OverrideValidator validator(catalog, device_name, errors);
  for (const ControlOverride& control_override : overrides) {
    validator.Validate(control_override);
  }
  return validator.IsValid();
}

absl::flat_hash_map<std::string, ControlOverride> GetOverridesByControl(
    absl::Span<const ControlOverride> overrides) {
  absl::flat_hash_map<std::string, ControlOverride> by_control;
  for (const ControlOverride& control_override : overrides) {
    const std::optional<WidgetPath> path =
        WidgetPath::Parse(control_override.control);
    if (!path.has_value()) {
      continue;
    }
    std::string control = path->ToString();
    auto [it, is_new] = by_control.try_emplace(control);
    ControlOverride& combined = it->second;
    if (is_new) {
      combined.line = control_override.line;
    }
    combined.control = std::move(control);
    combined.inputs += control_override.inputs;
    combined.outputs += control_override.outputs;
  }
  return by_control;
}

}  // namespace jpr
