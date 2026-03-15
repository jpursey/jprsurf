// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input_midi.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

namespace jpr {

//==============================================================================
// ControlPressInputMidiMsg
//==============================================================================

ControlPressInputMidiMsg::ControlPressInputMidiMsg(MidiIn* midi_in,
                                                   Config config)
    : ControlPressInput(config.release.has_value()),
      midi_in_(midi_in),
      press_(config.press),
      release_(config.release) {
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

//==============================================================================
// ControlValueInputMcuFader
//==============================================================================

namespace {
struct McuFaderPoint {
  double pitch_bend;
  double normalized;
};

// Approximation of curve that lines up wth the markings on MCU faders.
constexpr McuFaderPoint kMcuFaderPoints[] = {
    {0.0, 0.0},            // -infinity
    {530.0, 0.000316228},  // -60dB
    {1700.0, 0.001},       // -50dB
    {2750.0, 0.00316228},  // -40dB
    {3850.0, 0.01},        // -30dB
    {6100.0, 0.0316228},   // -20dB
    {8250.0, 0.1},         // -10dB
    {10600.0, 0.177828},   // -5dB
    {12720.0, 0.316228},   // 0dB
    {14900.0, 0.562341},   // 5dB
    {16383.0, 1.0},        // 10dB
};

}  // namespace

ControlValueInputMcuFader::ControlValueInputMcuFader(MidiIn* midi_in,
                                                     Config config)
    : ControlValueInput(), midi_in_(midi_in), channel_(config.channel) {
  DCHECK(midi_in_ != nullptr);
  midi_in_->Subscribe(this, MidiPitchBendStatus(channel_));
}

ControlValueInputMcuFader::~ControlValueInputMcuFader() {
  midi_in_->Unsubscribe(this);
}

void ControlValueInputMcuFader::OnMidiMessage(double time,
                                              const MidiMessage& message) {
  double pitch_bend = static_cast<double>(
      (static_cast<int>(message.data2) << 7) | message.data1);
  double value = 0.0;
  constexpr int kLastPoint = std::size(kMcuFaderPoints) - 1;
  if (pitch_bend <= kMcuFaderPoints[0].pitch_bend)
    value = kMcuFaderPoints[0].normalized;
  else if (pitch_bend >= kMcuFaderPoints[kLastPoint].pitch_bend)
    value = kMcuFaderPoints[kLastPoint].normalized;
  else {
    for (int i = 0; i < kLastPoint; ++i) {
      if (pitch_bend >= kMcuFaderPoints[i].pitch_bend &&
          pitch_bend <= kMcuFaderPoints[i + 1].pitch_bend) {
        double t =
            (pitch_bend - kMcuFaderPoints[i].pitch_bend) /
            (kMcuFaderPoints[i + 1].pitch_bend - kMcuFaderPoints[i].pitch_bend);
        value = kMcuFaderPoints[i].normalized +
                t * (kMcuFaderPoints[i + 1].normalized -
                     kMcuFaderPoints[i].normalized);
        break;
      }
    }
  }

  SetValue(value);
}

}  // namespace jpr