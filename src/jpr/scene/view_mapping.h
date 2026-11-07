// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "gb/base/flags.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output_handle.h"
#include "jpr/device/widget_shape.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_property.h"

namespace jpr {

class View;

// A view mapping represents a single mapping between a view property and a
// control.
//
// It is responsible for doing the actual synchronization between the view
// property and control when active, according to its type.
//
// A mapping drops a side it can't do, which GetType() then doesn't have: it
// reads the control only from an input GetReadInput() finds, so only into a
// property that can be written or triggered, and writes the control only to
// an output GetWriteOutput() finds, so only from a property that can be read.
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

  // A format a timeline position is written to text in, by name.
  struct TimelineFormat {
    std::string_view name;
    TimelineMode mode;
  };

  // The formats a timeline position is written to text in, apart from the
  // ruler's, which an empty name is (see WriteConfig::mode).
  static constexpr TimelineFormat kTimelineFormats[] = {
      {"beats", TimelineMode::kBeats},
      {"time", TimelineMode::kTime},
      {"frames", TimelineMode::kFrames},
      {"samples", TimelineMode::kSamples}};

  struct ReadConfig {
    friend bool operator==(const ReadConfig&, const ReadConfig&) = default;

    // The type of physical input to use for reading the control. If not set,
    // the mapping will automatically choose the best input type based on the
    // property type and available inputs.
    //
    // If set, the mapping reads only that input, and reads nothing if the
    // control doesn't have it, or the property type can't read it (such as a
    // timeline position from a press). A press_release mapping reads nothing
    // with any input type but a press.
    std::optional<ControlInput::Type> input_type;

    // The names of the modifiers, built in or added (such as "mod:shift", see
    // Scene::AddModifierProperty()), that must be active for this input to
    // trigger. When multiple mappings exist for the same control and input
    // type, modifiers are used for mutual exclusivity.
    std::vector<std::string> required_modifiers;

    // The press behavior for this mapping. Only meaningful for press input
    // types and is ignored for value and delta inputs.
    InputConfig::PressBehavior press_behavior =
        InputConfig::PressBehavior::kNormal;

    // If set, the minimum and maximum values for the property that the control
    // maps to. If not set, the default mapping will be used (e.g. for a pan
    // property, -1 maps to min and 1 maps to max). The values must match the
    // property's underlying type or they will be ignored, except that an
    // enumerated property's values may be the names of its values (see
    // ViewProperty::ResolveValue()).
    //
    // This is further affected by the input type of the control:
    //   - If the control has a Value input, then the property value will be
    //     linearly mapped to the control value according to the specified min
    //     and max.
    //   - If the control has a Delta input, then the property value will be
    //     clamped to the specified min and max after the delta is applied.
    //   - If the control has a Press input, then the property value will be
    //     toggled between the min and max values when the control is pressed:
    //       - A pan, volume, or normalized value at (or beyond) either end goes
    //         to the other end, and one between them goes to the nearer end,
    //         or the max from the middle. A pan whose range spans the center
    //         instead steps through min, the center, and max: from below the
    //         center to it, from below max to max, and from max (or beyond) to
    //         min.
    //       - Text and color need both min and max specified, and go to the
    //         max unless they are already at it.
    //       - An enumerated property is different (see press_toggles).
    //     Either way, if min and max are the same, a press sets that value.
    //
    // These values are ignored for action and toggle properties.
    std::optional<ViewProperty::Value> property_min;
    std::optional<ViewProperty::Value> property_max;

    // For an enumerated property with a Press input: without press_toggles, a
    // press steps to the next value in [property_min, property_max], wrapping
    // around (such as the timecode display's modes). With it, a press sets
    // property_max, or property_min if the value already is property_max.
    // The mapping gives the property the press to apply (see
    // ViewProperty::PressInt()), so a property whose writes run an action
    // runs it with the press. Other property types ignore it.
    bool press_toggles = false;

