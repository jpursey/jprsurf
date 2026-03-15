// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_midi.h"

#include "jpr/common/midi_message.h"

namespace jpr {

namespace {

std::vector<int> GetMaxValues(
    absl::Span<const ControlDValueOutputMidiCc::Mode> modes) {
  std::vector<int> max_values;
  max_values.reserve(modes.size());
  for (const auto& mode : modes) {
    max_values.push_back(mode.max_value);
  }
  return max_values;
}

std::vector<int> GetMaxValues(
    absl::Span<const ControlDValueOutputMidiCPressure::Mode> modes) {
  std::vector<int> max_values;
  max_values.reserve(modes.size());
  for (const auto& mode : modes) {
    max_values.push_back(mode.max_value);
  }
  return max_values;
}

}  // namespace

//==============================================================================
// ControlDValueOutputMidiNote
//==============================================================================

ControlDValueOutputMidiNote::ControlDValueOutputMidiNote(MidiOut* midi_out,
                                                         Config config)
    : ControlDValueOutput(/*max_value=*/1), midi_out_(midi_out) {
  messages_[0] =
      (config.use_note_off ? MidiNoteOff(config.channel, config.note, 0x00)
                           : MidiNoteOn(config.channel, config.note, 0x00));
  messages_[1] = MidiNoteOn(config.channel, config.note, 0x7F);
}

ControlDValueOutputMidiNote::~ControlDValueOutputMidiNote() = default;

void ControlDValueOutputMidiNote::OnValueChanged(int value, int mode) {
  midi_out_->UpdateState(messages_[value]);
}

//==============================================================================
// ControlDValueOutputMidiCc
//==============================================================================

ControlDValueOutputMidiCc::Config ControlDValueOutputMidiCc::McuEncoder(
    uint8_t track) {
  static constexpr Mode kModes[] = {
      {.value_or = 0x00, .value_add = 1, .max_value = 10},  // Mode 0
      {.value_or = 0x10, .value_add = 1, .max_value = 10},  // Mode 1
      {.value_or = 0x20, .value_add = 1, .max_value = 10},  // Mode 2
      {.value_or = 0x30, .value_add = 1, .max_value = 5},   // Mode 3
      {.value_or = 0x40, .value_add = 1, .max_value = 10},  // Mode 4
      {.value_or = 0x50, .value_add = 1, .max_value = 10},  // Mode 5
      {.value_or = 0x60, .value_add = 1, .max_value = 10},  // Mode 6
      {.value_or = 0x70, .value_add = 1, .max_value = 5},   // Mode 7
  };
  return Config{
      .channel = 0,
      .control = static_cast<uint8_t>(0x30 + std::clamp<uint8_t>(track, 0, 7)),
      .modes = kModes,
  };
}

ControlDValueOutputMidiCc::ControlDValueOutputMidiCc(MidiOut* midi_out,
                                                     Config config)
    : ControlDValueOutput(GetMaxValues(config.modes)),
      midi_out_(midi_out),
      status_(MidiCcStatus(config.channel)),
      control_(config.control),
      modes_(config.modes.begin(), config.modes.end()) {}

ControlDValueOutputMidiCc::~ControlDValueOutputMidiCc() = default;

void ControlDValueOutputMidiCc::OnValueChanged(int value, int mode) {
  const Mode& m = modes_[mode];
  MidiMessage message{
      .status = status_,
      .data1 = control_,
      .data2 = static_cast<uint8_t>(m.value_or | (value + m.value_add)),
  };
  midi_out_->UpdateState(message);
}

//==============================================================================
// ControlDValueOutputMidiCPressure
//==============================================================================

ControlDValueOutputMidiCPressure::ControlDValueOutputMidiCPressure(
    MidiOut* midi_out, Config config)
    : ControlDValueOutput(GetMaxValues(config.modes)),
      midi_out_(midi_out),
      status_(MidiChannelPressureStatus(config.channel)),
      modes_(config.modes.begin(), config.modes.end()) {}

ControlDValueOutputMidiCPressure::~ControlDValueOutputMidiCPressure() = default;

void ControlDValueOutputMidiCPressure::OnValueChanged(int value, int mode) {
  const Mode& m = modes_[mode];
  MidiMessage message{
      .status = status_,
      .data1 = static_cast<uint8_t>(m.value_or | (value + m.value_add)),
      .data2 = 0,
  };
  midi_out_->UpdateState(message);
}

//==============================================================================
// ControlCValueOutputMcuFader
//==============================================================================

namespace {

struct McuFaderPoint {
  double normalized;
  double pitch_bend;
};

// Approximation of curve that lines up with the markings on MCU faders.
// This is the same data as in control_input_midi.cc but keyed by normalized
// value for the inverse lookup.
constexpr McuFaderPoint kMcuFaderPoints[] = {
    {0.0, 0.0},            // -infinity
    {0.000316228, 530.0},  // -60dB
    {0.001, 1700.0},       // -50dB
    {0.00316228, 2750.0},  // -40dB
    {0.01, 3850.0},        // -30dB
    {0.0316228, 6100.0},   // -20dB
    {0.1, 8250.0},         // -10dB
    {0.177828, 10600.0},   // -5dB
    {0.316228, 12720.0},   // 0dB
    {0.562341, 14900.0},   // 5dB
    {1.0, 16383.0},        // 10dB
};

}  // namespace

ControlCValueOutputMcuFader::ControlCValueOutputMcuFader(MidiOut* midi_out,
                                                         Config config)
    : ControlCValueOutput(), midi_out_(midi_out), channel_(config.channel) {}

ControlCValueOutputMcuFader::~ControlCValueOutputMcuFader() = default;

void ControlCValueOutputMcuFader::OnValueChanged(double value, int mode) {
  double pitch_bend = 0.0;
  constexpr int kLastPoint = std::size(kMcuFaderPoints) - 1;
  if (value <= kMcuFaderPoints[0].normalized) {
    pitch_bend = kMcuFaderPoints[0].pitch_bend;
  } else if (value >= kMcuFaderPoints[kLastPoint].normalized) {
    pitch_bend = kMcuFaderPoints[kLastPoint].pitch_bend;
  } else {
    for (int i = 0; i < kLastPoint; ++i) {
      if (value >= kMcuFaderPoints[i].normalized &&
          value <= kMcuFaderPoints[i + 1].normalized) {
        double t =
            (value - kMcuFaderPoints[i].normalized) /
            (kMcuFaderPoints[i + 1].normalized - kMcuFaderPoints[i].normalized);
        pitch_bend = kMcuFaderPoints[i].pitch_bend +
                     t * (kMcuFaderPoints[i + 1].pitch_bend -
                          kMcuFaderPoints[i].pitch_bend);
        break;
      }
    }
  }

  uint16_t pb = static_cast<uint16_t>(std::clamp(pitch_bend, 0.0, 16383.0));
  midi_out_->UpdateState(MidiPitchBend(channel_, pb));
}

}  // namespace jpr
