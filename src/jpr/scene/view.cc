// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view.h"

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "jpr/scene/scene.h"

namespace jpr {

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

bool View::AddMapping(ViewMapping::Type type, std::string_view property_name,
                      std::string_view control_name) {
  if (scene_ == nullptr) {
    return false;
  }
  ViewProperty* property = scene_->GetProperty(property_name);
  if (property == nullptr) {
    return false;
  }
  ViewControl* control = scene_->GetControl(control_name);
  if (control == nullptr) {
    return false;
  }
  mappings_.push_back(
      absl::WrapUnique(new ViewMapping(type, property, control)));
  return true;
}

void View::Activate() { active_ = true; }

void View::Deactivate() { active_ = false; }

}  // namespace jpr
