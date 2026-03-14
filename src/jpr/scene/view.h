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
  // Determines the type of context for this view.
  enum class ContextType {
    // A view with no extra context. This is the default context for all views.
    kNone,

    // A view with a track context. This allows mappings to be made to the
    // properties of that track, such as volume, pan, etc.
    kTrack,
  };

  // Context which corresponds to the view's context type. For example, if the
  // context type is kTrack, the context will be a shared pointer to a Track.
  using Context =
      std::variant<std::monostate, std::unique_ptr<TrackProperties>>;

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
  // View context
  //----------------------------------------------------------------------------

  // Sets the context for this view.
  //
  // The context type is implied by the type of context passed in. If set,
  // mappings can be made to that context's properties (e.g. volume, pan, etc.).
  void SetContext(Context context);

  // Sets the context to the specified track.
  //
  // If track is null, this will set the context to a default stub track that
  // has no real functionality, but can be used for mappings. This is useful if
  // a parent view will be setting the actual track dynamically.
  void SetTrackContext(Track* track = nullptr);

  // Clears the context for this view, setting it back to the default state with
  // no context.
  //
  // This is equivalent to calling SetContext with a std::monostate.
  void ClearContext() { SetContext(std::monostate()); }

  // Returns the current context type for this view
  //
  // This determines what kind of context this view has, what additional
  // properties can be mapped, and how it participates in a parent view's child
  // context setting.
  ContextType GetContextType() const {
    return static_cast<ContextType>(context_.index());
  }

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
  void SetChildContext(ContextType context_type, int context_index = 0);

  // Clears the child context for this view, setting the child context type back
  // to the default state with no child context.
  //
  // This is equivalent to calling SetChildContext with ContextType::kNone.
  // Child views will no longer have their context driven by this view's context
  // and context index.
  void ClearChildContext() { SetChildContext(ContextType::kNone); }

  // Returns the current child context type and index for this view.
  ContextType GetChildContextType() const { return child_context_type_; }
  int GetChildContextIndex() const { return child_context_index_; }

  // Updates the child context index for this view, which will update the
  // context for all child views with the matching child context type.
  void SetChildContextIndex(int context_index);

  // Refreshes the context for all child views with the matching child context
  // type, if the view is active.
  void RefreshChildContext();

  //----------------------------------------------------------------------------
  // Mappings
  //----------------------------------------------------------------------------

  // Adds a mapping to this view.
  //
  // This will return false if the mapping is invalid, for instance if the
  // named control or property doesn't exist in the scene.
  bool AddMapping(ViewMapping::TypeFlags type, std::string_view property_name,
                  std::string_view control_name,
                  ViewMapping::ReadConfig read_config = {});

  // Synchronizes all active mappings for this view and all active child views.
  // This should be called whenever the view is active.
  void SyncMappings();

 private:
  friend class Scene;

  View(Scene* scene, View* parent_view, std::string_view name)
      : scene_(scene), parent_view_(parent_view), name_(name) {}

  // Returns the property with the given name in this view's context, or null if
  // no such property exists. For example, if this view has a track context,
  // this will return the track property with the given name for that track, if
  // it exists.
  ViewProperty* GetContextProperty(std::string_view name) const;

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
  Context context_;
  ContextType child_context_type_ = ContextType::kNone;
  int child_context_index_ = 0;

  // Mappings for this view.
  std::vector<std::unique_ptr<ViewMapping>> mappings_;
};

}  // namespace jpr