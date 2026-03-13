// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "jpr/scene/scene.h"

namespace jpr {

void View::Activate() {
  // A view cannot be active if its parent view is not active, so we check for
  // that here and simply return if the parent view is not active.
  if (active_ || (parent_view_ != nullptr && !parent_view_->IsActive())) {
    return;
  }
  active_ = true;
  RefreshChildContext();

  // Activate all mappings for this view and add them to the scene's active
  for (auto& mapping : mappings_) {
    mapping->Activate();
  }
}

void View::Deactivate() {
  if (!active_) {
    return;
  }
  active_ = false;

  // Deactivate all child views. This will also remove their mappings from the
  // scene's active mappings.
  for (auto& child_view : child_views_) {
    child_view->Deactivate();
  }

  // Deactivate all mappings for this view and remove them from the scene's
  // active
  for (auto& mapping : mappings_) {
    mapping->Deactivate();
  }
}

View* View::AddChildView(std::string_view name) {
  if (child_views_by_name_.contains(name)) {
    return nullptr;
  }
  child_views_.push_back(absl::WrapUnique(new View(scene_, this, name)));
  View* child_view = child_views_.back().get();
  child_views_by_name_[name] = child_view;
  return child_view;
}

View* View::GetChildView(std::string_view name) const {
  auto it = child_views_by_name_.find(name);
  return it != child_views_by_name_.end() ? it->second : nullptr;
}

void View::SetContext(Context context) {
  context_ = std::move(context);
  RefreshChildContext();
}

void View::SetTrackContext(Track* track) {
  if (track == nullptr) {
    track = TrackCache::Get().GetStubTrack();
  }
  SetContext(std::make_unique<TrackProperties>(track));
}

void View::SetChildContext(ContextType context_type, int context_index) {
  child_context_type_ = context_type;
  child_context_index_ = context_index;
  RefreshChildContext();
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
    case ContextType::kNone:
      return;
    case ContextType::kTrack:
      SetChildTracks();
      break;
  }
}

void View::SetChildTracks() {
  CHECK(active_);
  CHECK(scene_ != nullptr);
  absl::Span<Track* const> child_tracks;
  if (GetContextType() == ContextType::kNone) {
    child_tracks = TrackCache::Get().GetTopLevelTracks();
  } else if (GetContextType() == ContextType::kTrack) {
    auto& parent_track_properties =
        std::get<std::unique_ptr<TrackProperties>>(context_);
    DCHECK(parent_track_properties != nullptr);
    child_tracks = parent_track_properties->GetTrack()->GetChildTracks();
  }

  int index = child_context_index_;
  for (auto& child_view : child_views_) {
    if (child_view->GetContextType() != ContextType::kTrack) {
      continue;
    }
    auto& child_track_properties =
        std::get<std::unique_ptr<TrackProperties>>(child_view->context_);
    if (index < child_tracks.size()) {
      child_track_properties->SetTrack(child_tracks[index]);
    } else {
      child_track_properties->SetTrack(TrackCache::Get().GetStubTrack());
    }
    child_view->RefreshChildContext();
    ++index;
  }
}

ViewProperty* View::GetContextProperty(std::string_view name) const {
  switch (GetContextType()) {
    case ContextType::kNone:
      return nullptr;
    case ContextType::kTrack: {
      auto& track_properties =
          std::get<std::unique_ptr<TrackProperties>>(context_);
      DCHECK(track_properties != nullptr);
      return track_properties->GetProperty(name);
    } break;
  }
  return nullptr;
}

bool View::AddMapping(ViewMapping::TypeFlags type,
                      std::string_view property_name,
                      std::string_view control_name) {
  if (scene_ == nullptr) {
    return false;
  }
  ViewProperty* property = GetContextProperty(property_name);
  if (property == nullptr) {
    property = scene_->GetProperty(property_name);
  }
  if (property == nullptr) {
    return false;
  }
  Control* control = scene_->GetControl(control_name);
  if (control == nullptr) {
    return false;
  }
  mappings_.push_back(
      absl::WrapUnique(new ViewMapping(type, property, control)));
  return true;
}

void View::SyncMappings() {
  if (!active_) {
    return;
  }

  // First sync the track context if there is one, since some mappings may
  // depend on it.
  if (GetContextType() == ContextType::kTrack) {
    auto& track_properties =
        std::get<std::unique_ptr<TrackProperties>>(context_);
    DCHECK(track_properties != nullptr);
    track_properties->GetTrack()->Refresh();
  }

  // Now update all active mappings for this view. This will update the REAPER
  // state and hardware controls according to the current state of the view
  // properties.
  for (auto& mapping : mappings_) {
    if (mapping->IsActive()) {
      mapping->Sync();
    }
  }

  // Sync all child views.
  for (auto& child_view : child_views_) {
    child_view->SyncMappings();
  }
}

}  // namespace jpr
