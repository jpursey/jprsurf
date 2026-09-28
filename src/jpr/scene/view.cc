// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include <utility>
#include <variant>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "jpr/scene/scene.h"

namespace jpr {

namespace {

// Returns the type of route shown by a kSends or kReceives child context.
TrackRouteType GetChildRouteType(View::ChildContextType context_type) {
  return context_type == View::ChildContextType::kSends
             ? TrackRouteType::kSend
             : TrackRouteType::kReceive;
}

// Switches the view's track to its parent track, with the child context index
// centering the track that was left in the child views. This does nothing if
// the track has no parent track.
void SetTrackToParent(View* view) {
  Track* track = view->GetTrack();
  Track* parent_track = track->GetParentTrack();
  if (parent_track == nullptr) {
    // Already at the top level.
    return;
  }
  const TrackFilter filter = view->GetScene()->GetTrackFilter();
  int track_count = parent_track->GetChildTrackCount(filter);
  int view_count = view->GetChildViewCount();
  // If the track we are moving up from is not on the surface itself, there is
  // no position to center on, so fall back to the start of the child list.
  int start_index =
      std::clamp(track->GetIndex(filter).value_or(0) - view_count / 2, 0,
                 std::max(0, track_count - view_count));
  view->SetTrack(parent_track, start_index);
}

}  // namespace

// A view property that changes the child context index by a specified offset
// when triggered. This is used for the kChildInc and kChildDec properties, and
// with ChildIndexBankOffsetProperty for kBankInc and kBankDec.
class View::ChildIndexOffsetProperty : public ViewProperty {
 public:
  ChildIndexOffsetProperty(View* view, std::string_view name, int offset)
      : view_(view), ViewProperty(name, Type::kAction), offset_(offset) {}
  ~ChildIndexOffsetProperty() override = default;

 protected:
  void TriggerAction() override {
    int new_index =
        std::clamp(view_->GetChildContextIndex() + offset_ * GetStepSize(), 0,
                   view_->GetMaxChildContextIndex());
    view_->SetChildContextIndex(new_index);
  }

  View* GetView() const { return view_; }
  virtual int GetStepSize() const { return 1; }

 private:
  View* const view_;
  const int offset_;
};

class View::ChildIndexBankOffsetProperty : public ChildIndexOffsetProperty {
 public:
  ChildIndexBankOffsetProperty(View* view, std::string_view name, int offset)
      : ChildIndexOffsetProperty(view, name, offset) {}

 protected:
  int GetStepSize() const override { return GetView()->GetBankSize(); }
};

class View::TrackParentProperty : public ViewProperty {
 public:
  explicit TrackParentProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~TrackParentProperty() override = default;

 protected:
  void TriggerAction() override { SetTrackToParent(view_); }

 private:
  View* const view_;
};

class View::TrackRootProperty : public ViewProperty {
 public:
  explicit TrackRootProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~TrackRootProperty() override = default;

 protected:
  void TriggerAction() override {
    view_->SetTrack(TrackCache::Get().GetMasterTrack(), 0);
  }

 private:
  View* const view_;
};

class View::ParentTrackChildProperty : public ViewProperty {
 public:
  explicit ParentTrackChildProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~ParentTrackChildProperty() override = default;

 protected:
  void TriggerAction() override {
    Track* track = view_->GetTrack();
    if (view_->GetParentView() == nullptr ||
        track->GetChildTrackCount(view_->GetScene()->GetTrackFilter()) == 0) {
      return;
    }
    view_->GetParentView()->SetTrack(track, 0);
  }

 private:
  View* const view_;
};

class View::ParentTrackParentProperty : public ViewProperty {
 public:
  explicit ParentTrackParentProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~ParentTrackParentProperty() override = default;

 protected:
  void TriggerAction() override {
    if (view_->GetParentView() != nullptr) {
      SetTrackToParent(view_->GetParentView());
    }
  }

 private:
  View* const view_;
};

class View::ParentTrackRootProperty : public ViewProperty {
 public:
  explicit ParentTrackRootProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~ParentTrackRootProperty() override = default;

