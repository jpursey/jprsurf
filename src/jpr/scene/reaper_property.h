// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/numbered_name.h"
#include "jpr/scene/scene_state_property.h"

namespace jpr {

//==============================================================================
// REAPER commands
//==============================================================================

// Command properties are named "cmd:<id>", where the id is a REAPER command id.
// They are a CommandToggleProperty if REAPER reports a toggle state for the
// command, and a CommandActionProperty otherwise.
inline constexpr std::string_view kCmdPrefix = "cmd:";

// The name of the command property for a REAPER command id.
template <int Id>
inline constexpr std::string_view kCmdName = kNumberedName<kCmdPrefix, Id>;

inline constexpr std::string_view kCmdSoloInFront = kCmdName<40745>;
inline constexpr std::string_view kCmdMetronome = kCmdName<40364>;
inline constexpr std::string_view kCmdTransportRepeat = kCmdName<1068>;
inline constexpr std::string_view kCmdTransportPlay = kCmdName<1007>;
inline constexpr std::string_view kCmdTransportPause = kCmdName<1008>;
inline constexpr std::string_view kCmdTransportPlayPause = kCmdName<40073>;
inline constexpr std::string_view kCmdTransportPlayStop = kCmdName<40044>;
inline constexpr std::string_view kCmdTransportRecord = kCmdName<1013>;
inline constexpr std::string_view kCmdTransportStop = kCmdName<1016>;
inline constexpr std::string_view kCmdGoPrevMeasure = kCmdName<41041>;
inline constexpr std::string_view kCmdGoNextMeasure = kCmdName<41040>;
inline constexpr std::string_view kCmdGoPrevMeasureNoSeek = kCmdName<40838>;
inline constexpr std::string_view kCmdGoNextMeasureNoSeek = kCmdName<40837>;
inline constexpr std::string_view kCmdGoPrevBeat = kCmdName<40230>;
inline constexpr std::string_view kCmdGoNextBeat = kCmdName<40231>;
inline constexpr std::string_view kCmdGoBackBeat = kCmdName<41045>;
inline constexpr std::string_view kCmdGoForwardBeat = kCmdName<41044>;
inline constexpr std::string_view kCmdGoBackBeatNoSeek = kCmdName<40842>;
inline constexpr std::string_view kCmdGoForwardBeatNoSeek = kCmdName<40841>;
inline constexpr std::string_view kCmdGoPrevMarker = kCmdName<40172>;
inline constexpr std::string_view kCmdGoNextMarker = kCmdName<40173>;
inline constexpr std::string_view kCmdGoStart = kCmdName<40042>;
inline constexpr std::string_view kCmdGoEnd = kCmdName<40043>;
inline constexpr std::string_view kCmdAutoModeLatch = kCmdName<40404>;
inline constexpr std::string_view kCmdAutoModeRead = kCmdName<40401>;
inline constexpr std::string_view kCmdAutoModeTouch = kCmdName<40402>;
inline constexpr std::string_view kCmdAutoModeTrim = kCmdName<40400>;
inline constexpr std::string_view kCmdAutoModeWrite = kCmdName<40403>;
inline constexpr std::string_view kCmdUndo = kCmdName<40029>;
inline constexpr std::string_view kCmdRedo = kCmdName<40030>;
inline constexpr std::string_view kCmdSaveProject = kCmdName<40026>;
inline constexpr std::string_view kCmdSaveNewProjectVersion = kCmdName<41895>;
inline constexpr std::string_view kCmdUnselectAllItems = kCmdName<40289>;
inline constexpr std::string_view kCmdRemoveTimeSelection = kCmdName<40020>;
inline constexpr std::string_view kCmdInsertMidiItem = kCmdName<40214>;
inline constexpr std::string_view kCmdInsertEmptyItem = kCmdName<40142>;
inline constexpr std::string_view kCmdSoloDefeat = kCmdName<40340>;

//==============================================================================
// CommandActionProperty
//==============================================================================

// This property represents a REAPER action that can be triggered. It has no
// state, and so can only be mapped to control inputs.
class CommandActionProperty final : public ViewProperty {
 public:
  CommandActionProperty(std::string_view name, int command_id)
      : ViewProperty(name, Type::kAction), command_id_(command_id) {}
  ~CommandActionProperty() override = default;

 protected:
  // Overrides from ViewProperty.
  void TriggerAction() override;

 private:
  int command_id_;
};

//==============================================================================
// CommandToggleProperty
//==============================================================================

// This property represents a REAPER action with a toggle state. It can be
// mapped to control inputs and outputs that represent a binary value.
class CommandToggleProperty final : public SceneStateProperty {
 public:
  CommandToggleProperty(Scene* scene, std::string_view name, int command_id,
                        bool value)
      : SceneStateProperty(scene, name, Type::kToggle),
        command_id_(command_id),
        value_(value) {}
  ~CommandToggleProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override;
  void WriteBool(bool value) override;

 private:
  int command_id_;
  bool value_;
};

//==============================================================================
// PolledToggleProperty
//==============================================================================

// Polled toggle properties are named "state:<index>", where the index is into
// a table of read functions in reaper_property.cc. A new one needs a name here
// and a row at that index in the table (the build checks that the table's
// names match their indices).
inline constexpr std::string_view kStatePrefix = "state:";

// The name of the polled toggle property at an index in the table.
template <int Index>
inline constexpr std::string_view kStateName =
    kNumberedName<kStatePrefix, Index>;

// True while any track is soloed.
//
// This can't be a CommandToggleProperty for kCmdSoloDefeat, as REAPER reports
// no toggle state for that command.
inline constexpr std::string_view kStateAnyTrackSolo = kStateName<0>;

// True while the current project has anything to redo.
inline constexpr std::string_view kStateCanRedo = kStateName<1>;

// True while the current project has unsaved changes. This is always false if
// "undo/prompt to save" is disabled in REAPER's preferences.
inline constexpr std::string_view kStateProjectDirty = kStateName<2>;

// This read-only property represents REAPER state that is read with a function,
// and polled each run. It can be mapped to control outputs that represent a
// binary value.
class PolledToggleProperty final : public SceneStateProperty {
 public:
  using ReadFunction = bool (*)();

  // Returns the read function for the polled toggle at the index in its
  // "state:<index>" name, or null if the index is out of range.
  static ReadFunction GetReadFunction(int index);

  PolledToggleProperty(Scene* scene, std::string_view name, ReadFunction read)
      : SceneStateProperty(scene, name, Type::kToggle),
        read_(read),
        value_(read()) {}
  ~PolledToggleProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override;

 private:
  ReadFunction read_;
  bool value_;
};

}  // namespace jpr
