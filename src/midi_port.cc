// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "midi_port.h"

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "reaper_plugin_functions.h"

namespace jpr {

namespace {

uint16_t MakeKey(uint8_t status, uint8_t data = 0x80) {
  return (static_cast<uint16_t>(status) << 8) | data;
}

// This is the denominator used to convert MIDI time from the integer format
// used by Reaper to seconds.
constexpr double kSecondsPerMidiTick = 1.0 / 1024000.0;

}  // namespace

std::vector<std::unique_ptr<MidiIn>> MidiIn::GetPorts() {
  std::vector<std::unique_ptr<MidiIn>> ports;
  int port_count = GetNumMIDIInputs();
  ports.reserve(port_count);
  char name_buffer[256];
  for (int i = 0; i < port_count; ++i) {
    if (!GetMIDIInputName(i, name_buffer, std::size(name_buffer))) {
      continue;
    }
    name_buffer[std::size(name_buffer) - 1] = '\0';
    ports.push_back(absl::WrapUnique(new MidiIn(i, name_buffer)));
  }
  return ports;
}

MidiIn::~MidiIn() { Close(); }

bool MidiIn::Open(RunRegistry& registry) {
  if (port_ != nullptr) {
    return true;
  }
  port_ = CreateMIDIInput(index_);
  if (port_ == nullptr) {
    LOG(ERROR) << "Failed to open MIDI port " << index_ << " (" << name_ << ")";
    return false;
  }
  port_->start();
  run_handle_ =
      registry.AddRunnable([this](const RunTime& time) { Poll(time); });
  LOG(INFO) << "Opened MIDI port " << index_ << " (" << name_ << ")";
  return true;
}

void MidiIn::Close() {
  if (port_ == nullptr) {
    return;
  }
  port_->stop();
  port_->Destroy();
  port_ = nullptr;
  run_handle_ = {};
  LOG(INFO) << "Closed MIDI port " << index_ << " (" << name_ << ")";
}

void MidiIn::Subscribe(MidiListener* listener, uint8_t status) {
  DoSubscribe(listener, MakeKey(status));
}

void MidiIn::Subscribe(MidiListener* listener, uint8_t status, uint8_t data) {
  DoSubscribe(listener, MakeKey(status, data));
}

void MidiIn::DoSubscribe(MidiListener* listener, uint16_t key) {
  listeners_[key].push_back(listener);
  listener_keys_[listener].push_back(key);
}

void MidiIn::Unsubscribe(MidiListener* listener) {
  auto it = listener_keys_.find(listener);
  if (it == listener_keys_.end()) {
    return;
  }
  for (uint16_t key : it->second) {
    auto& vec = listeners_[key];
    vec.erase(std::remove(vec.begin(), vec.end(), listener), vec.end());
    if (vec.empty()) {
      listeners_.erase(key);
    }
  }
  listener_keys_.erase(it);
}

void MidiIn::Poll(const RunTime& time) {
  if (port_ == nullptr) {
    return;
  }
  port_->SwapBufsPrecise(time.coarse, time.precise);

  auto* event_list = port_->GetReadBuf();
  int bpos = 0;
  while (true) {
    MIDI_event_t* event = event_list->EnumItems(&bpos);
    if (event == nullptr) {
      break;
    }

    // Ignore sysex messages for now, since they can't be subscribed to.
    if (event->midi_message[0] == 0xF0) {
      continue;
    }

    // Convert to a 3-byte MIDI message, and time.
    double message_time =
        time.precise + event->frame_offset * kSecondsPerMidiTick;
    MidiMessage message = {.status = event->midi_message[0],
                           .data1 = event->midi_message[1],
                           .data2 = event->midi_message[2]};

    // First look up global listeners for this status byte, then look up
    // listeners for this specific status and data byte.
    if (auto it = listeners_.find(MakeKey(message.status));
        it != listeners_.end()) {
      for (MidiListener* listener : it->second) {
        listener->OnMidiMessage(message_time, message);
      }
    }

    // Now look up listeners for this specific status and data byte.
    for (auto it = listeners_.find(MakeKey(message.status, message.data1));
         it != listeners_.end() &&
         it->first == MakeKey(message.status, message.data1);
         ++it) {
      for (MidiListener* listener : it->second) {
        listener->OnMidiMessage(message_time, message);
      }
    }
  }
}

}  // namespace jpr