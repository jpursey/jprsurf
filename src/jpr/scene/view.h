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
#include "jpr/common/track.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

class Scene;
class ViewList;

//==============================================================================
// View
//==============================================================================

// A view is a potentially hierarchical mapping of other views rooted in a
// scene, and mappings between the REAPER state and one or more hardware
// controls.
//
// A view may have a subject: a track or route whose properties its track: or
// route: properties are (see GetSubject()). A view may also have a list of the
// child tracks or routes of its track, whose items its list item views show
// (see ListConfig).
class View final {
 public:
  //----------------------------------------------------------------------------
  // Configuration
  //----------------------------------------------------------------------------

  // Where a view's subject comes from (see Config::subject).

  // The parent's subject. The root view has none.
  struct ParentSubject {};

  // A reference the view is bound to (see Scene::GetReference()).
  struct ReferenceSubject {
    std::string name;
  };

  // An item of the parent's list, which the view is a list item view of. The
  // parent must already have its list.
  struct ListItemSubject {};

  // Picks the route type of a routes list when its track changes.
  enum class RouteTypeRule {
    kSends,
    kReceives,
    kSendsUnlessOnlyReceives,
  };

  // A list of the child tracks of the view's track that are on the surface.
  struct ChildTracks {};

  // A list of the sends or receives of the view's track.
  struct Routes {
    RouteTypeRule route_type_rule = RouteTypeRule::kSendsUnlessOnlyReceives;
  };

  // A list shown by the view's list item views, in the order they were added.
  // Item i shows the list's item at the scroll position plus i, and items past
  // the end of the list show nothing (the stub track, or a route with
  // route:exists false). The view must have a track subject.
  //
  // The list lays itself out again whenever the view's subject changes (back
  // to the start of the list, picking the route type by its rule), or the
  // track list changes (keeping the scroll position in range).
  struct ListConfig {
    std::variant<ChildTracks, Routes> items;

    // How far kBankInc and kBankDec scroll. Defaults to the item count.
    std::optional<int> bank_size;
  };

  // Configures a new view (see AddChildView()).
  struct Config {
    // If set, the view is only active while the condition is met. The scene
    // applies changes to the condition between runs, so the condition's
    // property may change at any time.
    std::optional<ViewCondition::Config> condition;

    std::variant<ParentSubject, ReferenceSubject, ListItemSubject> subject;
    std::optional<ListConfig> list;
  };

  //----------------------------------------------------------------------------
  // View-specific properties
  //----------------------------------------------------------------------------

  // Each property only exists on a view it applies to, so a mapping on one
  // anywhere else fails to be added. A "writable" reference is one the view
  // may set (see Scene::AddTrackReference()).

  // On a view with a list: scroll one item back (kChildDec) or forward
  // (kChildInc), or by the bank size (kBankDec and kBankInc). The scroll
  // position is kept in range.
  static constexpr std::string_view kChildDec = kViewName<"child_dec">;
  static constexpr std::string_view kChildInc = kViewName<"child_inc">;
  static constexpr std::string_view kBankDec = kViewName<"bank_dec">;
  static constexpr std::string_view kBankInc = kViewName<"bank_inc">;

  // On a view with a child tracks list, bound to a writable reference: set the
  // reference to its track's parent track (navigating "up" one level), with
  // the track that was left centered in the list if possible (kTrackParent),
  // or to the master track (kTrackRoot). kTrackParent does nothing if the
  // track has no parent track.
  static constexpr std::string_view kTrackParent = kViewName<"track_parent">;
  static constexpr std::string_view kTrackRoot = kViewName<"track_root">;

  // On a list item view of such a view: set the parent's reference to this
  // view's track (navigating "in" to it), if it has child tracks on the
  // surface.
  static constexpr std::string_view kParentTrackChild =
      kViewName<"parent_track_child">;

  // On a list item view of such a view: the same as kTrackParent and
  // kTrackRoot on the parent view.
  static constexpr std::string_view kParentTrackParent =
      kViewName<"parent_track_parent">;
  static constexpr std::string_view kParentTrackRoot =
      kViewName<"parent_track_root">;

