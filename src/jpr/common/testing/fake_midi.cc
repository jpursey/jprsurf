// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_midi.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

namespace {

// The units of a MIDI_event_t's frame_offset in an input's read buffer:
// 1/1024000 of a second.
constexpr double kMidiTicksPerSecond = 1024000.0;

// Returns the bytes of `event`.
absl::Span<const uint8_t> GetBytes(const MIDI_event_t* event) {
  return absl::MakeConstSpan(event->midi_message, event->size);
}

}  // namespace

//==============================================================================
// FakeMidiEventList
//==============================================================================

void FakeMidiEventList::AddItem(MIDI_event_t* event) {
  ADD_FAILURE() << "MIDI_eventlist::AddItem() isn't faked yet";
}

MIDI_event_t* FakeMidiEventList::EnumItems(int* bpos) {
  if (*bpos < 0 || *bpos >= static_cast<int>(events_.size())) {
    return nullptr;
  }
  return reinterpret_cast<MIDI_event_t*>(events_[(*bpos)++].data());
}

void FakeMidiEventList::DeleteItem(int bpos) {
  ADD_FAILURE() << "MIDI_eventlist::DeleteItem() isn't faked yet";
}

int FakeMidiEventList::GetSize() {
  ADD_FAILURE() << "MIDI_eventlist::GetSize() isn't faked yet";
  return 0;
}

void FakeMidiEventList::Empty() { events_.clear(); }

void FakeMidiEventList::Add(absl::Span<const uint8_t> bytes, int frame_offset) {
  // MIDI_event_t ends in 4 bytes of the message, which longer ones run past.
  const int size = static_cast<int>(bytes.size());
  std::vector<uint8_t>& event = events_.emplace_back(
      sizeof(MIDI_event_t) + (size > 4 ? size - 4 : 0), uint8_t{0});
  auto* header = reinterpret_cast<MIDI_event_t*>(event.data());
  header->frame_offset = frame_offset;
  header->size = size;
  std::memcpy(header->midi_message, bytes.data(), bytes.size());
}

//==============================================================================
// FakeMidiInput
//==============================================================================

FakeMidiInput::~FakeMidiInput() {
  if (open_) {
    ADD_FAILURE() << "MIDI input \"" << name_
                  << "\" is still open when FakeReaper is destroyed";
  }
}

void FakeMidiInput::Send(const MidiMessage& message) {
  const uint8_t bytes[] = {message.status, message.data1, message.data2};
  Send(bytes);
}

void FakeMidiInput::Send(absl::Span<const uint8_t> bytes) {
  if (!open_ || !started_) {
    return;
  }
  sent_.push_back({.bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
                   .time = ::time_precise()});
}

void FakeMidiInput::SwapBufs(unsigned int timestamp) {
  ADD_FAILURE() << "midi_Input::SwapBufs() isn't faked; JPRSurf calls "
                   "SwapBufsPrecise()";
}

void FakeMidiInput::SwapBufsPrecise(unsigned int coarse_timestamp,
                                    double precise_timestamp) {
  if (!open_) {
    ADD_FAILURE() << "MIDI input \"" << name_ << "\" was used after Destroy()";
    return;
  }
  // Each event's offset is from the time of the swap, so it is negative for
  // an event sent before it.
  read_buffer_.Empty();
  for (const Event& event : sent_) {
    read_buffer_.Add(
        event.bytes,
        static_cast<int>(std::lround((event.time - precise_timestamp) *
                                     kMidiTicksPerSecond)));
  }
  sent_.clear();
}

void FakeMidiInput::Destroy() {
  open_ = false;
  started_ = false;
  sent_.clear();
  read_buffer_.Empty();
}

//==============================================================================
// FakeMidiOutput
//==============================================================================

FakeMidiOutput::~FakeMidiOutput() {
  if (open_) {
    ADD_FAILURE() << "MIDI output \"" << name_
                  << "\" is still open when FakeReaper is destroyed";
  }
}

void FakeMidiOutput::Connect(
    absl::AnyInvocable<void(absl::Span<const uint8_t> bytes)> receiver) {
  if (receiver_ != nullptr) {
    ADD_FAILURE() << "MIDI output \"" << name_
                  << "\" was connected while a device is connected to it";
    return;
  }
  receiver_ = std::move(receiver);
}

void FakeMidiOutput::SetRecording(bool recording) {
  recording_ = recording;
  if (!recording_) {
    received_.clear();
  }
}

std::vector<std::vector<uint8_t>> FakeMidiOutput::TakeReceived() {
  if (!recording_) {
    ADD_FAILURE() << "MIDI output \"" << name_
                  << "\" isn't recording (see SetRecording())";
  }
  return std::exchange(received_, {});
}

void FakeMidiOutput::SendMsg(MIDI_event_t* message, int frame_offset) {
  if (!open_) {
    ADD_FAILURE() << "MIDI output \"" << name_ << "\" was used after Destroy()";
    return;
  }
  Receive(GetBytes(message));
}

void FakeMidiOutput::Send(unsigned char status, unsigned char data1,
                          unsigned char data2, int frame_offset) {
  if (!open_) {
    ADD_FAILURE() << "MIDI output \"" << name_ << "\" was used after Destroy()";
    return;
  }
  const uint8_t bytes[] = {status, data1, data2};
  Receive(bytes);
}

void FakeMidiOutput::Receive(absl::Span<const uint8_t> bytes) {
  if (recording_) {
    received_.emplace_back(bytes.begin(), bytes.end());
  }
  if (receiver_ != nullptr) {
    receiver_(bytes);
  }
}

void FakeMidiOutput::Destroy() { open_ = false; }

}  // namespace jpr
