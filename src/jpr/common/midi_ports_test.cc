// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_ports.h"

#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "gb/test/log_error_guard.h"
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
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

// Records the messages a MIDI input passes it.
class TestListener final : public MidiListener {
 public:
  void OnMidiMessage(double time_secs, const MidiMessage& message) override {
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

  MidiInHandle input = ports.OpenInput("X-Touch-Ext");
  ASSERT_TRUE(input.IsOpen());
  EXPECT_EQ(input->GetName(), "X-Touch-Ext");
  EXPECT_EQ(input->GetIndex(), 1);
  EXPECT_FALSE(fake_input_->IsOpen());

  MidiOutHandle output = ports.OpenOutput("X-Touch");
  ASSERT_TRUE(output.IsOpen());
  EXPECT_EQ(output->GetName(), "X-Touch");
  EXPECT_TRUE(fake_output_->IsOpen());

  EXPECT_FALSE(ports.OpenInput("Keys").IsOpen());
  EXPECT_FALSE(ports.OpenOutput("X-Touch-Ext").IsOpen());
}

TEST_F(MidiPortsTest, HasPortsByNameWithoutOpeningThem) {
  reaper_.AddMidiInput("X-Touch-Ext");
  MidiPorts ports;

  EXPECT_TRUE(ports.HasInput("X-Touch"));
  EXPECT_TRUE(ports.HasInput("X-Touch-Ext"));
  EXPECT_TRUE(ports.HasOutput("X-Touch"));
  EXPECT_FALSE(ports.HasOutput("X-Touch-Ext"));
  EXPECT_FALSE(ports.HasInput("Keys"));
  EXPECT_FALSE(fake_input_->IsOpen());
  EXPECT_FALSE(fake_output_->IsOpen());

  MidiInHandle input = ports.OpenInput("X-Touch");
  EXPECT_TRUE(ports.HasInput("X-Touch"));
}

// The fake fails the test if a port is created again while it is open.
TEST_F(MidiPortsTest, ReopeningAPortGivesAnotherHandleToTheSamePort) {
  MidiPorts ports;
  MidiInHandle input = ports.OpenInput("X-Touch");
  MidiOutHandle output = ports.OpenOutput("X-Touch");
  EXPECT_EQ(ports.OpenInput("X-Touch").Get(), input.Get());
  EXPECT_EQ(ports.OpenOutput("X-Touch").Get(), output.Get());
}

TEST_F(MidiPortsTest, PortClosesWithItsLastHandle) {
  MidiPorts ports;
  MidiInHandle first_input = ports.OpenInput("X-Touch");
  MidiInHandle second_input = ports.OpenInput("X-Touch");
  MidiOutHandle first_output = ports.OpenOutput("X-Touch");
  MidiOutHandle second_output = ports.OpenOutput("X-Touch");

  first_input = {};
  first_output = {};
  EXPECT_TRUE(fake_input_->IsOpen());
  EXPECT_TRUE(fake_output_->IsOpen());

  second_input = {};
  second_output = {};
  EXPECT_FALSE(fake_input_->IsOpen());
  EXPECT_FALSE(fake_output_->IsOpen());

  // A closed port opens again.
  MidiInHandle input = ports.OpenInput("X-Touch");
  EXPECT_TRUE(fake_input_->IsOpen());
}

TEST_F(MidiPortsTest, MovedHandleKeepsThePortOpen) {
  MidiPorts ports;
  MidiInHandle input = ports.OpenInput("X-Touch");
  MidiIn* const port = input.Get();

  MidiInHandle moved(std::move(input));
  EXPECT_FALSE(input.IsOpen());
  EXPECT_EQ(moved.Get(), port);
  EXPECT_TRUE(fake_input_->IsOpen());

  MidiInHandle assigned;
  assigned = std::move(moved);
  EXPECT_FALSE(moved.IsOpen());
  EXPECT_EQ(assigned.Get(), port);
  EXPECT_TRUE(fake_input_->IsOpen());

  // Assigning over a handle lets go of its port.
  assigned = MidiInHandle();
  EXPECT_FALSE(fake_input_->IsOpen());
}

TEST_F(MidiPortsTest, RunsTheOpenPorts) {
  MidiPorts ports;
  MidiInHandle input = ports.OpenInput("X-Touch");
  MidiOutHandle output = ports.OpenOutput("X-Touch");
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

TEST_F(MidiPortsTest, PortThatFailsToOpenIsNull) {
  fake_input_->SetOpenFails(true);
  MidiPorts ports;
  EXPECT_TRUE(ports.HasInput("X-Touch"));
  EXPECT_FALSE(ports.OpenInput("X-Touch").IsOpen());
}

// The fake sends at once, so MidiPorts doesn't wait for it.
TEST_F(MidiPortsTest, OutputQueuedAsItsLastHandleGoesIsSentWhenDestroyed) {
  {
    MidiPorts ports;
    ports.OpenOutput("X-Touch")->QueueMessage(MidiMessage{0xB0, 7, 0});
    EXPECT_TRUE(fake_output_->IsOpen());
  }
  EXPECT_THAT(fake_output_->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 7, 0)));
  EXPECT_FALSE(fake_output_->IsOpen());
}

