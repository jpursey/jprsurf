// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_output_midi.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::_;
using ::testing::ElementsAre;

// Each output sends on an output port, which the fake's port records.
class ControlOutputMidiTest : public ::testing::Test {
 protected:
  ControlOutputMidiTest() { fake_output_->SetRecording(true); }

  // Sends what the outputs queued, and returns it.
  std::vector<std::vector<uint8_t>> TakeSent() {
    ports_.RunOutput(RunTime::Now());
    return fake_output_->TakeReceived();
  }

  FakeReaper reaper_;
  FakeMidiOutput* fake_output_ = reaper_.AddMidiOutput("Device");
  MidiPorts ports_;
  MidiOut* output_ = ports_.OpenOutput("Device");
};

//==============================================================================
// ControlDValueOutputMidiNote
//==============================================================================

TEST_F(ControlOutputMidiTest, NoteIsOnWithItsModesVelocity) {
  constexpr ControlDValueOutputMidiNote::Mode kModes[] = {
      {.on_velocity = 0x7F}, {.on_velocity = 0x40}};
  ControlDValueOutputMidiNote output(
      output_, {.channel = 2, .note = 0x30, .modes = kModes});
  EXPECT_EQ(output.GetModeCount(), 2);
  EXPECT_EQ(output.GetMaxValue(), 1);

  output.SetValue(1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x92, 0x30, 0x7F)));
  output.SetValue(1, /*mode=*/1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x92, 0x30, 0x40)));

  // Off is the same in every mode.
  output.SetValue(0, /*mode=*/1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x92, 0x30, 0x00)));
}

TEST_F(ControlOutputMidiTest, NoteCanBeOffWithNoteOff) {
  constexpr ControlDValueOutputMidiNote::Mode kModes[] = {{}};
  ControlDValueOutputMidiNote output(
      output_,
      {.channel = 0, .note = 0x30, .use_note_off = true, .modes = kModes});

  output.SetValue(1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x90, 0x30, 0x7F)));
  output.SetValue(0);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x80, 0x30, 0x00)));
}

TEST_F(ControlOutputMidiTest, McuLightIsOnOrBlinking) {
  ControlDValueOutputMidiNote output(
      output_, ControlDValueOutputMidiNote::McuLight(0x5E));
  EXPECT_EQ(output.GetModeCount(), 2);

  output.SetValue(1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x90, 0x5E, 0x7F)));
  output.SetValue(1, /*mode=*/1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x90, 0x5E, 0x01)));
  output.SetValue(0);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0x90, 0x5E, 0x00)));
}

//==============================================================================
// ControlDValueOutputMidiCc
//==============================================================================

// Each mode's value is its `value_or` bits, or'd with the value plus
// `value_add`.
TEST_F(ControlOutputMidiTest, CcValueIsEachModesBitsAndOffset) {
  constexpr ControlDValueOutputMidiCc::Mode kModes[] = {
      {.max_value = 127},
      {.value_or = 0x40, .value_add = 1, .max_value = 10},
  };
  ControlDValueOutputMidiCc output(
      output_, {.channel = 3, .control = 0x07, .modes = kModes});
  EXPECT_EQ(output.GetModeCount(), 2);
  EXPECT_EQ(output.GetMaxValue(), 127);
  EXPECT_EQ(output.GetMaxValue(/*mode=*/1), 10);

  output.SetValue(100);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xB3, 0x07, 100)));
  output.SetValue(5, /*mode=*/1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xB3, 0x07, 0x46)));
}

