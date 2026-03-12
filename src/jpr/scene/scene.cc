// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

namespace jpr {

Scene::Scene(std::string_view name) : name_(name) {
  root_view_ = absl::WrapUnique(new View(this, nullptr, "root"));
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

void Scene::Activate(RunRegistry& registry) {
  run_handle_ =
      registry.AddRunnable([this](const RunTime& time) { OnRun(time); });
}

void Scene::Deactivate() { run_handle_ = {}; }

void Scene::AddActiveMapping(ViewMapping* mapping) {
  active_mappings_.insert(mapping);
}

void Scene::RemoveActiveMapping(ViewMapping* mapping) {
  active_mappings_.erase(mapping);
}

void Scene::OnRun(const RunTime& time) {
  // TODO
}

}  // namespace jpr