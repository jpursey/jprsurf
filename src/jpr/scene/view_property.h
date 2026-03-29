// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"

namespace jpr {

// A view property represents a single property of the REAPER state that is
// being mapped by one or more active views.
//
// It holds the state for the property and the set of all active mappings to
// view controls. This is used by the Scene to find all mappings that need to be
// updated when the property changes.
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
  bool GetBool() const;
  int GetInt() const;            // Returns in range [0,GetMaxValue()]
  double GetPan() const;         // Returns in range [-1,1]
  double GetVolume() const;      // Returns in range [0,inf)
  double GetNormalized() const;  // Returns in range [0,1]
  std::string GetText() const;
  Color GetColor() const;
  TimelinePosition GetTimelinePosition() const;

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
  void SetBool(bool value);
  void SetInt(int value);            // Input clamped to [0,GetMaxValue()]
  void SetPan(double value);         // Input clamped to [-1,1]
  void SetVolume(double value);      // Input clamped to [0,inf)
  void SetNormalized(double value);  // Input clamped to [0,1]
  void SetText(std::string_view value);
  void SetColor(const Color& value);
  void SetTimelinePosition(TimelinePosition value);

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
