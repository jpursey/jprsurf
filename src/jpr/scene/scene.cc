// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>
#include <stack>

#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/reaper_property.h"
#include "jpr/scene/scene_state_property.h"
#include "jpr/scene/timeline_property.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

// Creates the property for a "cmd:<id>" name, or returns null if the id is not
// a command id.
std::unique_ptr<ViewProperty> CreateCommandProperty(Scene* scene,
                                                    std::string_view name) {
  int command_id = 0;
  if (!absl::SimpleAtoi(name.substr(kCmdNamespace.size()), &command_id) ||
      command_id == 0) {
    return nullptr;
  }
  int state = GetToggleCommandState(command_id);
  if (state < 0) {
    return std::make_unique<CommandActionProperty>(name, command_id);
  }
  return std::make_unique<CommandToggleProperty>(scene, name, command_id,
                                                 state > 0);
}

// Creates the property for a "state:" name, or returns null if there is none.
std::unique_ptr<ViewProperty> CreateStateProperty(Scene* scene,
                                                  std::string_view name) {
  // Polled toggle properties.
  if (int index = 0;
      absl::SimpleAtoi(name.substr(kStateNamespace.size()), &index)) {
    PolledToggleProperty::ReadFunction read =
        PolledToggleProperty::GetReadFunction(index);
    if (read == nullptr) {
      return nullptr;
    }
    return std::make_unique<PolledToggleProperty>(
        scene, name, read, PolledToggleProperty::GetWriteFunction(index));
  }

  // Timeline position properties.
  if (name == kTimelinePosition) {
    return std::make_unique<TimelinePositionProperty>(
        scene, name, TimelinePositionProperty::Source::kCurrent);
  }
  if (name == kPlaybackPosition) {
    return std::make_unique<TimelinePositionProperty>(
        scene, name, TimelinePositionProperty::Source::kPlayback);
  }
  if (name == kEditPosition) {
    return std::make_unique<TimelinePositionProperty>(
        scene, name, TimelinePositionProperty::Source::kEdit);
  }

  // Primary ruler mode properties.
  if (name == kRulerMode) {
    return std::make_unique<RulerModeProperty>(scene, name);
  }
  if (name == kRulerBeats) {
    return std::make_unique<IsRulerModeProperty>(scene, name,
                                                 TimelineMode::kBeats);
  }
  if (name == kRulerTime) {
    return std::make_unique<IsRulerModeProperty>(scene, name,
                                                 TimelineMode::kTime);
  }
  if (name == kRulerFrames) {
    return std::make_unique<IsRulerModeProperty>(scene, name,
                                                 TimelineMode::kFrames);
  }
  if (name == kRulerSamples) {
    return std::make_unique<IsRulerModeProperty>(scene, name,
                                                 TimelineMode::kSamples);
  }

  // Secondary ruler mode properties.
  if (name == kSecondaryRulerMode) {
    return std::make_unique<SecondaryRulerModeProperty>(scene, name);
  }
  if (name == kSecondaryRulerTime) {
    return std::make_unique<IsSecondaryRulerModeProperty>(scene, name,
                                                          TimelineMode::kTime);
  }
  if (name == kSecondaryRulerFrames) {
    return std::make_unique<IsSecondaryRulerModeProperty>(
        scene, name, TimelineMode::kFrames);
  }
  if (name == kSecondaryRulerSamples) {
    return std::make_unique<IsSecondaryRulerModeProperty>(
        scene, name, TimelineMode::kSamples);
  }
  return nullptr;
}

}  // namespace

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