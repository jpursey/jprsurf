// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_midi.h"

#include <array>
#include <cstdint>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

const uint8_t kSysex[] = {0xF0, 0x00, 0x20, 0x32, 0x01, 0xF7};

TEST(FakeMidiTest, ListsPortsByName) {
  FakeReaper reaper;
  reaper.AddMidiInput("X-Touch");
  reaper.AddMidiInput("Keys");
  reaper.AddMidiOutput("X-Touch");

  EXPECT_EQ(::GetNumMIDIInputs(), 2);
  EXPECT_EQ(::GetNumMIDIOutputs(), 1);
  std::array<char, 64> name = {};
  EXPECT_TRUE(::GetMIDIInputName(1, name.data(), name.size()));
  EXPECT_STREQ(name.data(), "Keys");
  EXPECT_TRUE(::GetMIDIOutputName(0, name.data(), name.size()));
  EXPECT_STREQ(name.data(), "X-Touch");
  EXPECT_FALSE(::GetMIDIInputName(2, name.data(), name.size()));

  // A name that doesn't fit is cut short.
  std::array<char, 4> short_name = {};
  EXPECT_TRUE(::GetMIDIInputName(0, short_name.data(), short_name.size()));
  EXPECT_STREQ(short_name.data(), "X-T");
}

TEST(FakeMidiTest, CreatingAPortOpensItUntilItIsDestroyed) {
  FakeReaper reaper;
  FakeMidiInput* input = reaper.AddMidiInput("X-Touch");
  FakeMidiOutput* output = reaper.AddMidiOutput("X-Touch");
  EXPECT_FALSE(input->IsOpen());

  EXPECT_EQ(::CreateMIDIInput(0), input);
  EXPECT_EQ(::CreateMIDIOutput(0, false, nullptr), output);
  EXPECT_TRUE(input->IsOpen());
  EXPECT_TRUE(output->IsOpen());
  EXPECT_EQ(::CreateMIDIInput(1), nullptr);

  input->Destroy();
  output->Destroy();
  EXPECT_FALSE(input->IsOpen());
  EXPECT_FALSE(output->IsOpen());

  // A closed port opens again.
  EXPECT_EQ(::CreateMIDIInput(0), input);
  input->Destroy();
}

TEST(FakeMidiTest, CreatingAnOpenPortFailsTheTest) {
  FakeReaper reaper;
  FakeMidiOutput* output = reaper.AddMidiOutput("X-Touch");
  ::CreateMIDIOutput(0, false, nullptr);
  EXPECT_NONFATAL_FAILURE(::CreateMIDIOutput(0, false, nullptr),
                          "was created while it is open");
  output->Destroy();
}

TEST(FakeMidiTest, InputDeliversEventsAtTheTimeTheyWereSent) {
  FakeReaper reaper;
  FakeMidiInput* input = reaper.AddMidiInput("X-Touch");
  ::CreateMIDIInput(0)->start();

  input->Send(MidiMessage{0x90, 60, 127});
  reaper.AdvanceTime(absl::Milliseconds(10));
  input->Send(kSysex);
  input->SwapBufsPrecise(0, ::time_precise());

  MIDI_eventlist* events = input->GetReadBuf();
  int bpos = 0;
  MIDI_event_t* event = events->EnumItems(&bpos);
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(event->frame_offset, -10240);  // 10ms before, in 1/1024000s.
  EXPECT_THAT(absl::MakeConstSpan(event->midi_message, event->size),
              ElementsAre(0x90, 60, 127));
  event = events->EnumItems(&bpos);
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(event->frame_offset, 0);
  EXPECT_EQ(absl::MakeConstSpan(event->midi_message, event->size),
            absl::MakeConstSpan(kSysex));
  EXPECT_EQ(events->EnumItems(&bpos), nullptr);

  // Each event is delivered once.
  input->SwapBufsPrecise(0, ::time_precise());
  bpos = 0;
  EXPECT_EQ(input->GetReadBuf()->EnumItems(&bpos), nullptr);
  input->Destroy();
}

TEST(FakeMidiTest, InputDropsEventsUntilItIsStarted) {
  FakeReaper reaper;
  FakeMidiInput* input = reaper.AddMidiInput("X-Touch");
  input->Send(MidiMessage{0x90, 60, 127});
  ::CreateMIDIInput(0);
  input->Send(MidiMessage{0x90, 61, 127});
  input->start();
  input->Send(MidiMessage{0x90, 62, 127});
  input->SwapBufsPrecise(0, ::time_precise());

  int bpos = 0;
  MIDI_event_t* event = input->GetReadBuf()->EnumItems(&bpos);
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(event->midi_message[1], 62);
  EXPECT_EQ(input->GetReadBuf()->EnumItems(&bpos), nullptr);
  input->Destroy();
}

TEST(FakeMidiTest, OutputRecordsWhatItReceives) {
  FakeReaper reaper;
  FakeMidiOutput* output = reaper.AddMidiOutput("X-Touch");
  ::CreateMIDIOutput(0, false, nullptr);

  // A sysex event, built as REAPER lays it out.
  FakeMidiEventList sysex;
  sysex.Add(kSysex, -1);
  int bpos = 0;
  output->Send(0xB0, 7, 100, -1);
  output->SendMsg(sysex.EnumItems(&bpos), -1);
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 7, 100),
                          ElementsAre(0xF0, 0x00, 0x20, 0x32, 0x01, 0xF7)));
  EXPECT_THAT(output->TakeReceived(), IsEmpty());
  output->Destroy();
}

TEST(FakeMidiTest, UsingAPortAfterItIsDestroyedFailsTheTest) {
  FakeReaper reaper;
  FakeMidiOutput* output = reaper.AddMidiOutput("X-Touch");
  ::CreateMIDIOutput(0, false, nullptr);
  output->Destroy();
  EXPECT_NONFATAL_FAILURE(output->Send(0xB0, 7, 100, -1),
                          "was used after Destroy()");
}

TEST(FakeMidiTest, PortLeftOpenFailsTheTest) {
  EXPECT_NONFATAL_FAILURE(
      {
        FakeReaper reaper;
        reaper.AddMidiInput("X-Touch");
        ::CreateMIDIInput(0);
      },
      "MIDI input \"X-Touch\" is still open");
}

}  // namespace
}  // namespace jpr
