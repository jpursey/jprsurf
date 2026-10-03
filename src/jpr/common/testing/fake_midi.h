// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/types/span.h"
#include "jpr/common/midi_message.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// FakeMidiEventList
//
// The MIDI_eventlist a FakeMidiInput delivers its events in. Each event is
// kept whole, and EnumItems() steps through them in order. JPRSurf only reads
// it, so the rest isn't faked yet.
//==============================================================================

class FakeMidiEventList final : public MIDI_eventlist {
 public:
  FakeMidiEventList() = default;
  FakeMidiEventList(const FakeMidiEventList&) = delete;
  FakeMidiEventList& operator=(const FakeMidiEventList&) = delete;
  ~FakeMidiEventList() override = default;

  // MIDI_eventlist
  void AddItem(MIDI_event_t* event) override;
  MIDI_event_t* EnumItems(int* bpos) override;
  void DeleteItem(int bpos) override;
  int GetSize() override;
  void Empty() override;

  // Adds an event with `bytes`, at `frame_offset`.
  void Add(absl::Span<const uint8_t> bytes, int frame_offset);

 private:
  // Each event, laid out as a MIDI_event_t with its bytes.
  std::vector<std::vector<uint8_t>> events_;
};

//==============================================================================
// FakeMidiInput
//
// A MIDI input port in the fake REAPER, which FakeReaper::AddMidiInput()
// lists. CreateMIDIInput() returns it, and Destroy() closes it, as the fake
// owns it.
//
// A test sends messages into it, as the hardware does. Each is delivered on
// the next SwapBufsPrecise(), at the time it was sent. A port that isn't open
// and started drops them, as nothing is listening.
//
// It fails the test if it is still open when FakeReaper (which owns it) is
// destroyed.
//==============================================================================

class FakeMidiInput final : public midi_Input {
 public:
  FakeMidiInput(const FakeMidiInput&) = delete;
  FakeMidiInput& operator=(const FakeMidiInput&) = delete;
  ~FakeMidiInput() override;

  // The name REAPER lists the port under.
  const std::string& GetName() const { return name_; }

  // Returns true if the port is open: created, and not yet destroyed.
  bool IsOpen() const { return open_; }

  // Sends a message into the port, or a sysex message's bytes, at the fake's
  // current time.
  void Send(const MidiMessage& message);
  void Send(absl::Span<const uint8_t> bytes);

  // midi_Input
  void start() override { started_ = true; }
  void stop() override { started_ = false; }
  void SwapBufs(unsigned int timestamp) override;
  MIDI_eventlist* GetReadBuf() override { return &read_buffer_; }
  void SwapBufsPrecise(unsigned int coarse_timestamp,
                       double precise_timestamp) override;
  void Destroy() override;

 private:
  friend class FakeReaper;

  // An event sent into the port, and the time it was sent.
  struct Event {
    std::vector<uint8_t> bytes;
    double time = 0.0;
  };

  explicit FakeMidiInput(std::string_view name) : name_(name) {}

  const std::string name_;
  bool open_ = false;
  bool started_ = false;
  std::vector<Event> sent_;  // Since the last SwapBufsPrecise().
  FakeMidiEventList read_buffer_;
};

//==============================================================================
// FakeMidiOutput
//
// A MIDI output port in the fake REAPER, which FakeReaper::AddMidiOutput()
// lists. CreateMIDIOutput() returns it, and Destroy() closes it, as the fake
// owns it.
//
// The bytes of each message sent from it, as the hardware would receive them,
// go to the fake hardware connected to it, if there is one (see Connect()),
// and are recorded for TakeReceived() if the port is recording (see
// SetRecording()). Each is independent of the other.
//
// It fails the test if it is still open when FakeReaper (which owns it) is
// destroyed.
//==============================================================================

class FakeMidiOutput final : public midi_Output {
 public:
  FakeMidiOutput(const FakeMidiOutput&) = delete;
  FakeMidiOutput& operator=(const FakeMidiOutput&) = delete;
  ~FakeMidiOutput() override;

  // The name REAPER lists the port under.
  const std::string& GetName() const { return name_; }

  // Returns true if the port is open: created, and not yet destroyed.
  bool IsOpen() const { return open_; }

  // Connects fake hardware to the port: each message sent from it is passed to
  // `receiver` as it is sent. A port has at most one device, so connecting one
  // while another is connected fails the test, and leaves the first connected.
  //
  // The hardware stays connected whether or not the port is open, until
  // Disconnect().
  void Connect(
      absl::AnyInvocable<void(absl::Span<const uint8_t> bytes)> receiver);
  void Disconnect() { receiver_ = nullptr; }

  // Sets whether the port records each message sent from it, for
  // TakeReceived(). It doesn't until a test asks it to. Stopping forgets what
  // was recorded.
  void SetRecording(bool recording);

  // Returns the bytes of each message recorded since the last call, in order,
  // and forgets them. Taking them from a port that isn't recording fails the
  // test.
  std::vector<std::vector<uint8_t>> TakeReceived();

  // midi_Output
  void SendMsg(MIDI_event_t* message, int frame_offset) override;
  void Send(unsigned char status, unsigned char data1, unsigned char data2,
            int frame_offset) override;
  void Destroy() override;

 private:
  friend class FakeReaper;

  explicit FakeMidiOutput(std::string_view name) : name_(name) {}

  // Passes a message sent from the port to the hardware, and records it.
  void Receive(absl::Span<const uint8_t> bytes);

  const std::string name_;
  bool open_ = false;
  absl::AnyInvocable<void(absl::Span<const uint8_t> bytes)> receiver_;
  bool recording_ = false;
  std::vector<std::vector<uint8_t>> received_;
};

}  // namespace jpr