  // On a list item view of a view with a routes list, bound to a writable
  // reference: set the parent's reference to the track at the other end of
  // this view's route, showing the other route type. This navigates across the
  // route: from a send to the destination track's receives, or from a receive
  // to the source track's sends. The list is scrolled so the route back to the
  // original track is shown. This does nothing if this view has no route.
  static constexpr std::string_view kParentRouteOtherTrack =
      kViewName<"parent_route_other_track">;

  // On a view with a routes list: switch between showing sends and receives,
  // from the start of the list (doing nothing if the track has no routes of
  // the other type), and a text property with the route type shown, "Send" or
  // "Recv".
  static constexpr std::string_view kChildRouteToggle =
      kViewName<"child_route_toggle">;
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
  // exists, the reference doesn't exist, a list item's parent has no list, a
  // list is given for a view without a track subject, or the condition's
  // property doesn't exist (as seen from the child view).
  View* AddChildView(std::string_view name, const Config& config = {});

  int GetChildViewCount() const { return child_views_.size(); }
  View* GetChildViewAt(int index) const { return child_views_[index].get(); }
  View* GetChildView(std::string_view name) const;

  //----------------------------------------------------------------------------
  // Subject and list
  //----------------------------------------------------------------------------

  // Returns the reference holding the view's subject, or null if it has none.
  // The view's track: or route: properties are the reference's fields, if they
  // match its kind, and don't exist otherwise.
  const ViewReference* GetSubject() const { return subject_; }

  // Returns the view's track: its subject's track, or the stub track if it has
  // no track subject, or it refers to nothing.
  Track* GetTrack() const;

  // For a view with a child tracks list bound to a writable reference, scrolls
  // the list so the track is shown: as little as possible if it is in the
  // list's folder, or otherwise by setting the reference to the track's folder,
  // scrolled so the track is the last item. This does nothing if the track is
  // null or not on the surface (hidden, or the master track).
  void Reveal(Track* track);

  //----------------------------------------------------------------------------
  // Anchor
  //----------------------------------------------------------------------------

  // A view holds at most one anchor, which is released when the view's subject
  // changes, or the view is deactivated. The view does not know what is
  // anchored or why.

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
  // Properties and mappings
  //----------------------------------------------------------------------------

  // Returns the property with the given name, as seen from this view, or null
  // if no such property exists (see the property namespaces in
  // view_property.h).
  ViewProperty* GetProperty(std::string_view name) const;

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
  friend class ViewList;

  View(Scene* scene, View* parent_view, std::string_view name);

  // Applies the config to a new view. Returns false (logging why) if it isn't
  // valid, in which case the caller must discard the view.
  bool ApplyConfig(const Config& config);

  // Returns the subject's field with the name (without its namespace), or null
  // if the view's subject isn't of the kind, or has no such field.
  ViewProperty* GetSubjectField(SubjectKind kind, std::string_view name) const;

  // Brings the view up to date with its subject: releases the anchor if the
  // subject changed since the view was last updated, and lays out its list
  // again if the subject or the track list changed since its last layout (see
  // ViewList::Update()). This is called when the view syncs and becomes
  // active, and before its list acts.
  void UpdateSubject();

  // Constructed state
  Scene* scene_;
  View* parent_view_;
  std::string name_;
  std::unique_ptr<ViewCondition> condition_;  // Null if there is none.

  // Current state
  bool enabled_ = false;
  bool active_ = false;

  // Subject and list. A list item view owns its subject reference, which its
  // parent's list sets. The list is declared before the child views and
  // properties, as they may refer to it.
  std::unique_ptr<ViewReference> item_reference_;
  const ViewReference* subject_ = nullptr;
  int64_t subject_version_ = -1;  // See UpdateSubject().
  std::unique_ptr<ViewList> list_;

  // View hierarchy
  std::vector<std::unique_ptr<View>> child_views_;
  absl::flat_hash_map<std::string, View*> child_views_by_name_;

  // Anchor held for this view's subject.
  AnchorHold anchor_hold_;

  // The view: properties that apply to the view (see above).
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;

  // Mappings for this view.
  std::vector<std::unique_ptr<ViewMapping>> mappings_;
};

}  // namespace jpr