 protected:
  void TriggerAction() override {
    if (view_->GetParentView() == nullptr) {
      return;
    }
    view_->GetParentView()->SetTrack(TrackCache::Get().GetMasterTrack(), 0);
  }

 private:
  View* const view_;
};

class View::ParentRouteOtherTrackProperty : public ViewProperty {
 public:
  explicit ParentRouteOtherTrackProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~ParentRouteOtherTrackProperty() override = default;

 protected:
  void TriggerAction() override {
    View* parent_view = view_->GetParentView();
    if (parent_view == nullptr ||
        (parent_view->child_context_type_ != ChildContextType::kSends &&
         parent_view->child_context_type_ != ChildContextType::kReceives)) {
      return;
    }
    const TrackRoute* route = view_->route_properties_.GetRoute();
    if (route == nullptr) {
      return;
    }
    Track* track = parent_view->GetTrack();
    Track* other_track = route->other_track;
    const ChildContextType other_context_type =
        parent_view->child_context_type_ == ChildContextType::kSends
            ? ChildContextType::kReceives
            : ChildContextType::kSends;

    // Scroll so the route back to the original track is shown.
    absl::Span<const TrackRoute> other_routes =
        other_track->GetRoutes(GetChildRouteType(other_context_type));
    const int view_count = parent_view->GetChildViewCount();
    int start_index = 0;
    for (int i = 0; i < static_cast<int>(other_routes.size()); ++i) {
      if (other_routes[i].other_track == track) {
        start_index = std::max(0, i - view_count + 1);
        break;
      }
    }
    parent_view->SetChildContext(other_context_type);
    parent_view->SetTrack(other_track, start_index);
  }

 private:
  View* const view_;
};

class View::ChildRouteToggleProperty : public ViewProperty {
 public:
  explicit ChildRouteToggleProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kAction), view_(view) {}
  ~ChildRouteToggleProperty() override = default;

 protected:
  void TriggerAction() override { view_->ToggleChildRouteType(); }

 private:
  View* const view_;
};

class View::ChildRouteTypeNameProperty : public ViewProperty {
 public:
  explicit ChildRouteTypeNameProperty(View* view, std::string_view name)
      : ViewProperty(name, Type::kText), view_(view) {}
  ~ChildRouteTypeNameProperty() override = default;

  // Called by the view when its child context type changes.
  void OnChildContextChanged() { NotifyChanged(); }

 protected:
  std::string ReadString() const override {
    switch (view_->child_context_type_) {
      case ChildContextType::kSends:
        return "Send";
      case ChildContextType::kReceives:
        return "Recv";
      default:
        return "";
    }
  }

 private:
  View* const view_;
};

View::View(Scene* scene, View* parent_view, std::string_view name)
    : scene_(scene),
      parent_view_(parent_view),
      name_(name),
      track_properties_(&scene->GetTrackActions()),
      route_properties_(&scene->GetTrackActions()) {
  // Add properties for changing the child context index.
  properties_.emplace(kChildDec, std::make_unique<ChildIndexOffsetProperty>(
                                     this, kChildDec, -1));
  properties_.emplace(kChildInc, std::make_unique<ChildIndexOffsetProperty>(
                                     this, kChildInc, 1));
  properties_.emplace(kBankDec, std::make_unique<ChildIndexBankOffsetProperty>(
                                    this, kBankDec, -1));
  properties_.emplace(kBankInc, std::make_unique<ChildIndexBankOffsetProperty>(
                                    this, kBankInc, 1));
  // Add properties for navigating track contexts.
  properties_.emplace(
      kTrackParent, std::make_unique<TrackParentProperty>(this, kTrackParent));
  properties_.emplace(kTrackRoot,
                      std::make_unique<TrackRootProperty>(this, kTrackRoot));
  properties_.emplace(
      kParentTrackChild,
      std::make_unique<ParentTrackChildProperty>(this, kParentTrackChild));
  properties_.emplace(
      kParentTrackParent,
      std::make_unique<ParentTrackParentProperty>(this, kParentTrackParent));
  properties_.emplace(
      kParentTrackRoot,
      std::make_unique<ParentTrackRootProperty>(this, kParentTrackRoot));
  // Add properties for navigating and showing route contexts.
  properties_.emplace(kParentRouteOtherTrack,
                      std::make_unique<ParentRouteOtherTrackProperty>(
                          this, kParentRouteOtherTrack));
  properties_.emplace(
      kChildRouteToggle,
      std::make_unique<ChildRouteToggleProperty>(this, kChildRouteToggle));
  auto child_route_type_name_property =
      std::make_unique<ChildRouteTypeNameProperty>(this, kChildRouteTypeName);
  child_route_type_name_property_ = child_route_type_name_property.get();
  properties_.emplace(kChildRouteTypeName,
                      std::move(child_route_type_name_property));
}

