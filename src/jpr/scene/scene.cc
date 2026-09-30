// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/scene.h"

#include <memory>
#include <stack>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "gb/profile/profile_point.h"
#include "gb/profile/profile_timer.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/const_property.h"
#include "jpr/scene/modifier_property.h"
#include "jpr/scene/scene_state_property.h"
#include "jpr/scene/state_properties.h"

namespace jpr {

Scene::Scene(std::string_view name, TrackFilter track_filter)
    : name_(name), track_actions_(track_filter) {
  master_track_reference_ = CreateTrackReference(kMasterTrack);
  last_touched_track_reference_ = CreateTrackReference(kLastTouchedTrack);
  selected_track_reference_ = CreateTrackReference(kSelectedTrack);

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

Scene::~Scene() { SetWorkloadValues(); }

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

  // A name with a '.' is a field of a reference, which the reference owns.
  const auto [owner, field] = std::pair<std::string_view, std::string_view>(
      absl::StrSplit(name, absl::MaxSplits('.', 1)));
  if (!field.empty()) {
    const ViewReference* reference = GetReference(owner);
    return reference != nullptr ? reference->GetField(field) : nullptr;
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

bool Scene::IsNewUserName(std::string_view name) const {
  return root_view_->IsNewUserName(name);
}

bool Scene::IsUnusedUserName(std::string_view name) const {
  return name.starts_with(kUserNamespace) && !absl::StrContains(name, '.') &&
         !properties_.contains(name) &&
         !track_references_by_name_.contains(name);
}

ViewProperty* Scene::DoAddUserProperty(std::unique_ptr<ViewProperty> property) {
  if (property == nullptr || !IsNewUserName(property->GetName())) {
    return nullptr;
  }
  ViewProperty* added_property = property.get();
  properties_.emplace(added_property->GetName(), std::move(property));
  return added_property;
}

ViewProperty* Scene::AddConstProperty(ViewProperty::Type type,
                                      ViewProperty::Value value) {
  std::unique_ptr<ViewProperty> property =
      CreateConstProperty(type, std::move(value));
  if (property == nullptr) {
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

const ViewReference* Scene::GetReference(std::string_view name) const {
  return GetTrackReference(name);
}

const TrackReference* Scene::GetTrackReference(std::string_view name) const {
  auto it = track_references_by_name_.find(name);
  return it != track_references_by_name_.end() ? it->second : nullptr;
}

TrackReference* Scene::GetWritableTrackReference(std::string_view name) {
  if (!name.starts_with(kUserNamespace)) {
    return nullptr;
  }
  auto it = track_references_by_name_.find(name);
  return it != track_references_by_name_.end() ? it->second : nullptr;
}

TrackReference* Scene::CreateTrackReference(std::string_view name,
                                            const TrackReference* fallback,
                                            const TrackReference* follow) {
  TrackReference* reference =
      track_references_
          .emplace_back(std::make_unique<TrackReference>(name, &track_actions_,
                                                         fallback, follow))
          .get();
  track_references_by_name_.emplace(name, reference);
  return reference;
}

TrackReference* Scene::AddTrackReference(std::string_view name,
                                         TrackReference::Config config) {
  if (!IsNewUserName(name)) {
    LOG(ERROR) << "Failed to add reference '" << name
               << "': the name is not a new user: name";
    return nullptr;
  }
  const TrackReference* fallback = GetTrackReference(config.fallback);
  if (!config.fallback.empty() && fallback == nullptr) {
    LOG(ERROR) << "Failed to add reference '" << name << "': fallback '"
               << config.fallback << "' is not a track reference";
    return nullptr;
  }
  const TrackReference* follow = GetTrackReference(config.follow);
  if (!config.follow.empty() && follow == nullptr) {
    LOG(ERROR) << "Failed to add reference '" << name << "': follow '"
               << config.follow << "' is not a track reference";
    return nullptr;
  }

  return CreateTrackReference(name, fallback, follow);
}

void Scene::Activate(RunRegistry& registry) {
  run_handle_ =
      registry.AddRunnable([this](const RunTime& time) { OnRun(time); });
  root_view_->RefreshActive();
  SetWorkloadValues();
}

void Scene::Deactivate() {
  run_handle_ = {};
  root_view_->RefreshActive();
}

void Scene::OnRun(const RunTime& time) {
  UpdateReferences();
  {
    gb::ProfileScope<"SceneStateProperty::UpdateState"> scope;
    for (const auto& property : state_properties_) {
      property->UpdateState();
    }
  }

  // A view's condition can change at any time, but the view is only enabled or
  // disabled here, before its mappings run.
  ApplyViewConditions();
  if (root_view_->IsActive()) {
    gb::ProfileScope<"View::SyncMappings"> scope;
    root_view_->SyncMappings();
  }
}

void Scene::SetWorkloadValues() const {
  int view_count = 0;
  int mapping_count = 0;
  int property_count = static_cast<int>(properties_.size());
  std::vector<const View*> views = {root_view_.get()};
  while (!views.empty()) {
    const View* view = views.back();
    views.pop_back();
    ++view_count;
    mapping_count += static_cast<int>(view->mappings_.size());
    property_count += static_cast<int>(view->properties_.size() +
                                       view->user_properties_.size());
    for (const auto& child_view : view->child_views_) {
      views.push_back(child_view.get());
    }
  }
  gb::ProfileSetValue<"devices">(static_cast<int>(devices_.size()));
  gb::ProfileSetValue<"controls">(static_cast<int>(controls_.size()));
  gb::ProfileSetValue<"views">(view_count);
  gb::ProfileSetValue<"mappings">(mapping_count);
  gb::ProfileSetValue<"properties">(property_count);
}

void Scene::UpdateReferences() {
  gb::ProfileScope<"Scene::UpdateReferences"> scope;
  TrackCache& cache = TrackCache::Get();
  const int64_t track_list_version = cache.GetTrackListVersion();
  const int64_t selection_version = cache.GetSelectionVersion();
  const bool track_list_changed = (track_list_version != track_list_version_);
  const bool selection_changed = (selection_version != selection_version_);
  track_list_version_ = track_list_version;
  selection_version_ = selection_version;

  if (track_list_changed) {
    master_track_reference_->Set(cache.GetMasterTrack());
  }
  last_touched_track_reference_->Set(cache.GetLastTouchedTrack());
  if (track_list_changed || selection_changed) {
    // Only a track with a place in the filter is on the surface.
    Track* track = cache.GetOnlySelectedTrack();
    if (track != nullptr && !track->GetGlobalIndex(GetTrackFilter())) {
      track = nullptr;
    }
    selected_track_reference_->Set(track);
  }

  // A reference is added after the references its rules refer to, so updating
  // in the order they were added updates those first.
  for (const auto& reference : track_references_) {
    reference->Update();
  }
}

void Scene::AddConditionalView(View* view) {
  conditional_views_.push_back(view);
}

void Scene::ApplyViewConditions() {
  gb::ProfileScope<"Scene::ApplyViewConditions"> scope;
  for (View* view : conditional_views_) {
    // Refreshing a view also refreshes its child views, which clears their
    // conditions' changes, so they aren't refreshed twice.
    if (!view->condition_->HasChanged()) {
      continue;
    }
    view->RefreshActive();
  }
}

void Scene::RegisterProperty(SceneStateProperty* property) {
  state_properties_.insert(property);
}

void Scene::UnregisterProperty(SceneStateProperty* property) {
  state_properties_.erase(property);
}

}  // namespace jpr