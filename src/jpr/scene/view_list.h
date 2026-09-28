// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "jpr/common/track.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

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
  void AddAction(View* view, std::string_view name,
                 absl::AnyInvocable<void()> callback);

  // Adds a property to the view.
  static void AddProperty(View* view, std::unique_ptr<ViewProperty> property);

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