// A state change still to send is queued output too.
TEST_F(MidiPortsTest, StateQueuedAsItsLastHandleGoesIsSentWhenDestroyed) {
  {
    MidiPorts ports;
    ports.OpenOutput("X-Touch")->UpdateState(MidiCc(0, 7, 64));
    EXPECT_TRUE(fake_output_->IsOpen());
  }
  EXPECT_THAT(fake_output_->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 7, 64)));
}

// The port waits the flush wait after it last sent, so the hardware receives
// it all.
TEST_F(MidiPortsTest, ReleasedOutputClosesOnceItIsDoneSending) {
  MidiPorts::SetFlushWait(absl::Milliseconds(100));
  MidiPorts ports;
  MidiOutHandle output = ports.OpenOutput("X-Touch");
  output->QueueMessage(MidiMessage{0xB0, 7, 0});
  output = {};
  EXPECT_TRUE(fake_output_->IsOpen());

  ports.RunOutput(RunTime::Now());
  EXPECT_THAT(fake_output_->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 7, 0)));
  EXPECT_TRUE(fake_output_->IsOpen());

  reaper_.AdvanceTime(absl::Milliseconds(50));
  ports.RunOutput(RunTime::Now());
  EXPECT_TRUE(fake_output_->IsOpen());

  reaper_.AdvanceTime(absl::Milliseconds(50));
  ports.RunOutput(RunTime::Now());
  EXPECT_FALSE(fake_output_->IsOpen());
  MidiPorts::SetFlushWait(absl::ZeroDuration());
}

// An output released soon after it sent waits too.
TEST_F(MidiPortsTest, OutputReleasedSoonAfterItSentClosesOnceItIsDone) {
  MidiPorts::SetFlushWait(absl::Milliseconds(100));
  MidiPorts ports;
  MidiOutHandle output = ports.OpenOutput("X-Touch");
  output->QueueMessage(MidiMessage{0xB0, 7, 0});
  ports.RunOutput(RunTime::Now());
  output = {};
  EXPECT_TRUE(fake_output_->IsOpen());

  reaper_.AdvanceTime(absl::Milliseconds(100));
  ports.RunOutput(RunTime::Now());
  EXPECT_FALSE(fake_output_->IsOpen());
  MidiPorts::SetFlushWait(absl::ZeroDuration());
}

// An output that sent nothing closes at once.
TEST_F(MidiPortsTest, ReleasedOutputThatSentNothingClosesAtOnce) {
  MidiPorts::SetFlushWait(absl::Milliseconds(100));
  MidiPorts ports;
  MidiOutHandle output = ports.OpenOutput("X-Touch");
  output = {};
  EXPECT_FALSE(fake_output_->IsOpen());
  MidiPorts::SetFlushWait(absl::ZeroDuration());
}

TEST_F(MidiPortsTest, HandleLeftLogsAnError) {
  gb::LogErrorGuard log_error_guard;
  {
    MidiPorts ports;

    // Never destroyed, as its port goes with MidiPorts.
    new MidiInHandle(ports.OpenInput("X-Touch"));
  }
  EXPECT_THAT(
      log_error_guard.TakeMessages(),
      ElementsAre(Field(&gb::LogRecorder::Message::text,
                        HasSubstr("\"X-Touch\" has 1 handle(s) left"))));
  EXPECT_FALSE(fake_input_->IsOpen());
}

}  // namespace
}  // namespace jpr
