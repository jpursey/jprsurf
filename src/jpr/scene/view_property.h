// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "jpr/common/color.h"
#include "jpr/common/numbered_name.h"
#include "jpr/common/prefixed_name.h"
#include "jpr/common/timeline.h"

namespace jpr {

//==============================================================================
// Property namespaces
//==============================================================================

// Every property name is a namespace and a name within it, such as
// "track:mute". The namespace alone decides where the name is looked up.

// Global properties, which the scene looks up (see Scene::GetProperty()):
// - REAPER commands, by command id (see CommandActionProperty).
// - REAPER's global state: the polled state, the timeline, and the rulers.
// - Modifiers, built in and added with Scene::AddModifierProperty().
// - Values that never change, added with Scene::AddConstProperty().
// - Properties added with Scene::AddUserProperty().
inline constexpr std::string_view kCmdNamespace = "cmd:";
inline constexpr std::string_view kStateNamespace = "state:";
inline constexpr std::string_view kModNamespace = "mod:";
inline constexpr std::string_view kConstNamespace = "const:";
inline constexpr std::string_view kUserNamespace = "user:";

// Properties of the view a mapping is in, which the view looks up:
// - The view's track (see TrackProperties).
// - The view's route (see RouteProperties).
// - The view itself: its child context, and navigation (see View).
inline constexpr std::string_view kTrackNamespace = "track:";
inline constexpr std::string_view kRouteNamespace = "route:";
inline constexpr std::string_view kViewNamespace = "view:";

// The name of a property in each namespace, built at compile time, so the
// namespace can't be mistyped. For example, kTrackName<"mute"> is "track:mute".
// Commands are named by their command id, so kCmdName<40029> is "cmd:40029".
template <int Id>
inline constexpr std::string_view kCmdName = kNumberedName<kCmdNamespace, Id>;
template <StringLiteral Name>
inline constexpr std::string_view kStateName =
    kPrefixedName<kStateNamespace, Name>;
template <StringLiteral Name>
inline constexpr std::string_view kModName = kPrefixedName<kModNamespace, Name>;
template <StringLiteral Name>
inline constexpr std::string_view kUserName =
    kPrefixedName<kUserNamespace, Name>;
template <StringLiteral Name>
inline constexpr std::string_view kTrackName =
    kPrefixedName<kTrackNamespace, Name>;
template <StringLiteral Name>
inline constexpr std::string_view kRouteName =
    kPrefixedName<kRouteNamespace, Name>;
template <StringLiteral Name>
inline constexpr std::string_view kViewName =
    kPrefixedName<kViewNamespace, Name>;

//==============================================================================
// ViewProperty
//==============================================================================

// A view property represents a single property of the REAPER state that is
// being mapped by one or more active views.
//
// Anything that needs to know when the property changes, such as an active
// mapping, registers a flag with it (see RegisterFlag()).
class ViewProperty {
 public:
  // The type of the property, which defines what values it can hold.
  enum class Type {
    // A property that corresponds to a REAPER action that can be triggered.
    // This has no state, and so can only be mapped to control inputs.
    //
    // The underlying value is std::monostate. Setting a value with
    // std::monostate will trigger the action, and reading the value will always
    // return std::monostate.
    kAction,

    // A property that corresponds to a REAPER boolean property or action with a
    // state. This has a binary state (on or off), and so can be mapped to
    // control inputs and outputs that represent a binary value.
    //
    // The underlying value is a bool, with true representing the on state and
    // false representing the off state.
    kToggle,

    // A property that corresponds to the REAPER pan parameter on a track. This
    // has a continuous value that ranges from [-1..1].
    //
    // The underlying value is a double, with -1 representing full left pan, 0
    // representing center pan, and 1 representing full right pan.
    kPan,

    // A property that corresponds to the REAPER volume parameter on a track.
    // This has a continuous value that ranges from [0..inf), with 1 being unity
    // gain. This is a logarithmic scale, so a value of 2 represents a gain of
    // 6dB, a value of 0.5 represents a gain of -6dB, and so on.
    //
    // The underlying value is a double, with 0 representing silence, 1
    // representing unity gain, and values greater than 1 representing gain
    // above unity.
    kVolume,

    // A property that corresponds to any other REAPER parameter that has a
    // continuous value. This may be continuous or discrete depending on the
    // parameter, and the range of values is always [0..1] normalized.
    //
    // The underlying value is a double, with 0 representing the minimum value
    // of the parameter, 1 representing the maximum value of the parameter.
    kNormalized,

    // A property that corresponds to a REAPER parameter that has a text value
    // (like track title). This has a text value of arbitrary length.
    //
    // The underlying value is a std::string.
    kText,

    // A property that corresponds to a REAPER parameter that has a color value
    // (like track color). This has an RGB color value.
    //
    // The underlying value is a Color, with the RGB values representing the
    // color of the parameter.
    kColor,

    // A property that corresponds to a REAPER timeline position. This has a
    // value that represents a position in the timeline, which can be
    // interpreted in different modes (e.g. beats, time, frames, samples).
    //
    // The underlying value is a TimelinePosition.
    kTimelinePosition,

