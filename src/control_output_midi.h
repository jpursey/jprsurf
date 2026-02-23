// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "control_output.h"
#include "midi_port.h"

namespace jpr {

// This control output is an on/off discrete value that maps to a MIDI note
// on/off message.
//
// The value 0 corresponds to note off (or note on with velocity 0), and the
// value 1 corresponds to note on with velocity 127. This is generally used for
// controls that only have two states, such as an indicator LED, but can be
// used for any control that can be represented with a discrete value.
class ControlDValueOutputMidiNote : public ControlDValueOutput {
 public:
  ControlDValueOutputMidiNote(MidiOut* midi_out, uint8_t channel, uint8_t note,
                              bool use_note_off);
  ~ControlDValueOutputMidiNote() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value) override;

  MidiOut* midi_out_;
  MidiMessage messages_[2];
};

}  // namespace jpr
