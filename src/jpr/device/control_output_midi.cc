// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_midi.h"

namespace jpr {

ControlDValueOutputMidiNote::ControlDValueOutputMidiNote(MidiOut* midi_out,
                                                         uint8_t channel,
                                                         uint8_t note,
                                                         bool use_note_off)
    : ControlDValueOutput(/*max_value=*/1), midi_out_(midi_out) {
  messages_[0] = (use_note_off ? MidiNoteOff(channel, note, 0x00)
                               : MidiNoteOn(channel, note, 0x00));
  messages_[1] = MidiNoteOn(channel, note, 0x7F);
}

ControlDValueOutputMidiNote::~ControlDValueOutputMidiNote() = default;

void ControlDValueOutputMidiNote::OnValueChanged(int value) {
  midi_out_->UpdateState(messages_[value]);
}

}  // namespace jpr