bool View::ApplyConfig(const Config& config) {
  // The reference comes first, as the condition may be on its fields.
  if (const auto* subject = std::get_if<ReferenceSubject>(&config.subject)) {
    reference_ = scene_->GetTrackReference(subject->name);
    if (reference_ == nullptr) {
      LOG(ERROR) << "Failed to add view '" << name_ << "': reference '"
                 << subject->name << "' is not a track reference";
      return false;
    }
    writable_reference_ = scene_->GetWritableTrackReference(subject->name);
  }

  // The condition may refer to any of the view's properties.
  if (config.condition.has_value()) {
    ViewProperty* condition_property = GetProperty(config.condition->property);
    if (condition_property == nullptr) {
      LOG(ERROR) << "Failed to add view '" << name_ << "': condition property '"
                 << config.condition->property << "' not found";
      return false;
    }
    condition_ = std::make_unique<ViewCondition>(condition_property,
                                                 config.condition->value);
  }
  return true;
}

View::~View() {
  // The condition may refer to the view's own properties, so it must be
  // destroyed before them.
  condition_.reset();
}

void View::Enable() {
  if (enabled_) {
    return;
  }
  enabled_ = true;
  RefreshActive();
}

void View::Disable() {
  if (!enabled_) {
    return;
  }
  enabled_ = false;
  RefreshActive();
}

void View::RefreshActive() {
  bool parent_active = false;
  if (parent_view_ != nullptr) {
    parent_active = parent_view_->IsActive();
  } else if (scene_ != nullptr) {
    parent_active = scene_->IsActive();
  }

  // The condition decides whether the view is active, so it is watched whenever
  // the parent is active, whether or not the view is.
  if (condition_ != nullptr) {
    condition_->Watch(parent_active);
  }
  bool should_be_active = enabled_ && parent_active &&
                          (condition_ == nullptr || condition_->IsMet());
  if (active_ != should_be_active) {
    active_ = should_be_active;
    if (active_) {
      if (!NoticeReferenceChange()) {
        RefreshChildContext();
      }
    } else {
      ClearAnchor();
    }
  }

  // Always propagate to children and mappings, as their enabled state may
  // differ from this view's active state.
  for (auto& mapping : mappings_) {
    mapping->RefreshActive(active_);
  }
  for (auto& child_view : child_views_) {
    child_view->RefreshActive();
  }
}

View* View::AddChildView(std::string_view name, const Config& config) {
  if (child_views_by_name_.contains(name)) {
    LOG(ERROR) << "Failed to add view '" << name << "': the name is used";
    return nullptr;
  }
  auto child_view = absl::WrapUnique(new View(scene_, this, name));
  if (!child_view->ApplyConfig(config)) {
    return nullptr;
  }
  if (child_view->condition_ != nullptr) {
    scene_->AddConditionalView(child_view.get());
  }
  View* child_view_ptr = child_view.get();
  child_views_.push_back(std::move(child_view));
  child_views_by_name_[name] = child_view_ptr;
  return child_view_ptr;
}

View* View::GetChildView(std::string_view name) const {
  auto it = child_views_by_name_.find(name);
  return it != child_views_by_name_.end() ? it->second : nullptr;
}

void View::SetTrack(Track* track, int child_context_index) {
  if (reference_ != nullptr) {
    if (writable_reference_ == nullptr) {
      return;
    }
    writable_reference_->Set(track);
    if (!NoticeReferenceChange(child_context_index)) {
      SetChildContextIndex(child_context_index);
    }
    return;
  }

  if (track == nullptr) {
    track = TrackCache::Get().GetStubTrack();
  }
  if (GetTrack() == track) {
    SetChildContextIndex(child_context_index);
  } else {
    ClearAnchor();
    track_properties_.SetTrack(track);
    child_context_index_ = child_context_index;
    RefreshChildContext();
  }
}