    // A property that corresponds to a REAPER parameter that has an enumerated
    // set of discrete values. This has an integer value in the range
    // [0..GetMaxValue()].
    //
    // The underlying value is an int, with 0 representing the first enumerated
    // value and GetMaxValue() representing the last.
    kEnumerated,
  };

  // The value of the property. This is a variant that can hold any of the types
  // defined by the Type enum. The actual type of the value will depend on the
  // Type of the property (see the documentation for each Type for details).
  using Value = std::variant<std::monostate, bool, int, double, std::string,
                             Color, TimelinePosition>;

  explicit ViewProperty(std::string_view name, Type type);
  ViewProperty(const ViewProperty&) = delete;
  ViewProperty& operator=(const ViewProperty&) = delete;
  virtual ~ViewProperty() = default;

  // Name of the property.
  std::string_view GetName() const { return name_; }

  // Type of the property.
  Type GetType() const { return type_; }

  // Reads and returns the current value of the property from REAPER.
  Value GetValue() const;

  // Adapter functions that read and write the value of the property as a
  // specific type. These will reinterpret the underlying value of the property
  // as the specified type (for instance mapping a toggle property to a double
  // 0.0 or 1.0 for off and on, respectively).
  //
  // These may be overridden by derived classes that have a more specific
  // translation to the specific type. For instance, an enumerated property may
  // want to override GetText() to provide human-readable names for the
  // enumeration values.
  virtual bool GetBool() const;
  virtual int GetInt() const;            // Returns in range [0,GetMaxValue()]
  virtual double GetPan() const;         // Returns in range [-1,1]
  virtual double GetVolume() const;      // Returns in range [0,inf)
  virtual double GetNormalized() const;  // Returns in range [0,1]
  virtual std::string GetText() const;
  virtual Color GetColor() const;
  virtual TimelinePosition GetTimelinePosition() const;

  // Sets the value of the property in REAPER.
  //
  // The value must be of the correct type for the property or it will have no
  // effect (see the documentation for each Type for details). Values will be
  // clamped to the valid range for the property if they are out of range.
  void SetValue(const Value& value);

  // Adapter functions that set the value of the property as a specific type.
  // These will reinterpret the passed in value as the underlying type of the
  // property (for instance mapping a double value greater than 0.5 to an on
  // state for a toggle property).
  //
  // These may be overridden by derived classes that have a more specific
  // translation from the specific type. For instance, an enumerated property
  // may want to override SetText() to allow setting the value by name
  // instead of by integer value.
  virtual void SetBool(bool value);
  virtual void SetInt(int value);        // Input clamped to [0,GetMaxValue()]
  virtual void SetPan(double value);     // Input clamped to [-1,1]
  virtual void SetVolume(double value);  // Input clamped to [0,inf)
  virtual void SetNormalized(double value);  // Input clamped to [0,1]
  virtual void SetText(std::string_view value);
  virtual void SetColor(const Color& value);
  virtual void SetTimelinePosition(TimelinePosition value);

  // Returns the maximum value for an enumerated property. This is only
  // meaningful for properties of type kEnumerated, and will return 0 for other
  // types.
  virtual int GetMaxValue() const { return 0; }

  // Runs the action associated with this property. This is only applicable for
  // properties of type kAction, and will have no effect for other types.
  void RunAction() { TriggerAction(); }

  // Registers a boolean flag to be set to true whenever this property changes.
  // The flag pointer must remain valid until it is unregistered.
  void RegisterFlag(bool* flag);
  void UnregisterFlag(bool* flag);

 protected:
  // Derived classes should override these to read and write the value that is
  // appropriate for their type. This is guaranteed to be called for the correct
  // type and clamped to the valid range as specified by the property's type.
  virtual void TriggerAction() {}
  virtual bool ReadBool() const { return false; }
  virtual void WriteBool(bool value) {}
  virtual double ReadDouble() const { return 0.0; }
  virtual void WriteDouble(double value) {}
  virtual std::string ReadString() const { return ""; }
  virtual void WriteString(std::string_view value) {}
  virtual Color ReadColor() const { return {0, 0, 0}; }
  virtual void WriteColor(const Color& value) {}
  virtual TimelinePosition ReadTimelinePosition() const { return {}; }
  virtual void WriteTimelinePosition(TimelinePosition value) {}
  virtual int ReadInt() const { return 0; }
  virtual void WriteInt(int value) {}

  // Derived classes should call this whenever the property value changes.
  void NotifyChanged();

  // Called the when the first flag is registered to this property. This can be
  // used to perform any setup that is needed to track changes to the property,
  // such as subscribing to REAPER notifications.
  virtual void OnRegistered() {}

  // Called when the last flag is unregistered from this property. This can be
  // used to perform any teardown that is needed when the property is no longer
  // being tracked, such as unsubscribing from REAPER notifications.
  virtual void OnUnregistered() {}

 private:
  std::string name_;
  Type type_;
  absl::flat_hash_set<bool*> flags_;
};

}  // namespace jpr
