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

inline uint8_t MidiNoteOnStatus(uint8_t channel) {
  return static_cast<uint8_t>(0x90 | (channel & 0x0F));
}

inline MidiMessage MidiNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  return MidiMessage{MidiNoteOnStatus(channel), note, velocity};
}

inline uint8_t MidiNoteOffStatus(uint8_t channel) {
  return static_cast<uint8_t>(0x80 | (channel & 0x0F));
}

inline MidiMessage MidiNoteOff(uint8_t channel, uint8_t note,
                               uint8_t velocity) {
  return MidiMessage{MidiNoteOffStatus(channel), note, velocity};
}

inline uint8_t MidiCcStatus(uint8_t channel) {
  return static_cast<uint8_t>(0xB0 | (channel & 0x0F));
}

inline MidiMessage MidiCc(uint8_t channel, uint8_t control, uint8_t value) {
  return MidiMessage{MidiCcStatus(channel), control, value};
}

inline uint8_t MidiPolyPressureStatus(uint8_t channel) {
  return static_cast<uint8_t>(0xA0 | (channel & 0x0F));
}

inline MidiMessage MidiPolyPressure(uint8_t channel, uint8_t note,
                                    uint8_t pressure) {
  return MidiMessage{MidiPolyPressureStatus(channel), note, pressure};
}

inline uint8_t MidiChannelPressureStatus(uint8_t channel) {
  return static_cast<uint8_t>(0xD0 | (channel & 0x0F));
}

inline MidiMessage MidiChannelPressure(uint8_t channel, uint8_t pressure) {
  return MidiMessage{MidiChannelPressureStatus(channel), pressure, 0};
}

inline uint8_t MidiPitchBendStatus(uint8_t channel) {
  return static_cast<uint8_t>(0xE0 | (channel & 0x0F));
}

inline MidiMessage MidiPitchBend(uint8_t channel, uint16_t value) {
  return MidiMessage{MidiPitchBendStatus(channel),
                     static_cast<uint8_t>(value & 0x7F),
                     static_cast<uint8_t>((value >> 7) & 0x7F)};
}

}  // namespace jpr
