// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/control_input_midi.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "jpr/common/midi_message.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

// Each input listens on an input port, which the fake's port sends into.
class ControlInputMidiTest : public ::testing::Test {
 protected:
  // Sends `message` into the port, and delivers it.
  void Send(const MidiMessage& message) {
    fake_input_->Send(message);
    ports_.RunInput(RunTime::Now());
  }

  FakeReaper reaper_;
  FakeMidiInput* fake_input_ = reaper_.AddMidiInput("Device");
  MidiPorts ports_;
  MidiIn* input_ = ports_.OpenInput("Device");
};

//==============================================================================
// ControlPressInputMidiMsg
//==============================================================================

TEST_F(ControlInputMidiTest, PressAndReleaseAreTheirMessages) {
  ControlPressInputMidiMsg input(input_,
                                 {.press = MidiNoteOn(1, 0x20, 0x7F),
                                  .release = MidiNoteOff(1, 0x20, 0x40)});
  EXPECT_TRUE(input.HasRelease());

  Send(MidiNoteOn(1, 0x20, 0x7F));
  EXPECT_TRUE(input.IsPressed());
  Send(MidiNoteOff(1, 0x20, 0x40));
  EXPECT_FALSE(input.IsPressed());
  EXPECT_EQ(input.GetPressCount(), 1);
  EXPECT_EQ(input.GetReleaseCount(), 1);
}

// A message must match in its status, and both data bytes.
TEST_F(ControlInputMidiTest, OtherMessagesArentPresses) {
  ControlPressInputMidiMsg input(input_,
                                 {.press = MidiNoteOn(1, 0x20, 0x7F),
                                  .release = MidiNoteOn(1, 0x20, 0x00)});

  Send(MidiNoteOn(0, 0x20, 0x7F));  // Channel.
  Send(MidiNoteOn(1, 0x21, 0x7F));  // Note.
  Send(MidiNoteOn(1, 0x20, 0x01));  // Velocity.
  Send(MidiCc(1, 0x20, 0x7F));      // Status.
  EXPECT_EQ(input.GetPressCount(), 0);
}

TEST_F(ControlInputMidiTest, PressWithoutReleaseMessageIsNeverHeld) {
  ControlPressInputMidiMsg input(input_, {.press = MidiCc(0, 0x40, 0x7F)});
  EXPECT_FALSE(input.HasRelease());

  Send(MidiCc(0, 0x40, 0x7F));
  Send(MidiCc(0, 0x40, 0x7F));
  EXPECT_EQ(input.GetPressCount(), 2);
  EXPECT_FALSE(input.IsPressed());
}

TEST_F(ControlInputMidiTest, McuButtonIsItsNoteOnAndOff) {
  ControlPressInputMidiMsg::Config config =
      ControlPressInputMidiMsg::McuButton(0x5E);
  EXPECT_EQ(config.press, MidiNoteOn(0, 0x5E, 0x7F));
  EXPECT_EQ(config.release, MidiNoteOn(0, 0x5E, 0x00));
}

TEST_F(ControlInputMidiTest, McuFaderTouchesAreTheirNotes) {
  EXPECT_EQ(ControlPressInputMidiMsg::McuFaderTouch(0).press,
            MidiNoteOn(0, 0x68, 0x7F));
  EXPECT_EQ(ControlPressInputMidiMsg::McuFaderTouch(7).press,
            MidiNoteOn(0, 0x6F, 0x7F));
  EXPECT_EQ(ControlPressInputMidiMsg::McuMasterFaderTouch().press,
            MidiNoteOn(0, 0x70, 0x7F));
  EXPECT_EQ(ControlPressInputMidiMsg::McuMasterFaderTouch().release,
            MidiNoteOn(0, 0x70, 0x00));

  // A track past the eighth is the eighth.
  EXPECT_EQ(ControlPressInputMidiMsg::McuFaderTouch(9).press,
            MidiNoteOn(0, 0x6F, 0x7F));
}

//==============================================================================
// ControlDeltaInputMidiCcSignMagnitude
//==============================================================================

