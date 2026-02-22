// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>

#include "control_input.h"
#include "midi_port.h"

namespace jpr {

//==============================================================================
// ControlPressInputMidiMsg
//==============================================================================

// This control input type represents a press value that is controlled by MIDI
// messages.
//
// One message is triggered when a control is pressed, and optionally another
// message is triggered when the control is released.
class ControlPressInputMidiMsg : public ControlPressInput,
                                 private MidiListener {
 public:
  ControlPressInputMidiMsg(MidiIn* midi_in, MidiMessage press,
                           std::optional<MidiMessage> release = std::nullopt);
  ~ControlPressInputMidiMsg() override;

 private:
  // Implements MidiListener.
  void OnMidiMessage(double time, const MidiMessage& message) override;

  MidiIn* midi_in_;
  MidiMessage press_;
  std::optional<MidiMessage> release_;
};

}  // namespace jpr