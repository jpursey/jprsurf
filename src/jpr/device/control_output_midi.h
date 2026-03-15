// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/midi_port.h"
#include "jpr/device/control_output.h"

namespace jpr {

//==============================================================================
// ControlDValueOutputMidiNote
//==============================================================================

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

//==============================================================================
// ControlDValueOutputMidiCc
//==============================================================================

class ControlDValueOutputMidiCc : public ControlDValueOutput {
 public:
  struct Config {
    uint8_t channel = 0;
    uint8_t control = 0;
    uint8_t value_or = 0;   // Or'd with the CC value.
    uint8_t value_add = 0;  // Added to the CC value.
    int max_value = 1;      // Maximum value (before value_add or value_or).
  };

  // Returns the configuration for a standard per-track encoder light-ring on an
  // MCU device for the specified track and mode. Track must be between 0 and 7,
  // and mode must be between 0 and 7. Modes are as follows:
  //   - Mode 0: Single LED from left to right (11 values)
  //   - Mode 1: Fill from center from left to right (11 values).
  //   - Mode 2: Fill from left from left to right (11 values).
  //   - Mode 3: Spread from center to both side (5 values).
  //   - Mode 4: Same as Mode 0, but right and left always lit.
  //   - Mode 5: Same as Mode 1, but right and left always lit.
  //   - Mode 6: Same as Mode 2, but right and left always lit.
  //   - Mode 7: Same as Mode 3, but right and left always lit.
  static Config McuEncoder(uint8_t track, uint8_t mode);

  ControlDValueOutputMidiCc(MidiOut* midi_out, Config config);
  ~ControlDValueOutputMidiCc() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value) override;

  MidiOut* midi_out_;
  uint8_t status_;
  uint8_t control_;
  uint8_t value_or_;
  uint8_t value_add_;
};

//==============================================================================
// ControlDValueOutputMidiCPressure
//==============================================================================

// This control output maps a discrete value to a MIDI channel pressure
// (aftertouch) message.
//
// The value is mapped linearly from [0, max_value] to the 7-bit pressure range
// [0, 127], with optional value_or and value_add modifiers applied the same way
// as ControlDValueOutputMidiCc.
class ControlDValueOutputMidiCPressure : public ControlDValueOutput {
 public:
  struct Config {
    uint8_t channel = 0;
    uint8_t value_or = 0;   // Or'd with the pressure value.
    uint8_t value_add = 0;  // Added to the pressure value.
    int max_value = 1;      // Maximum value (before value_add or value_or).
  };

  ControlDValueOutputMidiCPressure(MidiOut* midi_out, Config config);
  ~ControlDValueOutputMidiCPressure() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value) override;

  MidiOut* midi_out_;
  uint8_t status_;
  uint8_t value_or_;
  uint8_t value_add_;
};

}  // namespace jpr
