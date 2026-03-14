// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input_midi.h"

#include "absl/log/check.h"

namespace jpr {

//==============================================================================
// ControlPressInputMidiMsg
//==============================================================================

ControlPressInputMidiMsg::ControlPressInputMidiMsg(
    MidiIn* midi_in, MidiMessage press, std::optional<MidiMessage> release)
    : ControlPressInput(release.has_value()),
      midi_in_(midi_in),
      press_(press),
      release_(release) {
  DCHECK(midi_in_ != nullptr);
  midi_in_->Subscribe(this, press_.status, press_.data1);
  if (release_.has_value()) {
    midi_in_->Subscribe(this, release_->status, release_->data1);
  }
}

ControlPressInputMidiMsg::~ControlPressInputMidiMsg() {
  midi_in_->Unsubscribe(this);
}

void ControlPressInputMidiMsg::OnMidiMessage(double time,
                                             const MidiMessage& message) {
  if (message.status == press_.status && message.data1 == press_.data1 &&
      message.data2 == press_.data2) {
    Press();
  } else if (release_.has_value() && message.status == release_->status &&
             message.data1 == release_->data1 &&
             message.data2 == release_->data2) {
    Release();
  }
}

//==============================================================================
// ControlDeltaInputMidiCcOnesComp
//==============================================================================

ControlDeltaInputMidiCcOnesComp::Config
ControlDeltaInputMidiCcOnesComp::McuEncoder(uint8_t track, double scaling) {
  return Config{
      .channel = 0,
      .control = static_cast<uint8_t>(0x10 + std::clamp<uint8_t>(track, 0, 7)),
      .scaling = scaling};
}

ControlDeltaInputMidiCcOnesComp::ControlDeltaInputMidiCcOnesComp(
    MidiIn* midi_in, Config config)
    : ControlDeltaInput(),
      midi_in_(midi_in),
      scaling_(config.scaling),
      channel_(config.channel),
      control_(config.control) {
  DCHECK(midi_in_ != nullptr);
  midi_in_->Subscribe(this, MidiCcStatus(channel_), control_);
}

ControlDeltaInputMidiCcOnesComp::~ControlDeltaInputMidiCcOnesComp() {
  midi_in_->Unsubscribe(this);
}

void ControlDeltaInputMidiCcOnesComp::OnMidiMessage(
    double time, const MidiMessage& message) {
  double delta = (message.data2 & 0x3F) * scaling_;
  if (message.data2 & 0x40) {
    delta = -delta;
  }
  AddDelta(delta);
}

}  // namespace jpr