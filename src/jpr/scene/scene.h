// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <concepts>
#include <cstdint>
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
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

// This is the top level mapping and view between one or more devices and the
// REAPER state.
//
// It holds the state for all actively mapped controls and reaper properties,
// and is responsible for ensuring synchronization.
//
// The scene reads REAPER's state through TrackCache, and changes routes through
// ContinuousUndo, so it must run where both are kept current: from a
// ControlSurfaceListener's OnRun(), as its ControlSurface keeps them current.
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

  // Returns the control named <device>/<control>, such as "XTouch/Play", or
  // null if there is none.
  Control* GetControl(std::string_view name) const;

  // Returns the global property with the name, or null if there is none (see
  // the property namespaces in view_property.h). This includes the fields of
  // references, named <reference>.<field>, such as "state:master_track.volume".
  ViewProperty* GetProperty(std::string_view name);

  // Adds a property created outside the scene (for instance, by the plugin), so
  // it can be mapped by name like any built-in property.
  //
  // This returns the added property, or null if the name is not in the user:
  // namespace, contains a '.', or is already used by a property or reference,
  // including a property added to any view (see View::AddUserProperty()). On
  // failure the property is destroyed.
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

  // References

  // Built in references, which the scene keeps current:
  // - The master track.
  // - The last touched track (see TrackCache::GetLastTouchedTrack()), which may
  //   be the master track, or a track that isn't on the surface.
  // - The selected track, if exactly one track is selected, and it is on the
  //   surface.
  static constexpr std::string_view kMasterTrack = kStateName<"master_track">;
  static constexpr std::string_view kLastTouchedTrack =
      kStateName<"last_touched_track">;
  static constexpr std::string_view kSelectedTrack =
      kStateName<"selected_track">;

  // Returns the reference with the name, built in or added, or null if there is
  // none. Only the scene, whoever added the reference, and the lists of views
  // bound to it (navigating, see View::ListConfig) can change what it refers
  // to.
  const ViewReference* GetReference(std::string_view name) const;

  // Returns the track reference with the name, built in or added, or null if
  // there is none.
  const TrackReference* GetTrackReference(std::string_view name) const;

  // Adds a track reference, which anything given the returned pointer may set
  // (see TrackReference::Set()). The scene updates it at the start of each run,
  // in the order references were added (see TrackReference::Update()).
  //
  // This returns null if the name is not in the user: namespace, contains a
  // '.', or is already used by a property (including one added to a view) or
  // reference, or if the fallback or follow is not the name of a track
  // reference.
  TrackReference* AddTrackReference(std::string_view name,
                                    TrackReference::Config config = {});

  // Activation and deactivation
  bool IsActive() const { return run_handle_.IsRegistered(); }
  void Activate(RunRegistry& registry);
  void Deactivate();

 private:
  friend class View;
  friend class SceneStateProperty;

  void OnRun(const RunTime& time);

  // Returns the track reference with the name if it is one anything may set
  // (see AddTrackReference()), or null if it isn't, or there is none.
  TrackReference* GetWritableTrackReference(std::string_view name);

  // Creates a track reference, and adds it to the references by name. The name
  // must be unused, and the fallback and follow are null if there are none.
  TrackReference* CreateTrackReference(std::string_view name,
                                       const TrackReference* fallback = nullptr,
                                       const TrackReference* follow = nullptr);

  // Sets the built in references, then updates every reference in the order
  // they were added. This is called at the start of each run, so everything
  // in the run sees the same references.
  void UpdateReferences();

  // Returns true if the name can be given to a new property or reference added
  // to the scene: it is in the user: namespace, has no '.', and is unused by
  // the scene and every view (see View::IsNewUserName()).
  bool IsNewUserName(std::string_view name) const;

  // Returns true if the name is in the user: namespace, has no '.', and is
  // unused by the scene's own properties and references. This is the scene's
  // part of IsNewUserName(), which also checks the views.
  bool IsUnusedUserName(std::string_view name) const;

  // Called when a view with a condition is added, so the scene applies changes
  // to its condition (see ApplyViewConditions()).
  void AddConditionalView(View* view);

  // Refreshes the views whose condition changed. This must not be called while
  // the views are running, as it enables and disables them.
  void ApplyViewConditions();

  // Sets the profile's values for the scene's workload: its devices, controls,
  // views, mappings, and properties.
  //
  // This is called when the scene is activated, so the values are there while
  // it runs, and again when it is destroyed, so the profile (written after the
  // surface, and so the scene, is destroyed) has everything ever added to it,
  // however it was built.
  void SetWorkloadValues() const;

  // Implements AddUserProperty() for any property type.
  ViewProperty* DoAddUserProperty(std::unique_ptr<ViewProperty> property);

  // Called by stateful properties that are being listened to, to register for
  // updates.
  void RegisterProperty(SceneStateProperty* property);
  void UnregisterProperty(SceneStateProperty* property);

  // State
  std::string name_;
  absl::flat_hash_map<std::string, std::unique_ptr<Device>> devices_;
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;
  absl::flat_hash_set<SceneStateProperty*> state_properties_;
  TrackActions track_actions_;

  // References, in the order they were added, which is the order they are
  // updated in: a reference is added after the references its rules refer to.
  // Views may point at references and their fields, so these are declared
  // before the root view, which is destroyed first.
  std::vector<std::unique_ptr<TrackReference>> track_references_;
  absl::flat_hash_map<std::string, TrackReference*> track_references_by_name_;
  TrackReference* master_track_reference_ = nullptr;
  TrackReference* last_touched_track_reference_ = nullptr;
  TrackReference* selected_track_reference_ = nullptr;

  // The versions of the track list and selection the built in references were
  // last set for.
  int64_t track_list_version_ = -1;
  int64_t selection_version_ = -1;

  std::unique_ptr<View> root_view_;
  std::vector<View*> conditional_views_;
  RunHandle run_handle_;
  Modifiers next_modifier_flag_ = kModUserStart;
};

}  // namespace jpr