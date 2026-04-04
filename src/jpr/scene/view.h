// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {

class Scene;

//==============================================================================
// View
//==============================================================================

// Base class for all views.
//
// A view is a potentially hierarchical mapping of other views rooted in a
// scene, and mappings between the REAPER state and one or more hardware
// controls.
class View final {
 public:
  // Determines the type of context to set for child views
  enum class ChildContextType {
    // No context will be set for child views.
    kNone,

    // Child views will be set to consecutive tracks starting from child tracks
    // of this view.
    kTrack,
  };

  //----------------------------------------------------------------------------
  // View-specific properties
  //----------------------------------------------------------------------------

  // Action properties that will move the child context index one less
  // (child_dec) or one more (child_inc). The resulting child_index will be
  // clamped to the number of child views that match the child context type.
  static constexpr std::string_view kChildDec = "child_dec";
  static constexpr std::string_view kChildInc = "child_inc";

  // Action properties that are the same as "child_dec" and "child_inc" except
  // that they will move the child context index by a bank size settable via
  // SetBankSize(). The default bank size is 8.
  static constexpr std::string_view kBankDec = "bank_dec";
  static constexpr std::string_view kBankInc = "bank_inc";

  // This switches this view's track to be its parent track, if it has a parent
  // track. Otherwise, this does nothing.
  static constexpr std::string_view kTrackParent = "track_parent";

  // This sets his view's track to be the master track.
  static constexpr std::string_view kTrackRoot = "track_root";

  // Tells the parent view to be the same track view as this view. This
  // effectively results in navigating "in" to the current track. If there is no
  // parent view, or this view does not child tracks, this does nothing.
  static constexpr std::string_view kParentTrackChild = "parent_track_child";

  // Tells the parent view to switch to its parent track. This is effectively
  // results in navigating "up" to the parent track from a child track. If there
  // is no parent view, or the parent view's track does not have a parent track,
  // this does nothing.
  static constexpr std::string_view kParentTrackParent = "parent_track_parent";

  // Tells the parent view to switch to the master track. This is effectively
  // results in navigating "up" to the root track from a child track. If there
  // is no parent view, this does nothing.
  static constexpr std::string_view kParentTrackRoot = "parent_track_root";

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  View(const View&) = delete;
  View& operator=(const View&) = delete;
  ~View() = default;

  //----------------------------------------------------------------------------
  // Attributes
  //----------------------------------------------------------------------------

  // Name of the view. Views can be looked up by name relative to their parent
  // view.
  std::string_view GetName() const { return name_; }

  // Current scene, null if not added to a scene yet
  Scene* GetScene() const { return scene_; }

  //----------------------------------------------------------------------------
  // Activation
  //----------------------------------------------------------------------------

  // Enable and disable this view. A view starts disabled by default and must
  // be enabled before it can become active.
  bool IsEnabled() const { return enabled_; }
  void Enable();
  void Disable();

  // Returns true if this view is actively updating the REAPER state and
  // hardware controls according to its mappings. A view is active if it is
  // enabled and its parent view is active (or if it is a root view, the scene
  // is active).
  bool IsActive() const { return active_; }

  // Refreshes the active state of this view and all its child views and
  // mappings based on the current enabled state and parent active state.
  void RefreshActive();

  //----------------------------------------------------------------------------
  // View hierarchy
  //----------------------------------------------------------------------------

  // Parent view, null if this is a root view or it is not yet added to another
  // view.
  View* GetParentView() const { return parent_view_; }

  // Adds a child view to thhis view. This will return null if a view with the
  // same name already exists.
  View* AddChildView(std::string_view name);

  int GetChildViewCount() const { return child_views_.size(); }
  View* GetChildViewAt(int index) const { return child_views_[index].get(); }
  View* GetChildView(std::string_view name) const;

  //----------------------------------------------------------------------------
  // View Context
  //----------------------------------------------------------------------------

  // Returns the current track for this view.
  Track* GetTrack() const;

  // Sets the track for this view.
  //
  // If track is null, this will set the context to a default stub track that
  // has no real functionality, but can be used for mappings. This is useful if
  // a parent view will be setting the actual track dynamically.
  //
  // The child context index is also reset, which will update the context for
  // all child views if there is a child context type set.
  void SetTrack(Track* track = nullptr, int child_context_index = 0);

