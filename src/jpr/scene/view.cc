// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include <utility>
#include <variant>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view_list.h"

namespace jpr {

View::View(Scene* scene, View* parent_view, std::string_view name)
    : scene_(scene), parent_view_(parent_view), name_(name) {}

bool View::ApplyConfig(const Config& config) {
  // The subject comes first, as the list and the condition depend on it.
  TrackReference* writable_reference = nullptr;
  if (const auto* subject = std::get_if<ReferenceSubject>(&config.subject)) {
    subject_ = scene_->GetReference(subject->name);
    if (subject_ == nullptr) {
      LOG(ERROR) << "Failed to add view '" << name_ << "': reference '"
                 << subject->name << "' not found";
      return false;
    }
    writable_reference = scene_->GetWritableTrackReference(subject->name);
  } else if (std::holds_alternative<ListItemSubject>(config.subject)) {
    if (parent_view_->list_ == nullptr) {
      LOG(ERROR) << "Failed to add view '" << name_
                 << "': a list item's parent has no list";
      return false;
    }
    item_reference_ = parent_view_->list_->CreateItemReference();
    subject_ = item_reference_.get();
  } else {
    subject_ = parent_view_->subject_;
  }

  if (config.list.has_value()) {
    if (subject_ == nullptr || subject_->GetKind() != SubjectKind::kTrack) {
      LOG(ERROR) << "Failed to add view '" << name_
                 << "': a list needs a track subject";
      return false;
    }
    list_ = ViewList::Create(this, *config.list, writable_reference);
    if (list_ == nullptr) {
      return false;
    }
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
      UpdateSubject();
      if (list_ != nullptr) {
        list_->OnActivated();
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
  if (child_view->item_reference_ != nullptr) {
    list_->AddItem(child_view.get());
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

Track* View::GetTrack() const {
  Track* track = nullptr;
  if (subject_ != nullptr && subject_->GetKind() == SubjectKind::kTrack) {
    track = static_cast<const TrackReference*>(subject_)->GetTrack();
  }
  return track != nullptr ? track : TrackCache::Get().GetStubTrack();
}

void View::UpdateSubject() {
  if (subject_ != nullptr && subject_->GetVersion() != subject_version_) {
    subject_version_ = subject_->GetVersion();
    ClearAnchor();
  }
  if (list_ != nullptr) {
    list_->Update();
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

ViewProperty* View::GetSubjectField(SubjectKind kind,
                                    std::string_view name) const {
  if (subject_ == nullptr || subject_->GetKind() != kind) {
    return nullptr;
  }
  return subject_->GetField(name);
}

ViewProperty* View::GetProperty(std::string_view name) const {
  if (name.starts_with(kTrackNamespace)) {
    return GetSubjectField(SubjectKind::kTrack,
                           name.substr(kTrackNamespace.size()));
  }
  if (name.starts_with(kRouteNamespace)) {
    return GetSubjectField(SubjectKind::kRoute,
                           name.substr(kRouteNamespace.size()));
  }
  if (name.starts_with(kViewNamespace)) {
    auto it = properties_.find(name);
    return it != properties_.end() ? it->second.get() : nullptr;
  }
  if (name.starts_with(kUserNamespace)) {
    for (const View* view = this; view != nullptr; view = view->parent_view_) {
      if (auto it = view->user_properties_.find(name);
          it != view->user_properties_.end()) {
        return it->second.get();
      }
    }
  }
  return scene_->GetProperty(name);
}

ViewProperty* View::DoAddUserProperty(std::unique_ptr<ViewProperty> property) {
  if (property == nullptr) {
    return nullptr;
  }
  const std::string_view name = property->GetName();
  if (!IsNewUserName(name)) {
    LOG(ERROR) << "Failed to add property '" << name << "' to view '" << name_
               << "': the name is not a new user: name, or is already used by "
                  "the view, an ancestor, a descendant, or the scene";
    return nullptr;
  }
  ViewProperty* added_property = property.get();
  added_property->SetView(this);
  user_properties_.emplace(name, std::move(property));
  return added_property;
}

bool View::IsNewUserName(std::string_view name) const {
  // GetProperty() finds a property added to this view, an ancestor, or the
  // scene. The scene's check covers what it doesn't: the namespace, a '.' (a
  // reference's field), and the names of references, which aren't properties.
  return scene_->IsUnusedUserName(name) && GetProperty(name) == nullptr &&
         !HasUserPropertyInSubtree(name);
}

bool View::HasUserPropertyInSubtree(std::string_view name) const {
  if (user_properties_.contains(name)) {
    return true;
  }
  for (const auto& child_view : child_views_) {
    if (child_view->HasUserPropertyInSubtree(name)) {
      return true;
    }
  }
  return false;
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

  // A change to the subject or the track list from outside the view (a
  // reference's rules, another view's mapping, or REAPER) is noticed here, or
  // when the view becomes active. Then a list item updates the reference it
  // owns, and a list polls what it shows, as mappings depend on them.
  UpdateSubject();
  if (item_reference_ != nullptr) {
    item_reference_->Update();
  }
  if (list_ != nullptr) {
    list_->Sync();
  }

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
