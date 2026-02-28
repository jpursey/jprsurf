// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "jpr/scene/view_mapping.h"

namespace jpr {

class Scene;

// Base class for all views.
//
// A view is a potentially hierarchical mapping of other views rooted in a
// scene, and mappings between the REAPER state and one or more hardware
// controls.
class View final {
 public:
  View(const View&) = delete;
  View& operator=(const View&) = delete;
  ~View() = default;

  // Name of the view. Views can be looked up by name relative to their parent
  // view.
  std::string_view GetName() const { return name_; }

  // Current scene, null if not added to a scene yet
  Scene* GetScene() const { return scene_; }

  // Parent view, null if this is a root view or it is not yet added to another
  // view.
  View* GetParentView() const { return parent_view_; }

  // Adds a child view to thhis view. This will return null if a view with the
  // same name already exists.
  View* AddChildView(std::string_view name);

  int GetChildViewCount() const { return child_views_.size(); }
  View* GetChildViewAt(int index) const { return child_views_[index].get(); }
  View* GetChildView(std::string_view name) const;

  // Adds a mapping to this view.
  //
  // This will return false if the mapping is invalid, for instance if the
  // named control or property doesn't exist in the scene.
  bool AddMapping(ViewMapping::Type type, std::string_view property_name,
                  std::string_view control_name);

  // Activation and deactivation. An active view will update the REAPER state
  // and hardware controls according to its mappings.
  bool IsActive() const { return active_; }
  void Activate();
  void Deactivate();

 private:
  friend class Scene;

  View(Scene* scene, View* parent_view, std::string_view name)
      : scene_(scene), parent_view_(parent_view), name_(name) {}

  // Constructed state
  Scene* scene_;
  View* parent_view_;
  std::string name_;

  // View hierarchy
  std::vector<std::unique_ptr<View>> child_views_;
  absl::flat_hash_map<std::string, View*> child_views_by_name_;

  // Mappings for this view.
  std::vector<std::unique_ptr<ViewMapping>> mappings_;

  // Current state
  bool active_ = false;
};

}  // namespace jpr