// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "jpr/common/track.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

//==============================================================================
// View properties
//==============================================================================

// Every view: property (see View::kChildDec), with what it needs of its view.
// A list adds the properties of each need its view meets, by a
// ViewPropertyName, so it can't add one without a row here. A "writable"
// reference is one the view may set (see Scene::AddTrackReference()).
inline constexpr PropertyInfo kViewProperties[] = {
    {View::kChildDec, ViewProperty::Type::kAction, kTriggerAccess,
     "Scrolls the view's list one item back. The scroll position is kept in "
     "range.",
     ViewNeed::kList},
    {View::kChildInc, ViewProperty::Type::kAction, kTriggerAccess,
     "Scrolls the view's list one item forward. The scroll position is kept "
     "in range.",
     ViewNeed::kList},
    {View::kBankDec, ViewProperty::Type::kAction, kTriggerAccess,
     "Scrolls the view's list back by its bank size. The scroll position is "
     "kept in range.",
     ViewNeed::kList},
    {View::kBankInc, ViewProperty::Type::kAction, kTriggerAccess,
     "Scrolls the view's list forward by its bank size. The scroll position "
     "is kept in range.",
     ViewNeed::kList},
    {View::kTrackParent, ViewProperty::Type::kAction, kTriggerAccess,
     "Sets the view's reference to its track's parent track, navigating up "
     "one level, with the track that was left centered in the list if "
     "possible. This does nothing if the track has no parent track.",
     ViewNeed::kChildTracksList},
    {View::kTrackRoot, ViewProperty::Type::kAction, kTriggerAccess,
     "Sets the view's reference to the master track.",
     ViewNeed::kChildTracksList},
    {View::kParentTrackChild, ViewProperty::Type::kAction, kTriggerAccess,
     "Sets the parent view's reference to this view's track, navigating into "
     "it, if it has child tracks on the surface.",
     ViewNeed::kChildTracksItem},
    {View::kParentTrackParent, ViewProperty::Type::kAction, kTriggerAccess,
     "The same as \"view:track_parent\" on the parent view.",
     ViewNeed::kChildTracksItem},
    {View::kParentTrackRoot, ViewProperty::Type::kAction, kTriggerAccess,
     "The same as \"view:track_root\" on the parent view.",
     ViewNeed::kChildTracksItem},
    {View::kParentRouteOtherTrack, ViewProperty::Type::kAction, kTriggerAccess,
     "Sets the parent view's reference to the track at the other end of this "
     "view's route, showing the other route type: from a send to the "
     "destination track's receives, or from a receive to the source track's "
     "sends. The list is scrolled so the route back to the original track is "
     "shown. This does nothing if this view has no route.",
     ViewNeed::kRoutesItem},
    {View::kChildRouteToggle, ViewProperty::Type::kAction, kTriggerAccess,
     "Switches the view's routes list between sends and receives, from the "
     "start of the list. This does nothing if the track has no routes of the "
     "other type.",
     ViewNeed::kRoutesList},
    {View::kChildRouteTypeName, ViewProperty::Type::kText, kReadAccess,
     "The route type the view's routes list shows: \"Send\" or \"Recv\".",
     ViewNeed::kRoutesList},
};
static_assert(AreValidProperties(kViewProperties, kViewNamespace),
              "kViewProperties rows must have unique names in view:");

// The name of a view: property, which must have a row in kViewProperties: a
// name without one doesn't compile.
class ViewPropertyName {
 public:
  // Implicit, so a list adds a property by its name alone.
  consteval ViewPropertyName(std::string_view name) : name_(name) {
    if (std::ranges::find(kViewProperties, name, &PropertyInfo::name) ==
        std::end(kViewProperties)) {
      throw "A view: property needs a row in kViewProperties";
    }
  }

  std::string_view Get() const { return name_; }

 private:
  std::string_view name_;
};

//==============================================================================
// ViewList
//==============================================================================

// The list of a view (see View::ListConfig), which lays out the view's list
// item views, and creates the view: properties that act on it. This is
// internal to View.
class ViewList {
 public:
  // Creates the list for the view, and adds the view: properties that act on
  // it to the view. The writable reference is the reference the view is bound
  // to if the view may set it (which navigation and revealing move), and is
  // otherwise null. Returns null (logging why) if the config isn't valid.
  static std::unique_ptr<ViewList> Create(View* view,
                                          const View::ListConfig& config,
                                          TrackReference* writable_reference);