// The rings' modes 0-7 are bits 4-6, and positions 1-11 (or 1-6 for spread)
// are the value plus one. Mode 8 is all off.
TEST_F(ControlOutputMidiTest, McuEncoderRingHasEachMode) {
  ControlDValueOutputMidiCc output(output_,
                                   ControlDValueOutputMidiCc::McuEncoder(2));
  EXPECT_EQ(output.GetModeCount(), 9);
  for (int mode = 0; mode < 8; ++mode) {
    SCOPED_TRACE(mode);
    const int max_value = output.GetMaxValue(mode);
    EXPECT_EQ(max_value, mode % 4 == 3 ? 5 : 10);
    output.SetValue(0, mode);
    EXPECT_THAT(TakeSent(),
                ElementsAre(ElementsAre(0xB0, 0x32, (mode << 4) | 1)));
    output.SetValue(max_value, mode);
    EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(
                                0xB0, 0x32, (mode << 4) | (max_value + 1))));
  }

  EXPECT_EQ(output.GetMaxValue(/*mode=*/8), 0);
  output.SetValue(5, /*mode=*/8);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xB0, 0x32, 0x00)));
}

TEST_F(ControlOutputMidiTest, McuEncoderRingsAreTheirControls) {
  ControlDValueOutputMidiCc first(output_,
                                  ControlDValueOutputMidiCc::McuEncoder(0));
  ControlDValueOutputMidiCc last(output_,
                                 ControlDValueOutputMidiCc::McuEncoder(9));
  first.SetValue(0);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xB0, 0x30, _)));

  // A track past the eighth is the eighth.
  last.SetValue(0);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xB0, 0x37, _)));
}

//==============================================================================
// ControlDValueOutputMidiCPressure
//==============================================================================

// As a CC's, without the control. Channel pressure has one data byte, and the
// port records the three it is passed.
TEST_F(ControlOutputMidiTest, ChannelPressureIsEachModesBitsAndOffset) {
  constexpr ControlDValueOutputMidiCPressure::Mode kModes[] = {
      {.max_value = 127},
      {.value_or = 0x20, .value_add = 2, .max_value = 10},
  };
  ControlDValueOutputMidiCPressure output(output_,
                                          {.channel = 1, .modes = kModes});
  EXPECT_EQ(output.GetModeCount(), 2);

  output.SetValue(100);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xD1, 100, _)));
  output.SetValue(5, /*mode=*/1);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xD1, 0x27, _)));
}

//==============================================================================
// ControlCValueOutputMcuFader
//==============================================================================

// Points on the MCU fader curve: a value, and the fader's position for it.
struct FaderPoint {
  double value;
  int position;
};

constexpr FaderPoint kFaderCurve[] = {
    {1.0, 16383},                          // +10 dB
    {(0.316228 + 0.562341) / 2.0, 13810},  // Halfway from 0 dB to +5 dB.
    {0.316228, 12720},                     // 0 dB
    {0.1, 8250},                           // -10 dB
    {0.000316228, 530},                    // -60 dB
    {0.0, 0},                              // -inf
};

// Returns the 14-bit position of a pitch bend message's `bytes`.
int GetPosition(const std::vector<uint8_t>& bytes) {
  return (bytes[2] << 7) | bytes[1];
}

TEST_F(ControlOutputMidiTest, FaderFollowsTheMcuCurve) {
  ControlCValueOutputMcuFader output(output_,
                                     ControlCValueOutputMcuFader::Track(2));
  for (const FaderPoint& point : kFaderCurve) {
    SCOPED_TRACE(point.value);
    output.SetValue(point.value);
    std::vector<std::vector<uint8_t>> sent = TakeSent();
    ASSERT_THAT(sent, ElementsAre(ElementsAre(0xE2, _, _)));
    EXPECT_NEAR(GetPosition(sent[0]), point.position, 1);
  }
}

TEST_F(ControlOutputMidiTest, McuFadersAreTheirChannels) {
  EXPECT_EQ(ControlCValueOutputMcuFader::Track(5).channel, 5);

  ControlCValueOutputMcuFader output(
      output_, ControlCValueOutputMcuFader::MasterFader());
  output.SetValue(1.0);
  EXPECT_THAT(TakeSent(), ElementsAre(ElementsAre(0xE8, 0x7F, 0x7F)));
}

}  // namespace
}  // namespace jpr
