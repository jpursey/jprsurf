// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <vector>

#include "absl/types/span.h"
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
  // The configuration for a ControlDValueOutputMidiNote, which specifies the
  // MIDI channel and note number to use for the note on/off messages, and
  // whether to use note off messages or note on messages with velocity 0 for
  // the off state.
  struct Config {
    uint8_t channel = 0;
    uint8_t note = 0;

    // If true, value 0 sends a note off message. If false, value 0 sends a note
    // on message with velocity 0.
    bool use_note_off = false;
  };

  ControlDValueOutputMidiNote(MidiOut* midi_out, Config config);
  ~ControlDValueOutputMidiNote() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value, int mode) override;

  MidiOut* midi_out_;
  MidiMessage messages_[2];
};

//==============================================================================
// ControlDValueOutputMidiCc
//==============================================================================

class ControlDValueOutputMidiCc : public ControlDValueOutput {
 public:
  // A mode defines how the discrete value maps to the 7-bit CC value for a
  // given mode. The value is mapped linearly from [0, max_value] to the 7-bit
  // CC range [0, 127], with the value_or modifier applied as a bitwise OR and
  // the value_add modifier applied as an addition after the linear mapping.
  struct Mode {
    uint8_t value_or = 0;   // Or'd with the CC value.
    uint8_t value_add = 0;  // Added to the CC value.
    int max_value = 1;      // Maximum value (before value_add or value_or).
  };

  // The configuration for a ControlDValueOutputMidiCc, which specifies the MIDI
  // channel and control number to use for the CC messages, and one or more
  // modes that define how the discrete value maps to the 7-bit CC value.
  struct Config {
    uint8_t channel = 0;
    uint8_t control = 0;
    absl::Span<const Mode> modes;  // One or more modes.
  };

  // Returns the configuration for a standard per-track encoder light-ring on an
  // MCU device for the specified track. All 9 MCU encoder modes are included:
  //   - Mode 0: Single LED from left to right (11 values)
  //   - Mode 1: Fill from center from left to right (11 values).
  //   - Mode 2: Fill from left from left to right (11 values).
  //   - Mode 3: Spread from center to both side (5 values).
  //   - Mode 4: Same as Mode 0, but right and left always lit.
  //   - Mode 5: Same as Mode 1, but right and left always lit.
  //   - Mode 6: Same as Mode 2, but right and left always lit.
  //   - Mode 7: Same as Mode 3, but right and left always lit.
  //   - Mode 8: All LEDs off (always sends 0).
  static Config McuEncoder(uint8_t track);

  ControlDValueOutputMidiCc(MidiOut* midi_out, Config config);
  ~ControlDValueOutputMidiCc() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value, int mode) override;

  MidiOut* midi_out_;
  uint8_t status_;
  uint8_t control_;
  std::vector<Mode> modes_;
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
  // A mode defines how the discrete value maps to the 7-bit pressure value for
  // a given mode. The value is mapped linearly from [0, max_value] to the 7-bit
  // pressure range [0, 127], with the value_or modifier applied as a bitwise OR
  // and the value_add modifier applied as an addition after the linear mapping.
  struct Mode {
    uint8_t value_or = 0;   // Or'd with the pressure value.
    uint8_t value_add = 0;  // Added to the pressure value.
    int max_value = 1;      // Maximum value (before value_add or value_or).
  };

  // The configuration for a ControlDValueOutputMidiCPressure, which specifies
  // the MIDI channel to use for the channel pressure messages, and one or more
  // modes that define how the discrete value maps to the 7-bit pressure value.
  struct Config {
    uint8_t channel = 0;
    absl::Span<const Mode> modes;  // One or more modes.
  };

  ControlDValueOutputMidiCPressure(MidiOut* midi_out, Config config);
  ~ControlDValueOutputMidiCPressure() override;

 private:
  // Implements ControlDValueOutput.
  void OnValueChanged(int value, int mode) override;

  MidiOut* midi_out_;
  uint8_t status_;
  std::vector<Mode> modes_;
};

//==============================================================================
// ControlCValueOutputMcuFader
//==============================================================================

// This control output maps a continuous value in [0.0, 1.0] to a MIDI pitch
// bend message using the non-linear curve defined by MCU style faders. The
// curve maps from negative infinity (0.0) to +10dB (1.0), which is the
// inverse of the mapping performed by ControlValueInputMcuFader.
class ControlCValueOutputMcuFader : public ControlCValueOutput {
 public:
  // The configuration for a ControlCValueOutputMcuFader, which specifies the
  // MIDI channel to send pitch bend messages on.
  struct Config {
    uint8_t channel = 0;
  };

  // Returns the configuration for a standard per-track fader on an MCU device
  // for the specified track (0-7).
  static Config Track(uint8_t track) { return Config{.channel = track}; }

  // Returns the configuration for the master fader on an MCU device.
  static Config MasterFader() { return Config{.channel = 8}; }

  ControlCValueOutputMcuFader(MidiOut* midi_out, Config config);
  ~ControlCValueOutputMcuFader() override;

 private:
  // Implements ControlCValueOutput.
  void OnValueChanged(double value, int mode) override;

  MidiOut* midi_out_;
  uint8_t channel_;
};

}  // namespace jpr
