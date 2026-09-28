// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/anchor.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/track_properties.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {

class Scene;

//==============================================================================
// View
//==============================================================================

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

    // Child views will be set to consecutive sends (kSends) or receives
    // (kReceives) of this view's track. Each child view's route properties
    // refer to its route, and its track is the track at the other end of the
    // route. If there are fewer routes than child views, the remaining child
    // views have no route (route:exists is false) and the stub track.
    kSends,
    kReceives,
  };

  // Where a view's subject (its track) comes from (see Config::subject).

  // The view's own track, which SetTrack() and its parent's child context set.
  struct ParentSubject {};

  // A reference the view is bound to. Its track is the view's track, and its
  // fields are the view's track: properties.
  struct ReferenceSubject {
    // The name of a track reference (see Scene::GetReference()).
    std::string name;
  };

  // Configures a new view (see AddChildView()).
  struct Config {
    // If set, the view is only active while the condition is met. The scene
    // applies changes to the condition between runs, so the condition's
    // property may change at any time.
    std::optional<ViewCondition::Config> condition;

    std::variant<ParentSubject, ReferenceSubject> subject;
  };

  //----------------------------------------------------------------------------
  // View-specific properties
  //----------------------------------------------------------------------------

  // Action properties that will move the child context index one less
  // (kChildDec) or one more (kChildInc). The resulting child_index will be
  // clamped to the number of child views that match the child context type.
  static constexpr std::string_view kChildDec = kViewName<"child_dec">;
  static constexpr std::string_view kChildInc = kViewName<"child_inc">;

  // Action properties that are the same as kChildDec and kChildInc except that
  // they move the child context index by a bank size settable via
  // SetBankSize(). The default bank size is 8.
  static constexpr std::string_view kBankDec = kViewName<"bank_dec">;
  static constexpr std::string_view kBankInc = kViewName<"bank_inc">;

  // This switches this view's track to be its parent track, if it has a parent
  // track. Otherwise, this does nothing. This is effectively navigating "up"
  // one level. The child context index is set so the track that was left is
  // centered in the child views, if possible.
  static constexpr std::string_view kTrackParent = kViewName<"track_parent">;

  // This sets his view's track to be the master track.
  static constexpr std::string_view kTrackRoot = kViewName<"track_root">;

  // Tells the parent view to be the same track view as this view. This
  // effectively results in navigating "in" to the current track. If there is no
  // parent view, or this view does not child tracks, this does nothing.
  static constexpr std::string_view kParentTrackChild =
      kViewName<"parent_track_child">;

  // Tells the parent view to switch to its parent track. This is effectively
  // results in navigating "up" to the parent track from a child track. If there
  // is no parent view, or the parent view's track does not have a parent track,
  // this does nothing. This is the same as kTrackParent on the parent view.
  static constexpr std::string_view kParentTrackParent =
      kViewName<"parent_track_parent">;

  // Tells the parent view to switch to the master track. This is effectively
  // results in navigating "up" to the root track from a child track. If there
  // is no parent view, this does nothing.
  static constexpr std::string_view kParentTrackRoot =
      kViewName<"parent_track_root">;

  // Tells the parent view to show the routes of the track at the other end of
  // this view's route, switching between sends and receives. This effectively
  // navigates across the route: from a send to the destination track's
  // receives, or from a receive to the source track's sends. The parent view is
  // scrolled so the route back to the original track is shown. If this view has
  // no route, or the parent view is not showing routes, this does nothing.
  static constexpr std::string_view kParentRouteOtherTrack =
      kViewName<"parent_route_other_track">;

  // Switches this view's child views between showing sends and receives. This
  // does nothing if this view is not showing routes, or its track has no routes
  // of the other type.
  static constexpr std::string_view kChildRouteToggle =
      kViewName<"child_route_toggle">;

  // A text property with the name of the routes shown by this view's child
  // views: "Send" or "Recv", or empty if this view is not showing routes.
  static constexpr std::string_view kChildRouteTypeName =
      kViewName<"child_route_type_name">;

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  View(const View&) = delete;
  View& operator=(const View&) = delete;
  ~View();

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
  //
  // These take effect immediately, so they must not be called while the scene
  // is running (for instance, from a property a mapping triggers). A view that
  // changes while the scene runs needs a condition instead (see
  // Config::condition), which may change at any time.
  bool IsEnabled() const { return enabled_; }
  void Enable();
  void Disable();

  // Returns true if this view is actively updating the REAPER state and
  // hardware controls according to its mappings. A view is active if it is
  // enabled, its condition (if it has one) is met, and its parent view is
  // active (or if it is a root view, the scene is active).
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

  // Adds a child view to this view, configured by the config.
  //
  // This returns null (logging why) if a view with the same name already
  // exists, the reference doesn't exist or isn't a track reference, or the
  // condition's property doesn't exist (as seen from the child view).
  View* AddChildView(std::string_view name, const Config& config = {});

  int GetChildViewCount() const { return child_views_.size(); }
  View* GetChildViewAt(int index) const { return child_views_[index].get(); }
  View* GetChildView(std::string_view name) const;

  //----------------------------------------------------------------------------
  // View Context
  //----------------------------------------------------------------------------

  // Returns the current track for this view, which is the stub track if it has
  // none.
  Track* GetTrack() const;

  // Sets the track for this view.
  //
  // If track is null, this will set the context to a default stub track that
  // has no real functionality, but can be used for mappings. This is useful if
  // a parent view will be setting the actual track dynamically.
  //
  // For a view bound to a reference, this sets the reference instead, if
  // anything may set it (see Scene::AddTrackReference()), and otherwise does
  // nothing. A null track sets the reference to its fallback's track.
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

  // Switches the child context between kSends and kReceives, starting from the
  // first route. This does nothing if the child context type is neither, or
  // this view's track has no routes of the other type. Returns true if it
  // switched.
  bool ToggleChildRouteType();

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
  //
  // This must be called whenever the track list changes, as that is when the
  // tracks and routes available to child views change.
  void RefreshChildContext();

  //----------------------------------------------------------------------------
  // Anchor
  //----------------------------------------------------------------------------

  // A view holds at most one anchor, which is released when the view's context
  // changes (its track or route), or the view is deactivated. The view does not
  // know what is anchored or why.

  // Holds the anchor, replacing (and so releasing) any anchor the view already
  // holds. An empty hold is ignored, so the view keeps any anchor it holds. The
  // hold is released right away if the view is not active.
  void SetAnchor(AnchorHold hold);

  // Releases the view's anchor if it is held on `anchor`. Otherwise this does
  // nothing, so releasing an anchor that was already replaced does not release
  // the anchor that replaced it.
  void ReleaseAnchor(const AnchorBase* anchor);

  // Releases the view's anchor, whatever it is held on.
  void ClearAnchor() { anchor_hold_.Reset(); }

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
  class ParentRouteOtherTrackProperty;
  class ChildRouteToggleProperty;
  class ChildRouteTypeNameProperty;

  View(Scene* scene, View* parent_view, std::string_view name);

  // Applies the config to a new view. Returns false (logging why) if it isn't
  // valid, in which case the caller must discard the view.
  bool ApplyConfig(const Config& config);

  // Returns the property with the given name, as seen from this view, or null
  // if no such property exists (see the property namespaces in
  // view_property.h).
  ViewProperty* GetProperty(std::string_view name) const;

  // If the view is bound to a reference whose track changed since the view
  // last noticed, releases the anchor, lays out the child context from the
  // child context index, and returns true.
  bool NoticeReferenceChange(int child_context_index = 0);

  // Sets the context for all child views with a track context type to the
  // child tracks of this view's track context, starting at the child context
  // index. This should only be called when this view is active, and the child
  // context type is kTrack.
  void SetChildTracks();

  // Sets the route and track context for all child views to consecutive routes
  // of this view's track, starting at the child context index. This should only
  // be called when this view is active, and the child context type is kSends or
  // kReceives.
  void SetChildRoutes();

  // Sets the route for this view's route properties, and the view's track to
  // the track at the other end of the route (or the stub track if there is no
  // such route). The anchor is released if the route changed.
  void SetRoute(Track* track, TrackRouteType type, int index);

  // Constructed state
  Scene* scene_;
  View* parent_view_;
  std::string name_;
  std::unique_ptr<ViewCondition> condition_;  // Null if there is none.

  // Current state
  bool enabled_ = false;
  bool active_ = false;

  // View hierarchy
  std::vector<std::unique_ptr<View>> child_views_;
  absl::flat_hash_map<std::string, View*> child_views_by_name_;

  // Context. A view bound to a reference uses the reference's track and fields
  // rather than its own track properties. The writable reference is the same
  // reference, if anything may set it (see Scene::AddTrackReference()), and
  // otherwise null.
  const TrackReference* reference_ = nullptr;
  TrackReference* writable_reference_ = nullptr;
  int64_t reference_version_ = -1;  // See NoticeReferenceChange().
  TrackProperties track_properties_;
  RouteProperties route_properties_;  // Set by a parent that shows routes.
  ChildContextType child_context_type_ = ChildContextType::kNone;
  int child_context_index_ = 0;

  // Anchor held for this view's context.
  AnchorHold anchor_hold_;

  // Properties for the view.
  int bank_size_ = 8;
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;
  ChildRouteTypeNameProperty* child_route_type_name_property_ = nullptr;

  // Mappings for this view.
  std::vector<std::unique_ptr<ViewMapping>> mappings_;
};

}  // namespace jpr