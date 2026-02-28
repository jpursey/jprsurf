// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/scene/view_control.h"
#include "jpr/scene/view_property.h"

namespace jpr {

// A view mapping represents a single mapping between a view property and a view
// control.
//
// It is responsible for doing the actual synchronization between the view
// property and control when active, according to its type.
class ViewMapping final {
 public:
  enum class Type {
    // A mapping that only updates the view property when the control changes.
    kReadControl,

    // A mapping that only updates the control when the view property changes.
    kWriteControl,

    // A mapping that updates both the view property and control when either
    // changes.
    kReadWrite,
  };

  ViewMapping(Type type, ViewProperty* property, ViewControl* control);
  ViewMapping(const ViewMapping&) = delete;
  ViewMapping& operator=(const ViewMapping&) = delete;
  ~ViewMapping();

  Type GetType() const { return type_; }
  ViewProperty* GetProperty() const { return property_; }
  ViewControl* GetControl() const { return control_; }

  // Activation and deactivation. An active mapping will update the view
  // property and/or control according to its type when either changes.
  bool IsActive() const { return active_; }
  void Activate();
  void Deactivate();

  // Read or write to the control. These are called by the Scene when the
  // control or property changes, respectively,
  void ReadControl();
  void WriteControl();

 private:
  Type type_;
  ViewProperty* property_;
  ViewControl* control_;
  bool active_ = false;
};

}  // namespace jpr
