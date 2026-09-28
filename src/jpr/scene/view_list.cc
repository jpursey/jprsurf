// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_list.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/route_reference.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/value_property.h"

namespace jpr {

namespace {

//==============================================================================
// ChildTrackList
//==============================================================================

class ChildTrackList final : public ViewList {
 public:
  // The reveal reference is null if the list reveals nothing, and otherwise
  // there must be a writable reference.
  ChildTrackList(View* view, std::optional<int> bank_size,
                 TrackReference* writable_reference,
                 const TrackReference* reveal);

  std::unique_ptr<ViewReference> CreateItemReference() override {
    return std::make_unique<TrackReference>(
        "", &GetView()->GetScene()->GetTrackActions());
  }
  void OnActivated() override;
  void Sync() override;
  void Reveal(Track* track) override;

 private:
  int GetLength() const override {
    return GetTrack()->GetChildTrackCount(GetTrackFilter());
  }
  void Layout() override;
  void AddItemProperties(View* item) override;

  // See View::kTrackParent and View::kTrackRoot.
  void MoveToParent();
  void MoveToRoot() { MoveTo(TrackCache::Get().GetMasterTrack(), 0); }

  // Reveals the reveal reference's track, and records its version.
  void RevealReference();

  // See View::ChildTracks::reveal.
  const TrackReference* const reveal_;
  int64_t revealed_version_ = -1;
};

ChildTrackList::ChildTrackList(View* view, std::optional<int> bank_size,
                               TrackReference* writable_reference,
                               const TrackReference* reveal)
    : ViewList(view, bank_size, writable_reference), reveal_(reveal) {
  if (!HasWritableReference()) {
    return;
  }
  AddAction(view, View::kTrackParent, [this] { MoveToParent(); });
  AddAction(view, View::kTrackRoot, [this] { MoveToRoot(); });
}

void ChildTrackList::AddItemProperties(View* item) {
  if (!HasWritableReference()) {
    return;
  }
  AddAction(item, View::kParentTrackChild, [this, item] {
    Track* track = item->GetTrack();
    if (track->GetChildTrackCount(GetTrackFilter()) > 0) {
      MoveTo(track, 0);
    }
  });
  AddAction(item, View::kParentTrackParent, [this] { MoveToParent(); });
  AddAction(item, View::kParentTrackRoot, [this] { MoveToRoot(); });
}

void ChildTrackList::Layout() {
  const TrackFilter filter = GetTrackFilter();

  // Walk the child tracks, skipping any that are not on the surface, and give
  // the items the run of them that starts at the scroll position.
  int skip_count = GetPosition();
  auto item = GetItems().begin();
  for (Track* track : GetTrack()->GetChildTracks()) {
    if (item == GetItems().end()) {
      break;
    }
    if (!track->IsVisible(filter)) {
      continue;
    }
    if (skip_count > 0) {
      --skip_count;
      continue;
    }
    GetItemReference<TrackReference>(*item++)->Set(track);
  }

  // Any items left over have no track to show.
  for (; item != GetItems().end(); ++item) {
    GetItemReference<TrackReference>(*item)->Set(nullptr);
  }
}

void ChildTrackList::MoveToParent() {
  Track* track = GetTrack();
  Track* parent_track = track->GetParentTrack();
  if (parent_track == nullptr) {
    // Already at the top level.
    return;
  }

  // Center the track that was left. If it is not on the surface itself, there
  // is no position to center on, so start at the start of the list.
  MoveTo(parent_track,
         track->GetIndex(GetTrackFilter()).value_or(0) - GetItemCount() / 2);
}

void ChildTrackList::OnActivated() {
  if (reveal_ != nullptr) {
    RevealReference();
  }
}

void ChildTrackList::Sync() {
  if (reveal_ != nullptr && reveal_->GetVersion() != revealed_version_) {
    RevealReference();
  }
}

void ChildTrackList::RevealReference() {
  revealed_version_ = reveal_->GetVersion();
  Reveal(reveal_->GetTrack());
}

void ChildTrackList::Reveal(Track* track) {
  if (track == nullptr) {
    return;
  }

  // A track that is hidden, or the master track, has no strip to scroll to.
  const std::optional<int> index = track->GetIndex(GetTrackFilter());
  if (!index.has_value()) {
    return;
  }
  Track* parent_track = track->GetParentTrack();
  const int last_position = *index - GetItemCount() + 1;
  if (GetTrack() != parent_track) {
    MoveTo(parent_track, last_position);
    return;
  }

  // The track is in the list, so scroll as little as possible to show it.
  if (*index < GetPosition()) {
    SetPosition(*index);
  } else if (*index >= GetPosition() + GetItemCount()) {
    SetPosition(last_position);
  }
}

//==============================================================================
// RouteList
//==============================================================================

// The route type a routes list shows, which reads as text, "Send" or "Recv"
// (see View::kChildRouteTypeName).
class RouteTypeProperty final : public ViewProperty {
 public:
  RouteTypeProperty() : ViewProperty(View::kChildRouteTypeName, Type::kText) {}

