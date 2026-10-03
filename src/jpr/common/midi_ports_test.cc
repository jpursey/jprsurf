// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_ports.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_message.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// Records the messages a MIDI input passes it.
class TestListener final : public MidiListener {
 public:
  void OnMidiMessage(double time, const MidiMessage& message) override {
    messages.push_back(message);
  }

  std::vector<MidiMessage> messages;
};

class MidiPortsTest : public ::testing::Test {
 protected:
  MidiPortsTest() { fake_output_->SetRecording(true); }

  FakeReaper reaper_;
  FakeMidiInput* fake_input_ = reaper_.AddMidiInput("X-Touch");
  FakeMidiOutput* fake_output_ = reaper_.AddMidiOutput("X-Touch");
};

TEST_F(MidiPortsTest, OpensPortsByName) {
  reaper_.AddMidiInput("X-Touch-Ext");
  MidiPorts ports;

  MidiIn* input = ports.OpenInput("X-Touch-Ext");
  ASSERT_NE(input, nullptr);
  EXPECT_EQ(input->GetName(), "X-Touch-Ext");
  EXPECT_EQ(input->GetIndex(), 1);
  EXPECT_FALSE(fake_input_->IsOpen());

  MidiOut* output = ports.OpenOutput("X-Touch");
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->GetName(), "X-Touch");
  EXPECT_TRUE(fake_output_->IsOpen());

  EXPECT_EQ(ports.OpenInput("Keys"), nullptr);
  EXPECT_EQ(ports.OpenOutput("X-Touch-Ext"), nullptr);
}

// The fake fails the test if a port is created again while it is open.
TEST_F(MidiPortsTest, ReopeningAPortReturnsTheSamePort) {
  MidiPorts ports;
  MidiIn* input = ports.OpenInput("X-Touch");
  MidiOut* output = ports.OpenOutput("X-Touch");
  EXPECT_EQ(ports.OpenInput("X-Touch"), input);
  EXPECT_EQ(ports.OpenOutput("X-Touch"), output);
}

TEST_F(MidiPortsTest, RunsTheOpenPorts) {
  MidiPorts ports;
  MidiIn* input = ports.OpenInput("X-Touch");
  MidiOut* output = ports.OpenOutput("X-Touch");
  TestListener listener;
  input->Subscribe(&listener, 0x90);

  fake_input_->Send(MidiMessage{0x90, 60, 127});
  ports.RunInput(RunTime::Now());
  EXPECT_THAT(listener.messages, ElementsAre(MidiMessage{0x90, 60, 127}));

  output->QueueMessage(MidiMessage{0x90, 60, 127});
  EXPECT_THAT(fake_output_->TakeReceived(), IsEmpty());
  ports.RunOutput(RunTime::Now());
  EXPECT_THAT(fake_output_->TakeReceived(),
              ElementsAre(ElementsAre(0x90, 60, 127)));
  input->Unsubscribe(&listener);
}

TEST_F(MidiPortsTest, ClosesThePortsWhenDestroyed) {
  {
    MidiPorts ports;
    ports.OpenInput("X-Touch");
    ports.OpenOutput("X-Touch");
  }
  EXPECT_FALSE(fake_input_->IsOpen());
  EXPECT_FALSE(fake_output_->IsOpen());
}

// The fake sends at once, so MidiPorts doesn't wait for it.
TEST_F(MidiPortsTest, SendsQueuedOutputWhenDestroyed) {
  {
    MidiPorts ports;
    ports.OpenOutput("X-Touch")->QueueMessage(MidiMessage{0xB0, 7, 0});
  }
  EXPECT_THAT(fake_output_->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 7, 0)));
}

}  // namespace
}  // namespace jpr
