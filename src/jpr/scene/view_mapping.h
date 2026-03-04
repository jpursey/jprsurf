// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "gb/base/flags.h"
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
    // A mapping that updates the view property when the control changes.
    kReadControl,

    // A mapping that updates the control when the view property changes.
    kWriteControl,
  };
  using TypeFlags = gb::Flags<Type>;
  static constexpr TypeFlags kReadControl = Type::kReadControl;
  static constexpr TypeFlags kWriteControl = Type::kWriteControl;
  static constexpr TypeFlags kReadWriteControl = {Type::kReadControl,
                                                  Type::kWriteControl};

  ViewMapping(TypeFlags type, ViewProperty* property, ViewControl* control);
  ViewMapping(const ViewMapping&) = delete;
  ViewMapping& operator=(const ViewMapping&) = delete;
  ~ViewMapping();

  TypeFlags GetType() const { return type_; }
  ViewProperty* GetProperty() const { return property_; }
  ViewControl* GetControl() const { return control_; }

  // Activation and deactivation. An active mapping will update the view
  // property and/or control according to its type when either changes.
  bool IsActive() const { return active_; }
  void Activate();
  void Deactivate();

  // Synchronizes the view property and control according to the type of the
  // mapping. This should be called whenever the mapping is active.
  void Sync();

  // Read or write to the control. These are called by the Scene when the
  // control or property changes, respectively,
  void ReadControl();
  void WriteControl();

 private:
  using SyncFunction = void(ViewProperty&, ViewControl&);

  void InitReadControl();
  void InitReadActionSyncFunction();
  void InitReadToggleSyncFunction();
  void InitReadPanSyncFunction();
  void InitReadVolumeSyncFunction();
  void InitReadNormalizedSyncFunction();
  void InitReadTextSyncFunction();
  void InitReadColorSyncFunction();

  void InitWriteControl();
  void InitWriteToggleSyncFunction();
  void InitWritePanSyncFunction();
  void InitWriteVolumeSyncFunction();
  void InitWriteNormalizedSyncFunction();
  void InitWriteTextSyncFunction();
  void InitWriteColorSyncFunction();

  TypeFlags type_;
  ViewProperty* property_;
  ViewControl* control_;
  SyncFunction* read_control_;
  SyncFunction* write_control_;
  bool active_ = false;
  bool control_changed_ = false;
  bool property_changed_ = false;
};

}  // namespace jpr
