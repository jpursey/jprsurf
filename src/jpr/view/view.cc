// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/view/view.h"

#include "absl/log/check.h"
#include "absl/memory/memory.h"

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

void View::Activate() { active_ = true; }

void View::Deactivate() { active_ = false; }

}  // namespace jpr
