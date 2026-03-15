// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>

#include "jpr/common/midi_port.h"
#include "jpr/device/control_input.h"

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
  // The configuration for a ControlPressInputMidiMsg, which specifies the MIDI
  // messages to listen for when the control is pressed and released. If the
  // release message is not specified, the control will only trigger press
  // events and will not report a pressed state.
  struct Config {
    MidiMessage press;
    std::optional<MidiMessage> release;
  };

  ControlPressInputMidiMsg(MidiIn* midi_in, Config config);
  ~ControlPressInputMidiMsg() override;

 private:
  // Implements MidiListener.
  void OnMidiMessage(double time, const MidiMessage& message) override;

  MidiIn* midi_in_;
  MidiMessage press_;
  std::optional<MidiMessage> release_;
};

//==============================================================================
// ControlDeltaInputMidiCcOnesComp
//==============================================================================

// This control input type represents a delta value that is controlled by MIDI
// CC messages that are represented in a 7-bit ones-complement encoding.
class ControlDeltaInputMidiCcOnesComp : public ControlDeltaInput,
                                        private MidiListener {
 public:
  // Default scaling factor results in +/- 1.0 for a maximum value.
  static constexpr double kDefaultScaling = 1.0 / 63.0;

  // The configuration for a ControlDeltaInputMidiCcOnesComp, which specifies
  // the MIDI channel and control values to listen for, as well as the scaling
  // factor to apply to the delta value.
  struct Config {
    uint8_t channel = 0;
    uint8_t control = 0;
    double scaling = kDefaultScaling;
  };

  // Constructs an encoder input for a standard MCU-style encoder on the
  // specified track (0-7). The MIDI channel and control values will be set
  // according to the standard encoding for that track.
  static Config McuEncoder(uint8_t track, double scaling = kDefaultScaling);

  // Constructs an encoder input for a MIDI control with the specified channel
  // and control values represented as a ones-compliment signed number.
  ControlDeltaInputMidiCcOnesComp(MidiIn* midi_in, Config config);

  ~ControlDeltaInputMidiCcOnesComp() override;

 private:
  // Implements MidiListener.
  void OnMidiMessage(double time, const MidiMessage& message) override;

  MidiIn* midi_in_;
  double scaling_;
  uint8_t channel_;
  uint8_t control_;
};

//==============================================================================
// ControlValueInputMcuFader
//==============================================================================

// This control input type represents a value in the range [0.0, 1.0] that is
// controlled by MIDI pitch bend messages on a specified channel, following the
// non-linear curve defined by MCU style faders.
class ControlValueInputMcuFader : public ControlValueInput,
                                  private MidiListener {
 public:
  // The configuration for a ControlValueInputMcuFader, which specifies the
  // MIDI channel to listen for pitch bend messages on.
  struct Config {
    uint8_t channel = 0;
  };

  // Constructs a pitch bend input for the specified track (0-7). The MIDI
  // channel will be set according to the standard encoding for that track.
  static Config Track(uint8_t track) { return Config{.channel = track}; }

  // Constructs a pitch bend input for the master fader. The MIDI channel will
  // be set according to the standard encoding for the master fader on an MCU
  // device.
  static Config MasterFader() { return Config{.channel = 8}; }

  // Constructs a pitch bend input for the specified MIDI channel.
  ControlValueInputMcuFader(MidiIn* midi_in, Config config);
  ~ControlValueInputMcuFader() override;

 private:
  // Implements MidiListener.
  void OnMidiMessage(double time, const MidiMessage& message) override;

  MidiIn* midi_in_;
  uint8_t channel_;
};

}  // namespace jpr