Track* View::GetTrack() const {
  if (reference_ == nullptr) {
    return track_properties_.GetTrack();
  }
  Track* track = reference_->GetTrack();
  return track != nullptr ? track : TrackCache::Get().GetStubTrack();
}

bool View::NoticeReferenceChange(int child_context_index) {
  if (reference_ == nullptr || reference_->GetVersion() == reference_version_) {
    return false;
  }
  reference_version_ = reference_->GetVersion();
  ClearAnchor();
  child_context_index_ = child_context_index;
  RefreshChildContext();
  return true;
}

void View::SetRoute(Track* track, TrackRouteType type, int index) {
  if (route_properties_.GetTrack() != track ||
      route_properties_.GetType() != type ||
      route_properties_.GetIndex() != index) {
    ClearAnchor();
  }
  route_properties_.SetRoute(track, type, index);
  const TrackRoute* route = route_properties_.GetRoute();
  SetTrack(route != nullptr ? route->other_track : nullptr);
}

int View::GetMaxChildContextIndex() const {
  switch (child_context_type_) {
    case ChildContextType::kNone:
      return 0;
    case ChildContextType::kTrack:
      return std::max<int>(
          0, GetTrack()->GetChildTrackCount(scene_->GetTrackFilter()) -
                 GetChildViewCount());
    case ChildContextType::kSends:
    case ChildContextType::kReceives: {
      const int route_count = static_cast<int>(
          GetTrack()->GetRoutes(GetChildRouteType(child_context_type_)).size());
      return std::max(0, route_count - GetChildViewCount());
    }
  }
  return 0;
}

void View::SetChildContext(ChildContextType context_type, int context_index) {
  const bool type_changed = (child_context_type_ != context_type);
  child_context_type_ = context_type;
  child_context_index_ = context_index;
  if (type_changed) {
    child_route_type_name_property_->OnChildContextChanged();
  }
  RefreshChildContext();
}

bool View::ToggleChildRouteType() {
  ChildContextType other_context_type;
  switch (child_context_type_) {
    case ChildContextType::kSends:
      other_context_type = ChildContextType::kReceives;
      break;
    case ChildContextType::kReceives:
      other_context_type = ChildContextType::kSends;
      break;
    default:
      return false;
  }
  if (GetTrack()->GetRoutes(GetChildRouteType(other_context_type)).empty()) {
    return false;
  }
  SetChildContext(other_context_type);
  return true;
}

void View::SetChildContextIndex(int context_index) {
  if (child_context_index_ == context_index) {
    return;
  }
  child_context_index_ = context_index;
  RefreshChildContext();
}

void View::RefreshChildContext() {
  if (!active_) {
    return;
  }
  switch (child_context_type_) {
    case ChildContextType::kNone:
      return;
    case ChildContextType::kTrack:
      SetChildTracks();
      break;
    case ChildContextType::kSends:
    case ChildContextType::kReceives:
      SetChildRoutes();
      break;
  }
}

void View::SetAnchor(AnchorHold hold) {
  if (!hold.IsHeld() || !active_) {
    return;
  }
  anchor_hold_ = std::move(hold);
}

void View::ReleaseAnchor(const AnchorBase* anchor) {
  if (anchor_hold_.GetAnchor() == anchor) {
    ClearAnchor();
  }
}

void View::SetChildTracks() {
  CHECK(active_);
  CHECK(scene_ != nullptr);
  const TrackFilter filter = scene_->GetTrackFilter();

  // Walk the child tracks, skipping any that are not on the surface, and give
  // the child views the run of them that starts at the child context index.
  int skip_count = child_context_index_;
  auto child_view = child_views_.begin();
  for (Track* track : GetTrack()->GetChildTracks()) {
    if (!track->IsVisible(filter)) {
      continue;
    }
    if (skip_count > 0) {
      --skip_count;
      continue;
    }
    if (child_view == child_views_.end()) {
      break;
    }
    (*child_view)->SetTrack(track);
    (*child_view)->RefreshChildContext();
    ++child_view;
  }

  // Any views left over have no track to show.
  for (; child_view != child_views_.end(); ++child_view) {
    (*child_view)->SetTrack(TrackCache::Get().GetStubTrack());
    (*child_view)->RefreshChildContext();
  }
}

