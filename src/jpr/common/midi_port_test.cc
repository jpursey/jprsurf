// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_port.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_message.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/midi_sysex.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

constexpr uint8_t kTestSysexPrefix[] = {0x00, 0x20, 0x32};

// A message, and the time a listener received it.
struct Received {
  double time;
  MidiMessage message;
};

// Records the messages a MIDI input passes it.
class TestListener final : public MidiListener {
 public:
  void OnMidiMessage(double time, const MidiMessage& message) override {
    received.push_back({time, message});
  }

  // Returns the messages received, without their times.
  std::vector<MidiMessage> GetMessages() const {
    std::vector<MidiMessage> messages;
    for (const Received& entry : received) {
      messages.push_back(entry.message);
    }
    return messages;
  }

  std::vector<Received> received;
};

// A sysex state that changes when a message's bytes do.
class TestSysexState final : public SysexMessageState {
 public:
  explicit TestSysexState(absl::Span<const uint8_t> bytes)
      : bytes_(bytes.begin(), bytes.end()) {}

  std::unique_ptr<SysexMessageState> Clone() const override {
    return std::make_unique<TestSysexState>(bytes_);
  }

  bool Update(const SysexMessage& message) override {
    std::vector<uint8_t> bytes(message.GetBytes().begin(),
                               message.GetBytes().end());
    if (bytes == bytes_) {
      return false;
    }
    bytes_ = std::move(bytes);
    return true;
  }

 private:
  std::vector<uint8_t> bytes_;
};

class TestSysexType final : public SysexMessageType {
 public:
  TestSysexType() : SysexMessageType(SysexPrefix(kTestSysexPrefix)) {}

  std::unique_ptr<SysexMessageState> CreateState(
      const SysexMessage& message) const override {
    return std::make_unique<TestSysexState>(message.GetBytes());
  }
};

// Returns a test sysex message with one data byte, `value`.
SysexMessage MakeSysex(uint8_t value) {
  SysexMessage message(SysexPrefix(kTestSysexPrefix), 1);
  message.GetMutableData()[0] = value;
  return message;
}

class MidiPortTest : public ::testing::Test {
 protected:
  // Delivers what was sent into the input to its listeners.
  void RunInput() { ports_.RunInput(RunTime::Now()); }

  // Sends what is queued on the output, and returns what it sent.
  std::vector<std::vector<uint8_t>> RunOutput() {
    ports_.RunOutput(RunTime::Now());
    return fake_output_->TakeReceived();
  }

  FakeReaper reaper_;
  FakeMidiInput* fake_input_ = reaper_.AddMidiInput("X-Touch");
  FakeMidiOutput* fake_output_ = reaper_.AddMidiOutput("X-Touch");
  MidiPorts ports_;
  MidiIn* input_ = ports_.OpenInput("X-Touch");
  MidiOut* output_ = ports_.OpenOutput("X-Touch");
};

//------------------------------------------------------------------------------
// MidiIn
//------------------------------------------------------------------------------

TEST_F(MidiPortTest, ListenersGetMessagesByStatus) {
  TestListener listener;
  input_->Subscribe(&listener, 0x90);

  fake_input_->Send(MidiMessage{0x90, 60, 127});
  fake_input_->Send(MidiMessage{0x91, 60, 127});
  fake_input_->Send(MidiMessage{0x90, 61, 0});
  fake_input_->Send(MidiMessage{0xB0, 60, 1});
  RunInput();
  EXPECT_THAT(listener.GetMessages(), ElementsAre(MidiMessage{0x90, 60, 127},
                                                  MidiMessage{0x90, 61, 0}));
  input_->Unsubscribe(&listener);
}

TEST_F(MidiPortTest, ListenersGetMessagesByStatusAndData) {
  TestListener listener;
  input_->Subscribe(&listener, 0xB0, 16);
  input_->Subscribe(&listener, 0xB0, 17);

  fake_input_->Send(MidiMessage{0xB0, 16, 1});
  fake_input_->Send(MidiMessage{0xB0, 18, 1});
  fake_input_->Send(MidiMessage{0xB0, 17, 65});
  fake_input_->Send(MidiMessage{0xB1, 16, 1});
  RunInput();
  EXPECT_THAT(listener.GetMessages(),
              ElementsAre(MidiMessage{0xB0, 16, 1}, MidiMessage{0xB0, 17, 65}));
  input_->Unsubscribe(&listener);
}

TEST_F(MidiPortTest, UnsubscribedListenersGetNothing) {
  TestListener listener;
  input_->Subscribe(&listener, 0x90);
  input_->Subscribe(&listener, 0xB0, 16);
  input_->Unsubscribe(&listener);

  fake_input_->Send(MidiMessage{0x90, 60, 127});
  fake_input_->Send(MidiMessage{0xB0, 16, 1});
  RunInput();
  EXPECT_THAT(listener.received, IsEmpty());
}

TEST_F(MidiPortTest, MessagesHaveTheTimeTheyWereSent) {
  TestListener listener;
  input_->Subscribe(&listener, 0x90);

  const double first_time = ::time_precise();
  fake_input_->Send(MidiMessage{0x90, 60, 127});
  reaper_.AdvanceTime(absl::Milliseconds(10));
  fake_input_->Send(MidiMessage{0x90, 60, 0});
  reaper_.AdvanceTime(absl::Milliseconds(20));
  RunInput();

  // Event times are in 1/1024000s, so they round to within a microsecond.
  EXPECT_THAT(
      listener.received,
      ElementsAre(Field(&Received::time, DoubleNear(first_time, 1e-6)),
                  Field(&Received::time, DoubleNear(first_time + 0.01, 1e-6))));
  input_->Unsubscribe(&listener);
}