  // Returns the current child context type and index for this view.
  ChildContextType GetChildContextType() const { return child_context_type_; }
  int GetChildContextIndex() const { return child_context_index_; }

  // Sets the type of context to control for child views of this view.
  //
  // The child context type determines which child views will be affected by the
  // child context index. All child views with the same context type will have
  // their context driven by this view's context and the context_index.
  //
  // For example, if the child context type is kTrack, then all child views with
  // a kTrack context will have their context set to sequential tracks starting
  // at `context_index`. If this view does not have a context, these will be
  // root level tracks, if this view has a track context, they will be children
  // of that track.
  void SetChildContext(ChildContextType context_type, int context_index = 0);

  // Clears the child context for this view, setting the child context type back
  // to the default state with no child context.
  //
  // This is equivalent to calling SetChildContext with ContextType::kNone.
  // Child views will no longer have their context driven by this view's context
  // and context index.
  void ClearChildContext() { SetChildContext(ChildContextType::kNone); }

  // Returns the maximum valid child context index for this view based on the
  // child context type and the number of views of that type. For example, if
  // the child context type is kTrack and there are 8 child views, and 10
  // possible tracks that can be mapped to, this would return 2, since the child
  // context index can only be 0, 1, or 2 to map to valid tracks for all child
  // views.
  int GetMaxChildContextIndex() const;

  // Updates the child context index for this view, which will update the
  // context for all child views with the matching child context type.
  void SetChildContextIndex(int context_index);

  // Refreshes the context for all child views with the matching child context
  // type, if the view is active.
  void RefreshChildContext();

  //----------------------------------------------------------------------------
  // Property settings
  //----------------------------------------------------------------------------

  // Controls the bank size for this view, which is used by the kBankLeft and
  // kBankRight properties to determine how much to change the child context
  // index when activated. The default bank size is 8.
  int GetBankSize() const { return bank_size_; }
  void SetBankSize(int bank_size) { bank_size_ = bank_size; }

  //----------------------------------------------------------------------------
  // Mappings
  //----------------------------------------------------------------------------

  // Adds a mapping to this view.
  //
  // This will return false if the mapping is invalid, for instance if the
  // named control or property doesn't exist in the scene.
  bool AddMapping(ViewMapping::TypeFlags type, std::string_view property_name,
                  std::string_view control_name,
                  ViewMapping::Config config = {});

  // Synchronizes all active mappings for this view and all active child views.
  // This should be called whenever the view is active.
  void SyncMappings();

 private:
  friend class Scene;

  class ChildIndexOffsetProperty;
  class ChildIndexBankOffsetProperty;
  class TrackParentProperty;
  class TrackRootProperty;
  class ParentTrackChildProperty;
  class ParentTrackParentProperty;
  class ParentTrackRootProperty;

  View(Scene* scene, View* parent_view, std::string_view name);

  // Returns the property with the given name in this view's scope, or null if
  // no such property exists. For example, if this view has a track context,
  // this will return the track property with the given name for that track, if
  // it exists. If it is a view-specific property, such as "child_inc", then
  // this will return that property.
  ViewProperty* GetProperty(std::string_view name) const;

  // Sets the context for all child views with a track context type to the
  // child tracks of this view's track context, starting at the child context
  // index. This should only be called when this view is active, and the child
  // context type is kTrack.
  void SetChildTracks();

  // Constructed state
  Scene* scene_;
  View* parent_view_;
  std::string name_;

  // Current state
  bool enabled_ = false;
  bool active_ = false;

  // View hierarchy
  std::vector<std::unique_ptr<View>> child_views_;
  absl::flat_hash_map<std::string, View*> child_views_by_name_;

  // Context
  TrackProperties track_properties_;
  ChildContextType child_context_type_ = ChildContextType::kNone;
  int child_context_index_ = 0;

  // Properties for the view.
  int bank_size_ = 8;
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;

  // Mappings for this view.
  std::vector<std::unique_ptr<ViewMapping>> mappings_;
};

}  // namespace jpr