// Bit 6 is the sign, and bits 0-5 the size, so 63 either way is 1.0.
TEST_F(ControlInputMidiTest, DeltaIsSignAndMagnitude) {
  ControlDeltaInputMidiCcSignMagnitude input(input_,
                                             {.channel = 1, .control = 0x10});
  struct Case {
    uint8_t value;
    double delta;
  };
  constexpr Case kCases[] = {
      {0x01, 1.0 / 63}, {0x3F, 1.0}, {0x41, -1.0 / 63},
      {0x7F, -1.0},     {0x00, 0.0}, {0x40, 0.0},
  };
  for (const Case& entry : kCases) {
    SCOPED_TRACE(entry.value);
    Send(MidiCc(1, 0x10, entry.value));
    EXPECT_DOUBLE_EQ(input.ReadDelta(), entry.delta);
  }
}

TEST_F(ControlInputMidiTest, DeltasAddUpAndAreScaled) {
  ControlDeltaInputMidiCcSignMagnitude input(
      input_, {.channel = 0, .control = 0x10, .scaling = 0.5});
  Send(MidiCc(0, 0x10, 0x03));
  Send(MidiCc(0, 0x10, 0x41));
  EXPECT_DOUBLE_EQ(input.ReadDelta(), 1.0);
}

TEST_F(ControlInputMidiTest, DeltaIsOnlyItsControlAndChannel) {
  ControlDeltaInputMidiCcSignMagnitude input(input_,
                                             {.channel = 1, .control = 0x10});
  Send(MidiCc(0, 0x10, 0x01));
  Send(MidiCc(1, 0x11, 0x01));
  Send(MidiNoteOn(1, 0x10, 0x01));
  EXPECT_EQ(input.ReadDelta(), 0.0);
}

TEST_F(ControlInputMidiTest, McuEncoderIsItsControl) {
  ControlDeltaInputMidiCcSignMagnitude::Config config =
      ControlDeltaInputMidiCcSignMagnitude::McuEncoder(3);
  EXPECT_EQ(config.channel, 0);
  EXPECT_EQ(config.control, 0x13);
  EXPECT_EQ(config.scaling, 1.0 / 63);

  // A track past the eighth is the eighth.
  EXPECT_EQ(ControlDeltaInputMidiCcSignMagnitude::McuEncoder(9).control, 0x17);
  EXPECT_EQ(ControlDeltaInputMidiCcSignMagnitude::McuEncoder(3, 0.5).scaling,
            0.5);
}

//==============================================================================
// ControlValueInputMcuFader
//==============================================================================

// Points on the MCU fader curve: a fader's position, and the value it is.
struct FaderPoint {
  uint16_t position;
  double value;
};

constexpr FaderPoint kFaderCurve[] = {
    {0, 0.0},                              // -inf
    {530, 0.000316228},                    // -60 dB
    {8250, 0.1},                           // -10 dB
    {12720, 0.316228},                     // 0 dB
    {13810, (0.316228 + 0.562341) / 2.0},  // Halfway from 0 dB to +5 dB.
    {16383, 1.0},                          // +10 dB
};

TEST_F(ControlInputMidiTest, FaderFollowsTheMcuCurve) {
  ControlValueInputMcuFader input(input_, ControlValueInputMcuFader::Track(2));
  for (const FaderPoint& point : kFaderCurve) {
    SCOPED_TRACE(point.position);
    Send(MidiPitchBend(2, point.position));
    EXPECT_NEAR(input.GetValue(), point.value, 1e-9);
  }
}

TEST_F(ControlInputMidiTest, FaderIsOnlyItsChannel) {
  ControlValueInputMcuFader input(input_, ControlValueInputMcuFader::Track(2));
  Send(MidiPitchBend(1, 16383));
  Send(MidiCc(2, 0, 0x7F));
  EXPECT_EQ(input.GetValue(), 0.0);
}

TEST_F(ControlInputMidiTest, McuFadersAreTheirChannels) {
  EXPECT_EQ(ControlValueInputMcuFader::Track(5).channel, 5);
  EXPECT_EQ(ControlValueInputMcuFader::MasterFader().channel, 8);
}

}  // namespace
}  // namespace jpr
