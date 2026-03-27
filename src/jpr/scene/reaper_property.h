// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/scene/scene_state_property.h"

namespace jpr {

//==============================================================================
// REAPER commands
//==============================================================================

inline constexpr std::string_view kCmdSoloInFront = "cmd:40745";
inline constexpr std::string_view kCmdMetronome = "cmd:40364";
inline constexpr std::string_view kCmdLoopTimeSelection = "cmd:40621";
inline constexpr std::string_view kCmdTransportPlay = "cmd:1007";
inline constexpr std::string_view kCmdTransportPause = "cmd:1008";
inline constexpr std::string_view kCmdTransportPlayPause = "cmd:40073";
inline constexpr std::string_view kCmdTransportPlayStop = "cmd:40044";
inline constexpr std::string_view kCmdTransportRecord = "cmd:1013";
inline constexpr std::string_view kCmdTransportStop = "cmd:1016";
inline constexpr std::string_view kCmdGoPrevMeasure = "cmd:41041";
inline constexpr std::string_view kCmdGoNextMeasure = "cmd:41040";
inline constexpr std::string_view kCmdGoPrevMeasureNoSeek = "cmd:40838";
inline constexpr std::string_view kCmdGoNextMeasureNoSeek = "cmd:40837";
inline constexpr std::string_view kCmdGoPrevBeat = "cmd:41045";
inline constexpr std::string_view kCmdGoNextBeat = "cmd:41044";
inline constexpr std::string_view kCmdGoPrevBeatNoSeek = "cmd:40842";
inline constexpr std::string_view kCmdGoNextBeatNoSeek = "cmd:40841";
inline constexpr std::string_view kCmdGoPrevMarker = "cmd:40172";
inline constexpr std::string_view kCmdGoNextMarker = "cmd:40173";
inline constexpr std::string_view kCmdGoStart = "cmd:40042";
inline constexpr std::string_view kCmdGoEnd = "cmd:40043";
inline constexpr std::string_view kCmdAutoModeLatch = "cmd:40404";
inline constexpr std::string_view kCmdAutoModeRead = "cmd:40401";
inline constexpr std::string_view kCmdAutoModeTouch = "cmd:40402";
inline constexpr std::string_view kCmdAutoModeTrim = "cmd:40400";
inline constexpr std::string_view kCmdAutoModeWrite = "cmd:40403";

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

}  // namespace jpr