    // If true, then the property value will be set to property_max when the
    // control is pressed and property_min when the control is released. If the
    // underlying control does not have press/release capability
    // (HasPressRelease() returns false), or input_type is set to anything but a
    // press, then this will never update the property value (effectively
    // disabling kReadControl capability). This doesn't apply with touch, which
    // every control with input has.
    //
    // A mapping that stops reading while it holds a press (its view
    // deactivates, or it is disabled or destroyed) applies the release then,
    // so what it holds can't stick on. That is while views are being
    // refreshed, which is why a property's write may not change views (see
    // ViewProperty). A press that deactivates its own mapping is released at
    // once, so that is a configuration error.
    bool press_release = false;

    // If true, the mapping reads whether a hand is on the control (see
    // InputConfig::touch) as a press with release, whatever inputs the control
    // has, and input_type is ignored. With press_release, the property is
    // property_max while the control is touched, and property_min otherwise:
    // a track's volume_touched while its fader is held, for instance.
    bool touch = false;
  };

  // A mode override maps a property's value to an output mode. When the
  // property's current value matches an entry in the value-to-mode map (see
  // ViewProperty::Equals()), the corresponding mode is used for the control
  // output.
  struct ModeOverride {
    friend bool operator==(const ModeOverride&, const ModeOverride&) = default;

    // The name of a property in the same view scope whose value determines
    // the output mode.
    std::string property;

    // Maps property values to output modes, by name (see WriteConfig::mode).
    // The property's current value is looked up in this map; the first match
    // is used. An enumerated property's values may be the names of its values
    // (see ViewProperty::ResolveValue()).
    std::vector<std::pair<ViewProperty::Value, std::string>> value_to_mode;
  };

  struct WriteConfig {
    friend bool operator==(const WriteConfig&, const WriteConfig&) = default;

    // The name of the output mode to write to the control in: one of the
    // control's mode names in its shape (such as "blink"), or empty for its
    // first mode. A timeline position written to text names its format
    // instead: the name of one of kTimelineFormats, or empty for the ruler's.
    //
    // If any mode override matches, its mode is used instead.
    std::string mode;

    // Ordered list of mode overrides. Each override specifies a property and a
    // value-to-mode map. Overrides are checked in order; the first override
    // whose property value matches an entry in its map wins. If no override
    // matches, the default mode is used. The mapping will also update the
    // control whenever any override property changes.
    std::vector<ModeOverride> mode_overrides;
  };

  struct Config {
    friend bool operator==(const Config&, const Config&) = default;

    ReadConfig read;
    WriteConfig write;

    // If set, the mapping only acts while this condition is met. This allows
    // several mappings to share a control, with only one of them acting
    // depending on some state.
    //
    // A write mapping writes to the control, and holds its output, only while
    // the condition is met. A read mapping keeps its input while its view is
    // active, and checks the condition when the input arrives. While the
    // condition isn't met the input does nothing, but it still counts in the
    // control's modifier sets and press groups, so it doesn't fall through to
    // another mapping. A press_release mapping still applies the release of a
    // press it applied, so what it holds can't stick on.
    std::optional<ViewCondition::Config> condition;
  };

  // Returns true if a mapping can read a control into a property with the
  // access, which is one that can be written or triggered, or write a control
  // from one, which is one that can be read.
  static bool CanReadControlInto(PropertyAccessFlags access);
  static bool CanWriteControlFrom(PropertyAccessFlags access);

  // Returns the input a mapping reads a control with the inputs from, into a
  // property of the type and access, or nullopt if it reads nothing: it can't
  // read into the property (see CanReadControlInto()), or none of the inputs
  // suits the type and the config (see ReadConfig). `has_press_release` is
  // whether the control's press input has a release (see
  // Control::HasPressRelease()).
  static std::optional<ControlInput::Type> GetReadInput(
      ViewProperty::Type type, PropertyAccessFlags access,
      Control::Inputs inputs, bool has_press_release, const ReadConfig& config);

  // Returns the output a mapping writes a property of the type and access to,
  // on a control with the outputs, or nullopt if it writes nothing: it can't
  // write from the property (see CanWriteControlFrom()), or none of the
  // outputs suits the type.
  static std::optional<ControlOutput::Type> GetWriteOutput(
      ViewProperty::Type type, PropertyAccessFlags access,
      Control::Outputs outputs);

