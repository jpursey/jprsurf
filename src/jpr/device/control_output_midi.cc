// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_midi.h"

#include "jpr/common/midi_message.h"

namespace jpr {

//==============================================================================
// ControlDValueOutputMidiNote
//==============================================================================

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

//==============================================================================
// ControlDValueOutputMidiCc
//==============================================================================

ControlDValueOutputMidiCc::Config ControlDValueOutputMidiCc::McuEncoder(
    uint8_t track, uint8_t mode) {
  return Config{
      .channel = 0,
      .control = static_cast<uint8_t>(0x30 + std::clamp<uint8_t>(track, 0, 7)),
      .value_or = static_cast<uint8_t>(std::clamp<uint8_t>(mode, 0, 7) << 4),
      .value_add = 1,
      .max_value = ((mode & 3) < 3 ? 10 : 5),
  };
}

ControlDValueOutputMidiCc::ControlDValueOutputMidiCc(MidiOut* midi_out,
                                                     Config config)
    : ControlDValueOutput(config.max_value),
      midi_out_(midi_out),
      status_(MidiCcStatus(config.channel)),
      control_(config.control),
      value_or_(config.value_or),
      value_add_(config.value_add) {}

ControlDValueOutputMidiCc::~ControlDValueOutputMidiCc() = default;

void ControlDValueOutputMidiCc::OnValueChanged(int value) {
  MidiMessage message{
      .status = status_,
      .data1 = control_,
      .data2 = static_cast<uint8_t>(value_or_ | (value + value_add_)),
  };
  midi_out_->UpdateState(message);
}

//==============================================================================
// ControlDValueOutputMidiCPressure
//==============================================================================

ControlDValueOutputMidiCPressure::ControlDValueOutputMidiCPressure(
    MidiOut* midi_out, Config config)
    : ControlDValueOutput(config.max_value),
      midi_out_(midi_out),
      status_(MidiChannelPressureStatus(config.channel)),
      value_or_(config.value_or),
      value_add_(config.value_add) {}

ControlDValueOutputMidiCPressure::~ControlDValueOutputMidiCPressure() = default;

void ControlDValueOutputMidiCPressure::OnValueChanged(int value) {
  MidiMessage message{
      .status = status_,
      .data1 = static_cast<uint8_t>(value_or_ | (value + value_add_)),
      .data2 = 0,
  };
  midi_out_->UpdateState(message);
}

}  // namespace jpr
