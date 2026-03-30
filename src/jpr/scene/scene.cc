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

ViewProperty* Scene::GetProperty(std::string_view name) {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  if (name.starts_with("cmd:")) {
    int command_id = 0;
    if (!absl::SimpleAtoi(name.substr(4), &command_id) || command_id == 0) {
      return nullptr;
    }
    int state = GetToggleCommandState(command_id);
    std::unique_ptr<ViewProperty> property;
    if (state < 0) {
      property = std::make_unique<CommandActionProperty>(name, command_id);
    } else {
      property = std::make_unique<CommandToggleProperty>(this, name, command_id,
                                                         state > 0);
    }
    auto property_ptr = property.get();
    properties_[name] = std::move(property);
    return property_ptr;
  }

  // Timeline position properties.
  std::unique_ptr<ViewProperty> property;
  if (name == kTimelinePosition) {
    property = std::make_unique<TimelinePositionProperty>(
        this, name, TimelinePositionProperty::Source::kCurrent);
  } else if (name == kPlaybackPosition) {
    property = std::make_unique<TimelinePositionProperty>(
        this, name, TimelinePositionProperty::Source::kPlayback);
  } else if (name == kEditPosition) {
    property = std::make_unique<TimelinePositionProperty>(
        this, name, TimelinePositionProperty::Source::kEdit);
  }
  // Primary ruler mode properties.
  else if (name == kRulerMode) {
    property = std::make_unique<RulerModeProperty>(this, name);
  } else if (name == kRulerBeats) {
    property = std::make_unique<IsRulerModeProperty>(this, name,
                                                     TimelineMode::kBeats);
  } else if (name == kRulerTime) {
    property = std::make_unique<IsRulerModeProperty>(this, name,
                                                     TimelineMode::kTime);
  } else if (name == kRulerFrames) {
    property = std::make_unique<IsRulerModeProperty>(this, name,
                                                     TimelineMode::kFrames);
  } else if (name == kRulerSamples) {
    property = std::make_unique<IsRulerModeProperty>(this, name,
                                                     TimelineMode::kSamples);
  }
  // Secondary ruler mode properties.
  else if (name == kRuler2Mode) {
    property = std::make_unique<SecondaryRulerModeProperty>(this, name);
  } else if (name == kRuler2Time) {
    property = std::make_unique<IsSecondaryRulerModeProperty>(
        this, name, TimelineMode::kTime);
  } else if (name == kRuler2Frames) {
    property = std::make_unique<IsSecondaryRulerModeProperty>(
        this, name, TimelineMode::kFrames);
  } else if (name == kRuler2Samples) {
    property = std::make_unique<IsSecondaryRulerModeProperty>(
        this, name, TimelineMode::kSamples);
  }

  if (property != nullptr) {
    auto property_ptr = property.get();
    properties_[name] = std::move(property);
    return property_ptr;
  }
  return nullptr;
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