  // Returns the number of the mode that a mapping of a property of the type to
  // a control of the shape names (see WriteConfig::mode), or nullopt if there
  // is none.
  static std::optional<int> FindMode(ViewProperty::Type type,
                                     const WidgetShape& shape,
                                     std::string_view name);

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
  // and control. A mapping is active if it is enabled, its parent view is
  // active, and its condition (if any) is met. A read mapping keeps its input
  // while its condition isn't met (see Config::condition).
  bool IsActive() const { return active_; }

  // Synchronizes the view property and control according to the type of the
  // mapping. This should be called whenever the parent view is active, even if
  // the mapping is not, so a change to its condition can activate it.
  void Sync();

  // Read or write to the control. These are called by the Scene when the
  // control or property changes, respectively,
  void ReadControl();
  void WriteControl();

 private:
  friend class View;

  using WriteSyncFunction = void(ViewProperty&, Control&, int mode);

  // A mode override, with its property and the numbers of its modes.
  struct PropertyModes {
    ViewProperty* property = nullptr;
    std::vector<std::pair<ViewProperty::Value, int>> value_to_mode;
  };

  // The modifiers are the flags of the read config's required modifiers, and
  // the modes are the numbers of the config's write modes (see FindMode()).
  ViewMapping(View* view, TypeFlags type, ViewProperty* property,
              Control* control, ReadConfig read_config,
              Modifiers required_modifiers, int mode,
              std::vector<PropertyModes> mode_overrides,
              std::unique_ptr<ViewCondition> condition);

  // Refreshes the active state of this mapping, and whether it has its input,
  // based on whether it is enabled, whether its parent view is active, and
  // whether its condition is met.
  void RefreshActive(bool parent_active);

  // Each reads the input GetReadInput() chose, or writes the output
  // GetWriteOutput() chose.
  void InitReadControl(Modifiers required_modifiers);
  void InitReadActionSyncFunction();
  void InitReadToggleSyncFunction(ControlInput::Type input);
  void InitReadPanSyncFunction(ControlInput::Type input);
  void InitReadVolumeSyncFunction(ControlInput::Type input);
  void InitReadNormalizedSyncFunction(ControlInput::Type input);
  void InitReadTextSyncFunction(ControlInput::Type input);
  void InitReadColorSyncFunction(ControlInput::Type input);
  void InitReadTimelinePositionSyncFunction();
  void InitReadEnumeratedSyncFunction(ControlInput::Type input);

  void InitWriteControl();
  void InitWriteToggleSyncFunction(ControlOutput::Type output);
  void InitWritePanSyncFunction(ControlOutput::Type output);
  void InitWriteVolumeSyncFunction(ControlOutput::Type output);
  void InitWriteNormalizedSyncFunction(ControlOutput::Type output);
  void InitWriteTextSyncFunction();
  void InitWriteColorSyncFunction(ControlOutput::Type output);
  void InitWriteTimelinePositionSyncFunction();
  void InitWriteEnumeratedSyncFunction(ControlOutput::Type output);

  // Returns the current mode for writing to the control, resolved from the
  // mode overrides if configured, or the mode otherwise.
  int ResolveMode() const;

  TypeFlags type_;
  View* view_;
  ViewProperty* property_;
  Control* control_;
  ReadConfig read_config_;
  int mode_;
  std::vector<PropertyModes> mode_overrides_;
  std::unique_ptr<ViewCondition> condition_;  // Null if there is none.
  absl::AnyInvocable<void(ViewProperty&, Control&, InputId)> read_control_;
  WriteSyncFunction* write_control_;
  InputConfig input_config_;
  ControlInputHandle input_handle_;
  ControlOutputHandle output_handle_;  // Registered while active and writing.
  bool enabled_ = true;
  bool active_ = false;

  // Whether the parent view is active. The condition is watched whenever it is.
  bool parent_active_ = false;
  bool reads_property_ = false;
  bool control_changed_ = false;
  bool property_changed_ = false;

  // Whether a press_release mapping applied a press, and so must apply its
  // release, when the control is released or the mapping stops reading.
  bool holding_ = false;
};

}  // namespace jpr
