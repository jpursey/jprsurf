// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>
#include <stack>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "jpr/scene/modifier_property.h"

namespace jpr {

Scene::Scene(std::string_view name) : name_(name) {
  root_view_ = absl::WrapUnique(new View(this, nullptr, "root"));
  properties_.emplace(
      ModifierProperty::kShift,
      std::make_unique<ModifierProperty>(ModifierProperty::kShift, kModShift));
  properties_.emplace(
      ModifierProperty::kCtrl,
      std::make_unique<ModifierProperty>(ModifierProperty::kCtrl, kModCtrl));
  properties_.emplace(
      ModifierProperty::kAlt,
      std::make_unique<ModifierProperty>(ModifierProperty::kAlt, kModAlt));
  properties_.emplace(
      ModifierProperty::kOpt,
      std::make_unique<ModifierProperty>(ModifierProperty::kOpt, kModOpt));
}

Scene::~Scene() = default;

void Scene::AddDevice(std::string_view device_name,
                      std::unique_ptr<Device> device) {
  for (const auto& control : device->GetControls()) {
    std::string control_name =
        absl::StrCat(device_name, "/", control->GetName());
    controls_.emplace(std::move(control_name), control.get());
  }
  devices_.emplace(device_name, std::move(device));
}

Control* Scene::GetControl(std::string_view name) const {
  auto it = controls_.find(name);
  return it != controls_.end() ? it->second : nullptr;
}

ViewProperty* Scene::GetProperty(std::string_view name) const {
  // TODO: Create properties on demand for valid property names, if it doesn't
  // already exist.
  auto it = properties_.find(name);
  return it != properties_.end() ? it->second.get() : nullptr;
}

Modifiers Scene::AddModifierProperty(std::string_view name) {
  if (next_modifier_flag_ == 0 || properties_.contains(name)) {
    return 0;
  }
  Modifiers flag = next_modifier_flag_;
  next_modifier_flag_ <<= 1;
  properties_.emplace(name, std::make_unique<ModifierProperty>(name, flag));
  return flag;
}

void Scene::Activate(RunRegistry& registry) {
  run_handle_ =
      registry.AddRunnable([this](const RunTime& time) { OnRun(time); });
  root_view_->RefreshActive();
}

void Scene::Deactivate() {
  run_handle_ = {};
  root_view_->RefreshActive();
}

void Scene::OnRun(const RunTime& time) {
  if (root_view_->IsActive()) {
    root_view_->SyncMappings();
  }
}

}  // namespace jpr