TEST_F(MidiPortTest, SysexInputIsIgnored) {
  TestListener listener;
  input_->Subscribe(&listener, 0xF0);
  input_->Subscribe(&listener, 0x90);

  const uint8_t sysex[] = {0xF0, 0x00, 0x20, 0x32, 0x01, 0xF7};
  fake_input_->Send(sysex);
  fake_input_->Send(MidiMessage{0x90, 60, 127});
  RunInput();
  EXPECT_THAT(listener.GetMessages(), ElementsAre(MidiMessage{0x90, 60, 127}));
  input_->Unsubscribe(&listener);
}

//------------------------------------------------------------------------------
// MidiOut
//------------------------------------------------------------------------------

TEST_F(MidiPortTest, QueuedMessagesAreAlwaysSentInOrder) {
  output_->QueueMessage(MidiMessage{0x90, 60, 127});
  output_->QueueMessage(MidiMessage{0x90, 60, 127});
  output_->QueueMessage(MidiMessage{0x90, 61, 0});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x90, 60, 127),
                                       ElementsAre(0x90, 60, 127),
                                       ElementsAre(0x90, 61, 0)));
  EXPECT_THAT(RunOutput(), IsEmpty());
}

TEST_F(MidiPortTest, StateIsSentOnlyWhenItChanges) {
  output_->UpdateState(MidiMessage{0x90, 60, 127});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x90, 60, 127)));
  output_->UpdateState(MidiMessage{0x90, 60, 127});
  EXPECT_THAT(RunOutput(), IsEmpty());

  // A note's velocity is part of its state, but every off is the same.
  output_->UpdateState(MidiMessage{0x90, 60, 1});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x90, 60, 1)));
  output_->UpdateState(MidiMessage{0x80, 60, 0});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x80, 60, 0)));
  output_->UpdateState(MidiMessage{0x90, 60, 0});
  EXPECT_THAT(RunOutput(), IsEmpty());
}

TEST_F(MidiPortTest, OnlyTheLatestStateIsSent) {
  output_->UpdateState(MidiMessage{0xB0, 7, 10});
  output_->UpdateState(MidiMessage{0xB0, 7, 20});
  output_->UpdateState(MidiMessage{0xE0, 0, 64});
  EXPECT_THAT(RunOutput(), UnorderedElementsAre(ElementsAre(0xB0, 7, 20),
                                                ElementsAre(0xE0, 0, 64)));

  // A change set back before it is sent isn't sent.
  output_->UpdateState(MidiMessage{0xB0, 7, 30});
  output_->UpdateState(MidiMessage{0xB0, 7, 20});
  EXPECT_THAT(RunOutput(), IsEmpty());
}

TEST_F(MidiPortTest, QueuedMessagesAreSentAheadOfStateAndUpdateIt) {
  output_->UpdateState(MidiMessage{0xB0, 7, 10});
  output_->QueueMessage(MidiMessage{0x90, 60, 127});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x90, 60, 127),
                                       ElementsAre(0xB0, 7, 10)));

  // The queued message replaces the pending state, and is the state sent.
  output_->UpdateState(MidiMessage{0xB0, 7, 20});
  output_->QueueMessage(MidiMessage{0xB0, 7, 30});
  output_->UpdateState(MidiMessage{0xB0, 7, 30});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0xB0, 7, 30)));
}

TEST_F(MidiPortTest, ResetStateSendsItAgain) {
  output_->UpdateState(MidiMessage{0x90, 60, 127});
  output_->UpdateState(MidiMessage{0xB0, 7, 10});
  RunOutput();

  output_->ResetNoteState(0, 60);
  output_->UpdateState(MidiMessage{0x90, 60, 127});
  output_->UpdateState(MidiMessage{0xB0, 7, 10});
  EXPECT_THAT(RunOutput(), ElementsAre(ElementsAre(0x90, 60, 127)));

  output_->ResetAllState();
  output_->UpdateState(MidiMessage{0x90, 60, 127});
  output_->UpdateState(MidiMessage{0xB0, 7, 10});
  EXPECT_THAT(RunOutput(), UnorderedElementsAre(ElementsAre(0x90, 60, 127),
                                                ElementsAre(0xB0, 7, 10)));
}

TEST_F(MidiPortTest, QueuedSysexIsAlwaysSent) {
  output_->QueueMessage(MakeSysex(1));
  output_->QueueMessage(MakeSysex(1));
  EXPECT_THAT(RunOutput(),
              ElementsAre(ElementsAre(0xF0, 0x00, 0x20, 0x32, 1, 0xF7),
                          ElementsAre(0xF0, 0x00, 0x20, 0x32, 1, 0xF7)));
}

TEST_F(MidiPortTest, SysexStateIsSentOnlyWhenItChanges) {
  TestSysexType type;
  ASSERT_TRUE(type.Register());

  output_->UpdateState(MakeSysex(1));
  EXPECT_THAT(RunOutput(),
              ElementsAre(ElementsAre(0xF0, 0x00, 0x20, 0x32, 1, 0xF7)));
  output_->UpdateState(MakeSysex(1));
  EXPECT_THAT(RunOutput(), IsEmpty());
  output_->UpdateState(MakeSysex(2));
  EXPECT_THAT(RunOutput(),
              ElementsAre(ElementsAre(0xF0, 0x00, 0x20, 0x32, 2, 0xF7)));
}

}  // namespace
}  // namespace jpr
