// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>

#include "absl/functional/any_invocable.h"
#include "gb/base/flags.h"
#include "jpr/device/control.h"
#include "jpr/scene/view_property.h"

namespace jpr {

class View;

// A view mapping represents a single mapping between a view property and a
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

  struct ReadConfig {
    // If set, the minimum and maximum values for the property that the control
    // maps to. If not set, the default mapping will be used (e.g. for a pan
    // property, -1 maps to min and 1 maps to max). The values must match the
    // property's underlying type or they will be ignored.
    //
    // This is further affected by the input type of the control:
    //   - If the control has a Value input, then the property value will be
    //     linearly mapped to the control value according to the specified min
    //     and max.
    //   - If the control has a Delta input, then the property value will be
    //     clamped to the specified min and max after the delta is applied.
    //   - If the control has a Press input, then the property value will be
    //     toggled between the min and max values when the control is pressed if
    //     both are specified. If the value is not currently at either the min
    //     or max value, then it will be toggled to the max value if it is
    //     closer to the max value, or the min value if it is closer to the min
    //     value.
    //
    // These values are ignored for action and toggle properties.
    std::optional<ViewProperty::Value> property_min;
    std::optional<ViewProperty::Value> property_max;
  };

  ViewMapping(const ViewMapping&) = delete;
  ViewMapping& operator=(const ViewMapping&) = delete;
  ~ViewMapping();

  TypeFlags GetType() const { return type_; }
  ViewProperty* GetProperty() const { return property_; }
  Control* GetControl() const { return control_; }

  // Enable and disable this mapping. A mapping starts enabled by default.
  bool IsEnabled() const { return enabled_; }
  void Enable();
  void Disable();

  // Returns true if this mapping is actively synchronizing the view property
  // and control. A mapping is active if it is both enabled and its parent view
  // is active.
  bool IsActive() const { return active_; }

  // Synchronizes the view property and control according to the type of the
  // mapping. This should be called whenever the mapping is active.
  void Sync();

  // Read or write to the control. These are called by the Scene when the
  // control or property changes, respectively,
  void ReadControl();
  void WriteControl();

 private:
  friend class View;

  using WriteSyncFunction = void(ViewProperty&, Control&);

  ViewMapping(View* view, TypeFlags type, ViewProperty* property,
    Control* control, ReadConfig read_config = {});

  // Refreshes the active state of this mapping based on whether it is enabled
  // and whether its parent view is active.
  void RefreshActive(bool parent_active);

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
  View* view_;
  ViewProperty* property_;
  Control* control_;
  ReadConfig read_config_;
  absl::AnyInvocable<void(ViewProperty&, Control&)> read_control_;
  WriteSyncFunction* write_control_;
  std::optional<ControlInput::Type> input_type_;
  bool enabled_ = true;
  bool active_ = false;
  bool control_changed_ = false;
  bool property_changed_ = false;
};

}  // namespace jpr
