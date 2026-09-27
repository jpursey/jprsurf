// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>
#include <stack>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/scene_state_property.h"
#include "jpr/scene/state_properties.h"

namespace jpr {

Scene::Scene(std::string_view name, TrackFilter track_filter)
    : name_(name), track_actions_(track_filter) {
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

ViewProperty* Scene::GetProperty(std::string_view name) {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }

  // Command and state properties are created the first time they are used.
  std::unique_ptr<ViewProperty> property;
  if (name.starts_with(kCmdNamespace)) {
    property = CreateCommandProperty(this, name);
  } else if (name.starts_with(kStateNamespace)) {
    property = CreateStateProperty(this, name);
  }
  if (property == nullptr) {
    return nullptr;
  }
  ViewProperty* property_ptr = property.get();
  properties_.emplace(name, std::move(property));
  return property_ptr;
}

ViewProperty* Scene::AddViewProperty(std::unique_ptr<ViewProperty> property) {
  if (property == nullptr || !property->GetName().starts_with(kUserNamespace) ||
      properties_.contains(property->GetName())) {
    return nullptr;
  }
  ViewProperty* added_property = property.get();
  properties_.emplace(added_property->GetName(), std::move(property));
  return added_property;
}

Modifiers Scene::AddModifierProperty(std::string_view name) {
  if (next_modifier_flag_ == 0 || !name.starts_with(kModNamespace) ||
      properties_.contains(name)) {
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
  for (const auto& property : state_properties_) {
    property->UpdateState();
  }
  if (root_view_->IsActive()) {
    root_view_->SyncMappings();
  }
}

void Scene::RegisterProperty(SceneStateProperty* property) {
  state_properties_.insert(property);
}

void Scene::UnregisterProperty(SceneStateProperty* property) {
  state_properties_.erase(property);
}

}  // namespace jpr