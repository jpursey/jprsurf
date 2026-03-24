// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "jpr/scene/scene.h"

namespace jpr {

// A view property that changes the child context index by a specified offset
// when triggered. This is used for the child_inc, child_dec, child_bank_inc,
// and child_bank_dec properties.
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
  void TriggerAction() override {
    if (view_->GetContextType() != View::ContextType::kTrack) {
      return;
    }
    auto& track_properties =
        std::get<std::unique_ptr<TrackProperties>>(view_->context_);
    DCHECK(track_properties != nullptr);
    Track* parent_track = track_properties->GetTrack()->GetParentTrack();
    if (parent_track != nullptr) {
      view_->SetTrackContext(parent_track, 0);
    } else {
      view_->ClearContext(0);
    }
  }

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
    if (view_->GetContextType() != View::ContextType::kTrack) {
      return;
    }
    view_->ClearContext(0);
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
    if (view_->GetContextType() != View::ContextType::kTrack ||
        view_->GetParentView() == nullptr ||
        view_->GetParentView()->GetChildContextType() !=
            View::ContextType::kTrack) {
      return;
    }
    auto& track_properties =
        std::get<std::unique_ptr<TrackProperties>>(view_->context_);
    DCHECK(track_properties != nullptr);
    Track* track = track_properties->GetTrack();
    if (track->GetChildTrackCount() == 0) {
      return;
    }
    view_->GetParentView()->SetTrackContext(track_properties->GetTrack(), 0);
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
    if (view_->GetContextType() != View::ContextType::kTrack ||
        view_->GetParentView() == nullptr ||
        view_->GetParentView()->GetChildContextType() !=
            View::ContextType::kTrack) {
      return;
    }
    auto& track_properties =
        std::get<std::unique_ptr<TrackProperties>>(view_->context_);
    DCHECK(track_properties != nullptr);
    Track* parent_track = track_properties->GetTrack()->GetParentTrack();
    if (parent_track == nullptr) {
      return;
    }
    Track* grandparent_track = parent_track->GetParentTrack();
    int track_count = (grandparent_track != nullptr
                           ? grandparent_track->GetChildTrackCount()
                           : TrackCache::Get().GetTopLevelTrackCount());
    int view_count = view_->GetParentView()->GetChildContextCount();
    int start_index = std::clamp(parent_track->GetIndex() - view_count / 2, 0,
                                 std::max(0, track_count - view_count));
    if (grandparent_track != nullptr) {
      view_->GetParentView()->SetTrackContext(grandparent_track, start_index);
    } else {
      view_->GetParentView()->ClearContext(start_index);
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
    if (view_->GetContextType() != View::ContextType::kTrack ||
        view_->GetParentView() == nullptr ||
        view_->GetParentView()->GetChildContextType() !=
            View::ContextType::kTrack) {
      return;
    }
    view_->GetParentView()->ClearContext(0);
  }

 private:
  View* const view_;
};

View::View(Scene* scene, View* parent_view, std::string_view name)
    : scene_(scene), parent_view_(parent_view), name_(name) {
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
  bool should_be_active = enabled_ && parent_active;
  if (active_ != should_be_active) {
    active_ = should_be_active;
    if (active_) {
      RefreshChildContext();
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

void View::SetContext(Context context, int child_context_index) {
  context_ = std::move(context);
  child_context_index_ = child_context_index;
  RefreshChildContext();
}

void View::SetTrackContext(Track* track, int child_context_index) {
  if (track == nullptr) {
    track = TrackCache::Get().GetStubTrack();
  }
  if (GetTrackContext() == track) {
    SetChildContextIndex(child_context_index);
  } else {
    SetContext(std::make_unique<TrackProperties>(track), child_context_index);
  }
}

Track* View::GetTrackContext() const {
  if (GetContextType() != ContextType::kTrack) {
    return nullptr;
  }
  auto& track_properties = std::get<std::unique_ptr<TrackProperties>>(context_);
  DCHECK(track_properties != nullptr);
  return track_properties->GetTrack();
}

void View::ClearContext(int child_context_index) {
  SetContext(std::monostate(), child_context_index);
}

int View::GetChildContextCount() const {
  switch (child_context_type_) {
    case ContextType::kNone:
      return 0;
    case ContextType::kTrack: {
      int count = 0;
      for (auto& child_view : child_views_) {
        if (child_view->GetContextType() == ContextType::kTrack) {
          ++count;
        }
      }
      return count;
    }
  }
  return 0;
}

int View::GetMaxChildContextIndex() const {
  switch (child_context_type_) {
    case ContextType::kNone:
      return 0;
    case ContextType::kTrack:
      if (GetContextType() == ContextType::kNone) {
        return TrackCache::Get().GetTopLevelTrackCount() -
               GetChildContextCount();
      } else if (GetContextType() == ContextType::kTrack) {
        auto& track_properties =
            std::get<std::unique_ptr<TrackProperties>>(context_);
        DCHECK(track_properties != nullptr);
        return track_properties->GetTrack()->GetChildTracks().size() -
               GetChildContextCount();
      }
      break;
  }
  return 0;
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

ViewProperty* View::GetProperty(std::string_view name) const {
  switch (GetContextType()) {
    case ContextType::kNone:
      break;
    case ContextType::kTrack: {
      auto& track_properties =
          std::get<std::unique_ptr<TrackProperties>>(context_);
      DCHECK(track_properties != nullptr);
      if (ViewProperty* property = track_properties->GetProperty(name);
          property != nullptr) {
        return property;
      }
    } break;
  }
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  return nullptr;
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
    property = scene_->GetProperty(property_name);
  }
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
  mappings_.push_back(absl::WrapUnique(
      new ViewMapping(this, type, property, control, std::move(config))));
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
