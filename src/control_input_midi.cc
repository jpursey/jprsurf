// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "control_input_midi.h"

#include "absl/log/check.h"

namespace jpr {

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

}  // namespace jpr