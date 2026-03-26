// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/scene/view_property.h"

namespace jpr {

class Scene;

// Base class for all properties that are part of the scene and track state that
// may change.
class SceneStateProperty : public ViewProperty {
 public:
  ~SceneStateProperty() override = default;

  // Derived classes must override this to update the state of the property, and
  // if it changed call NotifyChanged().
  virtual void UpdateState() = 0;

 protected:
  SceneStateProperty(Scene* scene, std::string_view name, Type type)
      : ViewProperty(name, type), scene_(scene) {}

  // Regsiters the property with the scene so it can be updated.
  void OnRegistered() override;
  void OnUnregistered() override;

 private:
  Scene* scene_;
};

}  // namespace jpr
