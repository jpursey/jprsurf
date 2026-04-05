// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "gb/base/flags.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input_handle.h"
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
    // The type of physical input to use for reading the control. If not set,
    // the mapping will automatically choose the best input type based on the
    // property type and available inputs.
    std::optional<ControlInput::Type> input_type;

    // The modifier keys that must be active for this input to trigger. When
    // multiple mappings exist for the same control and input type, modifiers
    // are used for mutual exclusivity.
    Modifiers required_modifiers = 0;

    // The press behavior for this mapping. Only meaningful for press input
    // types and is ignored for value and delta inputs.
    InputConfig::PressBehavior press_behavior =
        InputConfig::PressBehavior::kNormal;

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

    // If true, then the property value will be set to property_max when the
    // control is pressed and property_min when the control is released. If the
    // underlying control does not have press/release capbility
    // (HasPressRelease() returns false), then this will never update the
    // property value (effectively disabling kReadControl capability).
    bool press_release = false;
  };

  // A mode override maps a property's value to an output mode. When the
  // property's current value matches an entry in the value-to-mode map, the
  // corresponding mode is used for the control output.
  struct ModeOverride {
    // The name of a property in the same view scope whose value determines
    // the output mode.
    std::string property;

    // Maps property values to output modes. The property's current value is
    // looked up in this map; the first match is used.
    std::vector<std::pair<ViewProperty::Value, int>> value_to_mode;
  };

  struct WriteConfig {
    // The default output mode index to use when writing to the control. This is
    // passed through to the control's Set* methods. Defaults to 0.
    //
    // If any mode override matches, its mode is used instead.
    int mode = 0;

    // Ordered list of mode overrides. Each override specifies a property and a
    // value-to-mode map. Overrides are checked in order; the first override
    // whose property value matches an entry in its map wins. If no override
    // matches, the default mode is used. The mapping will also update the
    // control whenever any override property changes.
    std::vector<ModeOverride> mode_overrides;
  };

  struct Config {
    ReadConfig read;
    WriteConfig write;
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

  using WriteSyncFunction = void(ViewProperty&, Control&, int mode);

  ViewMapping(View* view, TypeFlags type, ViewProperty* property,
              Control* control, Config config = {},
              std::vector<ViewProperty*> mode_properties = {});

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
  void InitReadTimelinePositionSyncFunction();
  void InitReadEnumeratedSyncFunction();

  void InitWriteControl();
  void InitWriteToggleSyncFunction();
  void InitWritePanSyncFunction();
  void InitWriteVolumeSyncFunction();
  void InitWriteNormalizedSyncFunction();
  void InitWriteTextSyncFunction();
  void InitWriteColorSyncFunction();
  void InitWriteTimelinePositionSyncFunction();
  void InitWriteEnumeratedSyncFunction();

  // Returns the current mode for writing to the control, resolved from the
  // mode override properties if configured, or the static mode otherwise.
  int ResolveMode() const;

  TypeFlags type_;
  View* view_;
  ViewProperty* property_;
  Control* control_;
  Config config_;
  std::vector<ViewProperty*> mode_properties_;
  absl::AnyInvocable<void(ViewProperty&, Control&, InputId)> read_control_;
  WriteSyncFunction* write_control_;
  InputConfig input_config_;
  ControlInputHandle input_handle_;
  bool enabled_ = true;
  bool active_ = false;
  bool reads_property_ = false;
  bool control_changed_ = false;
  bool property_changed_ = false;
};

}  // namespace jpr