  // The needs of view: properties (see ViewNeed) that a list meets: those of
  // its view, and of each of its item views, if any.
  struct Needs {
    std::vector<ViewNeed> view;
    std::optional<ViewNeed> item;
  };

  // Returns the needs a list of the config meets, on a view with a writable
  // reference or not (see Create()), which are the view: properties it adds.
  static Needs GetNeeds(const View::ListConfig& config, bool writable);

  ViewList(const ViewList&) = delete;
  ViewList& operator=(const ViewList&) = delete;
  virtual ~ViewList() = default;

  // Creates the subject reference for a new list item view, which the item
  // owns.
  virtual std::unique_ptr<ViewReference> CreateItemReference() = 0;

  // Adds a new list item view, whose reference CreateItemReference() created,
  // and adds the view: properties that act on this list to it. The list lays
  // out again when it is next updated.
  void AddItem(View* item);

  // Lays out the list again if the view's subject changed since the last
  // layout (from the start of the list, and for a routes list, picking the
  // route type by its rule), or the track list changed (keeping the scroll
  // position in range).
  void Update();

  // Called when the view becomes active, after Update().
  virtual void OnActivated() {}

  // Polls what the list shows. This is called each run while the view is
  // active, after Update().
  virtual void Sync() {}

 protected:
  ViewList(View* view, std::optional<int> bank_size,
           TrackReference* writable_reference);

  View* GetView() const { return view_; }
  Track* GetTrack() const { return view_->GetTrack(); }
  const std::vector<View*>& GetItems() const { return items_; }
  int GetItemCount() const { return static_cast<int>(items_.size()); }
  int GetPosition() const { return position_; }
  bool HasWritableReference() const { return writable_reference_ != nullptr; }
  TrackFilter GetTrackFilter() const;

  // Returns the subject reference of a list item view, which must be the type
  // CreateItemReference() created.
  template <typename ReferenceType>
  static ReferenceType* GetItemReference(View* item) {
    return static_cast<ReferenceType*>(item->item_reference_.get());
  }

  // Adds an action property to the view (the list's view, or one of its
  // items), which brings the list's view up to date (see
  // View::UpdateSubject()) before it calls the callback.
  void AddAction(View* view, ViewPropertyName name,
                 absl::AnyInvocable<void()> callback);

  // Adds a property of type PropertyType to the view, created from its name
  // and `args`, and returns it.
  template <typename PropertyType, typename... Args>
  static PropertyType* AddProperty(View* view, ViewPropertyName name,
                                   Args&&... args) {
    auto property =
        std::make_unique<PropertyType>(name.Get(), std::forward<Args>(args)...);
    PropertyType* added_property = property.get();
    view->properties_.emplace(name.Get(), std::move(property));
    return added_property;
  }

  // Scrolls to the position, kept in range, and lays out the list if it moved.
  void SetPosition(int position);

  // Lays out the list from the position, kept in range.
  void Relayout(int position);

  // Sets the writable reference to the track, and lays out the list from the
  // position, kept in range, so the position stays. This does nothing if there
  // is no writable reference.
  void MoveTo(Track* track, int position);

 private:
  // The number of items in the list.
  virtual int GetLength() const = 0;

  // Called when the view's subject changed, before the list is laid out from
  // the start.
  virtual void OnSubjectChanged() {}

  // Points each item at the list's item at the scroll position plus its
  // index, or at nothing past the end of the list.
  virtual void Layout() = 0;

  // Adds the view: properties that act on this list to a new list item view.
  virtual void AddItemProperties(View* item) = 0;

  // See View::kChildInc and View::kBankInc.
  void Scroll(int offset);
  int GetBankSize() const;

  // Returns the scroll position kept in range.
  int ClampPosition(int position) const;

  View* const view_;
  const std::optional<int> bank_size_;
  TrackReference* const writable_reference_;
  std::vector<View*> items_;
  int position_ = 0;

  // The versions of the view's subject and the track list that the list was
  // last laid out for.
  int64_t subject_version_ = -1;
  int64_t track_list_version_ = -1;
};

}  // namespace jpr
