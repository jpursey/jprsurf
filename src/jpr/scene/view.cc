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
  if (active_) {
    return;
  }
  active_ = true;
  RefreshChildContext();
}

void View::Deactivate() { active_ = false; }

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
    ++index;
  }
}

bool View::AddMapping(ViewMapping::Type type, std::string_view property_name,
                      std::string_view control_name) {
  if (scene_ == nullptr) {
    return false;
  }
  ViewProperty* property = scene_->GetProperty(property_name);
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

}  // namespace jpr
