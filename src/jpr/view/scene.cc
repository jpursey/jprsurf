// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/view/scene.h"

namespace jpr {

Scene::Scene(std::string_view name) : name_(name) {}

Scene::~Scene() = default;

void Scene::AddDevice(std::string_view name, std::unique_ptr<Device> device) {
  devices_.emplace(name, std::move(device));
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