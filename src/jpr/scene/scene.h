// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/runner.h"
#include "jpr/common/track.h"
#include "jpr/device/device.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view.h"

namespace jpr {

// This is the top level mapping and view between one or more devices and the
// REAPER state.
//
// It holds the state for all actively mapped controls and reaper properties,
// and is responsible for ensuring synchronization.
class Scene final {
 public:
  // The track filter decides which tracks the scene's views show, and which
  // tracks its track actions include in ranges.
  explicit Scene(std::string_view name,
                 TrackFilter track_filter = TrackFilter::kMcp);
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  ~Scene();

  // Attributes
  std::string_view GetName() const { return name_; }

  // Devices
  void AddDevice(std::string_view device_name, std::unique_ptr<Device> device);

  // Views
  View* GetRootView() const { return root_view_.get(); }

  // Tracks
  TrackFilter GetTrackFilter() const { return track_actions_.GetTrackFilter(); }
  TrackActions& GetTrackActions() { return track_actions_; }

  // Controls and Properties
  Control* GetControl(std::string_view name) const;

  // Returns the global property with the name, or null if there is none (see
  // the property namespaces in view_property.h).
  ViewProperty* GetProperty(std::string_view name);

  // Adds a property created outside the scene (for instance, by the plugin), so
  // it can be mapped by name like any built-in property.
  //
  // This returns the added property, or null if the name is not in the user:
  // namespace, or is already used. On failure the property is destroyed.
  template <typename PropertyType>
    requires std::derived_from<PropertyType, ViewProperty>
  PropertyType* AddUserProperty(std::unique_ptr<PropertyType> property) {
    return static_cast<PropertyType*>(DoAddUserProperty(std::move(property)));
  }

  // Adds a property that always reads the value, and ignores writes, such as a
  // light that is always on in a view. It has a new name in the const:
  // namespace, so a mapping refers to it by the returned property's GetName().
  //
  // This returns null if the type and value can't be a constant (see
  // CreateConstProperty() for the ones that can).
  ViewProperty* AddConstProperty(ViewProperty::Type type,
                                 ViewProperty::Value value);

  // Adds a new toggle property that can be mapped to an unused modifier flag.
  //
  // If no more modifier flags are available, or the name is not in the mod:
  // namespace or is already used, this will return zero.
  //
  // Modifier properties are already preset for "mod:shift", "mod:ctrl",
  // "mod:alt", and "mod:opt", but this allows for up to 60 additional modifiers
  // to be added for mapping to custom properties.
  Modifiers AddModifierProperty(std::string_view name);

  // Activation and deactivation
  bool IsActive() const { return run_handle_.IsRegistered(); }
  void Activate(RunRegistry& registry);
  void Deactivate();

 private:
  friend class View;
  friend class SceneStateProperty;

  void OnRun(const RunTime& time);

  // Called when a view with a condition is added, so the scene applies changes
  // to its condition (see ApplyViewConditions()).
  void AddConditionalView(View* view);

  // Refreshes the views whose condition changed. This must not be called while
  // the views are running, as it enables and disables them.
  void ApplyViewConditions();

  // Implements AddUserProperty() for any property type.
  ViewProperty* DoAddUserProperty(std::unique_ptr<ViewProperty> property);

  // Called by stateful properties that are being listened to, to register for
  // updates.
  void RegisterProperty(SceneStateProperty* property);
  void UnregisterProperty(SceneStateProperty* property);

  // State
  std::string name_;
  absl::flat_hash_map<std::string, std::unique_ptr<Device>> devices_;
  absl::flat_hash_map<std::string, Control*> controls_;
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;
  absl::flat_hash_set<SceneStateProperty*> state_properties_;
  TrackActions track_actions_;
  std::unique_ptr<View> root_view_;
  std::vector<View*> conditional_views_;
  RunHandle run_handle_;
  Modifiers next_modifier_flag_ = kModUserStart;
};

}  // namespace jpr