// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>

namespace jpr {

//==============================================================================
// MidiMessage
//==============================================================================

// Defines a standard 1 to 3 byte MIDI message, which consists of a status byte
// and up to two data bytes. Whether the data bytes are used depends on the
// status byte (if they are not used they must be zero). This supports all MIDI
// messages other than SysEx messages which are handled separately.
struct MidiMessage {
  bool operator==(const MidiMessage&) const = default;

  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

//==============================================================================
// Generators for specific messages
//==============================================================================

inline MidiMessage MidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  return MidiMessage{static_cast<uint8_t>(0x90 | (channel & 0x0F)), note,
                     velocity};
}

inline MidiMessage MidiNoteOff(uint8_t channel, uint8_t note,
                               uint8_t velocity) {
  return MidiMessage{static_cast<uint8_t>(0x80 | (channel & 0x0F)), note,
                     velocity};
}

inline MidiMessage MidiCc(uint8_t channel, uint8_t control, uint8_t value) {
  return MidiMessage{static_cast<uint8_t>(0xB0 | (channel & 0x0F)), control,
                     value};
}

inline MidiMessage MidiPitchBend(uint8_t channel, uint16_t value) {
  return MidiMessage{static_cast<uint8_t>(0xE0 | (channel & 0x0F)),
                     static_cast<uint8_t>(value & 0x7F),
                     static_cast<uint8_t>((value >> 7) & 0x7F)};
}

}  // namespace jpr