  TrackRouteType GetRouteType() const { return route_type_; }
  void SetRouteType(TrackRouteType route_type) {
    if (route_type_ != route_type) {
      route_type_ = route_type;
      NotifyChanged();
    }
  }

 protected:
  std::string ReadString() const override {
    return route_type_ == TrackRouteType::kSend ? "Send" : "Recv";
  }

 private:
  TrackRouteType route_type_ = TrackRouteType::kSend;
};

TrackRouteType GetOtherRouteType(TrackRouteType type) {
  return type == TrackRouteType::kSend ? TrackRouteType::kReceive
                                       : TrackRouteType::kSend;
}

class RouteList final : public ViewList {
 public:
  RouteList(View* view, std::optional<int> bank_size,
            TrackReference* writable_reference, View::RouteTypeRule rule);

  std::unique_ptr<ViewReference> CreateItemReference() override {
    return std::make_unique<RouteReference>(
        "", &GetView()->GetScene()->GetTrackActions());
  }
  void Sync() override;

 private:
  int GetLength() const override {
    return static_cast<int>(GetTrack()->GetRoutes(GetRouteType()).size());
  }
  void OnSubjectChanged() override;
  void Layout() override;
  void AddItemProperties(View* item) override;

  TrackRouteType GetRouteType() const { return route_type_->GetRouteType(); }

  // See View::kChildRouteToggle.
  void ToggleRouteType();

  // See View::kParentRouteOtherTrack.
  void CrossRoute(View* item);

