// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "control_output.h"
#include "midi_port.h"

namespace jpr {

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