void View::SetChildRoutes() {
  CHECK(active_);
  CHECK(scene_ != nullptr);
  const TrackRouteType type = GetChildRouteType(child_context_type_);
  Track* track = GetTrack();

  // Views past the last route still refer to the route index they would show,
  // but have no route, and no track to show.
  int index = child_context_index_;
  for (auto& child_view : child_views_) {
    child_view->SetRoute(track, type, index);
    child_view->RefreshChildContext();
    ++index;
  }
}

ViewProperty* View::GetProperty(std::string_view name) const {
  if (name.starts_with(kTrackNamespace)) {
    if (reference_ != nullptr) {
      return reference_->GetField(name.substr(kTrackNamespace.size()));
    }
    return track_properties_.GetProperty(name);
  }
  if (name.starts_with(kRouteNamespace)) {
    return route_properties_.GetProperty(name);
  }
  if (name.starts_with(kViewNamespace)) {
    auto it = properties_.find(name);
    return it != properties_.end() ? it->second.get() : nullptr;
  }
  return scene_->GetProperty(name);
}

bool View::AddMapping(ViewMapping::TypeFlags type,
                      std::string_view property_name,
                      std::string_view control_name,
                      ViewMapping::Config config) {
  if (scene_ == nullptr) {
    return false;
  }
  ViewProperty* property = GetProperty(property_name);
  if (property == nullptr) {
    LOG(ERROR) << "Failed to add mapping for view '" << GetName()
               << "': property '" << property_name << "' not found";
    return false;
  }
  Control* control = scene_->GetControl(control_name);
  if (control == nullptr) {
    LOG(ERROR) << "Failed to add mapping for view '" << GetName()
               << "': control '" << control_name << "' not found";
    return false;
  }
  std::vector<ViewProperty*> mode_properties;
  for (const auto& override : config.write.mode_overrides) {
    ViewProperty* mode_property = GetProperty(override.property);
    if (mode_property == nullptr) {
      LOG(ERROR) << "Failed to add mapping for view '" << GetName()
                 << "': mode property '" << override.property << "' not found";
      return false;
    }
    mode_properties.push_back(mode_property);
  }
  ViewProperty* condition_property = nullptr;
  if (config.condition.has_value()) {
    condition_property = GetProperty(config.condition->property);
    if (condition_property == nullptr) {
      LOG(ERROR) << "Failed to add mapping for view '" << GetName()
                 << "': condition property '" << config.condition->property
                 << "' not found";
      return false;
    }
  }
  mappings_.push_back(absl::WrapUnique(
      new ViewMapping(this, type, property, control, std::move(config),
                      std::move(mode_properties), condition_property)));
  return true;
}

void View::SyncMappings() {
  if (!active_) {
    return;
  }

  // A bound view notices a change to its reference from outside the view (its
  // rules, or another view's mapping) here, or when it becomes active.
  NoticeReferenceChange();

  // First sync the track context if there is one, since some mappings may
  // depend on it. A reference refreshes its own track while its fields are
  // watched (see TrackReference::Update()).
  Track* track = GetTrack();
  if (reference_ == nullptr) {
    track->Refresh();
    track->RefreshMeter();
  }

  // REAPER doesn't reliably report route volume, pan, and mute changes, so they
  // are polled for the track whose routes are shown by the child views.
  if (child_context_type_ == ChildContextType::kSends ||
      child_context_type_ == ChildContextType::kReceives) {
    track->RefreshRoutes();
  }
  route_properties_.UpdateOtherTrack();

  // Now update all active mappings for this view. This will update the REAPER
  // state and hardware controls according to the current state of the view
  // properties.
  // Inactive mappings are synced too, so a change to their condition can
  // activate them.
  for (auto& mapping : mappings_) {
    mapping->Sync();
  }

  // Sync all child views.
  for (auto& child_view : child_views_) {
    child_view->SyncMappings();
  }
}

}  // namespace jpr