  const View::RouteTypeRule rule_;
  RouteTypeProperty* route_type_ = nullptr;
};

RouteList::RouteList(View* view, std::optional<int> bank_size,
                     TrackReference* writable_reference,
                     View::RouteTypeRule rule)
    : ViewList(view, bank_size, writable_reference), rule_(rule) {
  AddAction(view, View::kChildRouteToggle, [this] { ToggleRouteType(); });
  auto route_type = std::make_unique<RouteTypeProperty>();
  route_type_ = route_type.get();
  AddProperty(view, std::move(route_type));
}

void RouteList::AddItemProperties(View* item) {
  if (HasWritableReference()) {
    AddAction(item, View::kParentRouteOtherTrack,
              [this, item] { CrossRoute(item); });
  }
}

void RouteList::Sync() {
  // REAPER doesn't reliably report route volume, pan, and mute changes, so
  // they are polled.
  GetTrack()->RefreshRoutes();
}

void RouteList::ToggleRouteType() {
  // Nothing changes if the track has no routes of the other type.
  const TrackRouteType other_type = GetOtherRouteType(GetRouteType());
  if (GetTrack()->GetRoutes(other_type).empty()) {
    return;
  }
  route_type_->SetRouteType(other_type);
  Relayout(0);
}

void RouteList::OnSubjectChanged() {
  switch (rule_) {
    case View::RouteTypeRule::kSends:
      route_type_->SetRouteType(TrackRouteType::kSend);
      break;
    case View::RouteTypeRule::kReceives:
      route_type_->SetRouteType(TrackRouteType::kReceive);
      break;
    case View::RouteTypeRule::kSendsUnlessOnlyReceives: {
      Track* track = GetTrack();
      route_type_->SetRouteType(track->GetSends().empty() &&
                                        !track->GetReceives().empty()
                                    ? TrackRouteType::kReceive
                                    : TrackRouteType::kSend);
      break;
    }
  }
}

void RouteList::Layout() {
  // Items past the last route still refer to the route index they would show,
  // but have no route.
  Track* track = GetTrack();
  int index = GetPosition();
  for (View* item : GetItems()) {
    GetItemReference<RouteReference>(item)->Set(track, GetRouteType(), index++);
  }
}

void RouteList::CrossRoute(View* item) {
  const TrackRoute* route = GetItemReference<RouteReference>(item)->GetRoute();
  if (route == nullptr) {
    return;
  }
  Track* track = GetTrack();
  Track* other_track = route->other_track;
  const TrackRouteType other_type = GetOtherRouteType(GetRouteType());

  // Scroll so the route back to the original track is shown.
  absl::Span<const TrackRoute> other_routes =
      other_track->GetRoutes(other_type);
  int position = 0;
  for (int i = 0; i < static_cast<int>(other_routes.size()); ++i) {
    if (other_routes[i].other_track == track) {
      position = i - GetItemCount() + 1;
      break;
    }
  }
  route_type_->SetRouteType(other_type);
  MoveTo(other_track, position);
}

}  // namespace

//==============================================================================
// ViewList
//==============================================================================

std::unique_ptr<ViewList> ViewList::Create(View* view,
                                           const View::ListConfig& config,
                                           TrackReference* writable_reference) {
  if (const auto* routes = std::get_if<View::Routes>(&config.items)) {
    return std::make_unique<RouteList>(
        view, config.bank_size, writable_reference, routes->route_type_rule);
  }

  const auto& child_tracks = std::get<View::ChildTracks>(config.items);
  if (child_tracks.reveal.empty()) {
    return std::make_unique<ChildTrackList>(view, config.bank_size,
                                            writable_reference, nullptr);
  }
  const ViewReference* reveal =
      view->GetScene()->GetReference(child_tracks.reveal);
  if (reveal == nullptr || reveal->GetKind() != SubjectKind::kTrack) {
    LOG(ERROR) << "Failed to add view '" << view->GetName()
               << "': the reference to reveal '" << child_tracks.reveal
               << "' is not a track reference";
    return nullptr;
  }
  if (writable_reference == nullptr || reveal == writable_reference) {
    LOG(ERROR) << "Failed to add view '" << view->GetName()
               << "': a list that reveals must be bound to a writable "
                  "reference, other than the one it reveals";
    return nullptr;
  }
  return std::make_unique<ChildTrackList>(
      view, config.bank_size, writable_reference,
      static_cast<const TrackReference*>(reveal));
}

ViewList::ViewList(View* view, std::optional<int> bank_size,
                   TrackReference* writable_reference)
    : view_(view),
      bank_size_(bank_size),
      writable_reference_(writable_reference) {
  AddAction(view, View::kChildDec, [this] { Scroll(-1); });
  AddAction(view, View::kChildInc, [this] { Scroll(1); });
  AddAction(view, View::kBankDec, [this] { Scroll(-GetBankSize()); });
  AddAction(view, View::kBankInc, [this] { Scroll(GetBankSize()); });
}

void ViewList::AddItem(View* item) {
  AddItemProperties(item);
  items_.push_back(item);
  track_list_version_ = -1;
}

void ViewList::Update() {
  if (view_->GetSubject()->GetVersion() != subject_version_) {
    OnSubjectChanged();
    Relayout(0);
  } else if (TrackCache::Get().GetTrackListVersion() != track_list_version_) {
    Relayout(position_);
  }
}

TrackFilter ViewList::GetTrackFilter() const {
  return view_->GetScene()->GetTrackFilter();
}

void ViewList::AddAction(View* view, std::string_view name,
                         absl::AnyInvocable<void()> callback) {
  AddProperty(view, std::make_unique<CallbackActionProperty>(
                        name, [this, callback = std::move(callback)]() mutable {
                          view_->UpdateSubject();
                          callback();
                        }));
}

void ViewList::AddProperty(View* view, std::unique_ptr<ViewProperty> property) {
  const std::string name(property->GetName());
  view->properties_.emplace(name, std::move(property));
}

void ViewList::SetPosition(int position) {
  if (ClampPosition(position) != position_) {
    Relayout(position);
  }
}

void ViewList::Relayout(int position) {
  subject_version_ = view_->GetSubject()->GetVersion();
  track_list_version_ = TrackCache::Get().GetTrackListVersion();
  position_ = ClampPosition(position);
  Layout();

  // Each item notices its new subject at once, releasing its anchor, rather
  // than when it next syncs.
  for (View* item : items_) {
    item->UpdateSubject();
  }
}

void ViewList::MoveTo(Track* track, int position) {
  if (writable_reference_ == nullptr) {
    return;
  }
  writable_reference_->Set(track);
  Relayout(position);
}

void ViewList::Scroll(int offset) { SetPosition(position_ + offset); }

int ViewList::GetBankSize() const {
  return bank_size_.value_or(GetItemCount());
}

int ViewList::ClampPosition(int position) const {
  return std::clamp(position, 0, std::max(0, GetLength() - GetItemCount()));
}

}  // namespace jpr
