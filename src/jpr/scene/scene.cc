// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>

#include "absl/memory/memory.h"
#include "jpr/scene/view_control.h"

namespace jpr {

Scene::Scene(std::string_view name) : name_(name) {
  root_view_ = absl::WrapUnique(new View(this, nullptr, "root"));
}

Scene::~Scene() = default;

void Scene::AddDevice(std::string_view device_name,
                      std::unique_ptr<Device> device) {
  for (const auto& control : device->GetControls()) {
    auto view_control =
        std::make_unique<ViewControl>(device_name, control.get());
    std::string_view view_control_name = view_control->GetName();
    controls_.emplace(view_control_name, std::move(view_control));
  }
  devices_.emplace(device_name, std::move(device));
}

ViewControl* Scene::GetControl(std::string_view name) const {
  auto it = controls_.find(name);
  return it != controls_.end() ? it->second.get() : nullptr;
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

void Scene::OnRun(const RunTime& time) {
  // TODO
}

}  // namespace jpr