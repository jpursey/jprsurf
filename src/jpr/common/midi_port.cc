// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_port.h"

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

//==============================================================================
// MidiIn
//==============================================================================

namespace {

uint16_t MakeListenerKey(uint8_t status, uint8_t data = 0x80) {
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
  DoSubscribe(listener, MakeListenerKey(status));
}

void MidiIn::Subscribe(MidiListener* listener, uint8_t status, uint8_t data) {
  DoSubscribe(listener, MakeListenerKey(status, data));
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
  for (uint16_t listener_key : it->second) {
    auto& vec = listeners_[listener_key];
    vec.erase(std::remove(vec.begin(), vec.end(), listener), vec.end());
    if (vec.empty()) {
      listeners_.erase(listener_key);
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
    if (auto it = listeners_.find(MakeListenerKey(message.status));
        it != listeners_.end()) {
      for (MidiListener* listener : it->second) {
        listener->OnMidiMessage(message_time, message);
      }
    }

    // Now look up listeners for this specific status and data byte.
    for (auto it =
             listeners_.find(MakeListenerKey(message.status, message.data1));
         it != listeners_.end() &&
         it->first == MakeListenerKey(message.status, message.data1);
         ++it) {
      for (MidiListener* listener : it->second) {
        listener->OnMidiMessage(message_time, message);
      }
    }
  }
}

//==============================================================================
// MidiOut
//==============================================================================

namespace {

// Supported 7-bit CC numbers: 0-5, 7-31, 64-87, 89-97, 102-119.
// This excludes: 6 (data entry MSB), 32-63 (LSB for CCs 0-31), 88 (high
// resolution velocity prefix), 98-101 (NRPN/RPN), 120-127 (channel mode).
bool IsSupportedCc(uint8_t cc) {
  return (cc <= 5) || (cc >= 7 && cc <= 31) || (cc >= 64 && cc <= 87) ||
         (cc >= 89 && cc <= 97) || (cc >= 102 && cc <= 119);
}

// A note-on with velocity 0 is treated as a note-off per the MIDI spec.
bool IsNoteOn(const MidiMessage& message) {
  return (message.status & 0xF0) == 0x90 && message.data2 != 0;
}

// State key encoding: type_id (2 bits) | channel (4 bits) | value (8 bits),
// where value is note number, CC number, or 0 for pitch bend.
uint16_t MakeStateKey(int type_id, uint8_t channel, uint8_t value) {
  return (static_cast<uint16_t>(type_id) << 12) |
         (static_cast<uint16_t>(channel & 0x0F) << 8) | value;
}

}  // namespace

std::optional<MidiOut::StateInfo> MidiOut::GetStateInfo(
    const MidiMessage& message) const {
  uint8_t status_type = message.status & 0xF0;
  uint8_t channel = message.status & 0x0F;

  switch (status_type) {
    case 0x80:  // Note off
    case 0x90:  // Note on
      return StateInfo{.type = StateType::kNote,
                       .key = MakeStateKey(static_cast<int>(StateType::kNote),
                                           channel, message.data1)};
    case 0xB0:  // Control change
      if (IsSupportedCc(message.data1)) {
        return StateInfo{.type = StateType::kCc7,
                         .key = MakeStateKey(static_cast<int>(StateType::kCc7),
                                             channel, message.data1)};
      }
      LOG(WARNING) << "MidiOut: Unsupported CC number "
                   << static_cast<int>(message.data1)
                   << " for state tracking on channel "
                   << static_cast<int>(channel);
      return std::nullopt;
    case 0xE0:  // Pitch bend
      return StateInfo{
          .type = StateType::kPitchBend,
          .key = MakeStateKey(static_cast<int>(StateType::kPitchBend), channel,
                              0)};
    default:
      LOG(WARNING) << "MidiOut: Unsupported status "
                   << absl::Hex(message.status, absl::kZeroPad2)
                   << " for state tracking";
      return std::nullopt;
  }
}

void MidiOut::UpdateLastSent(const StateInfo& info,
                             const MidiMessage& message) {
  last_sent_[info.key] = message;
  pending_.erase(info.key);
  pending_keys_.erase(info.key);
}

std::vector<std::unique_ptr<MidiOut>> MidiOut::GetPorts() {
  std::vector<std::unique_ptr<MidiOut>> ports;
  int port_count = GetNumMIDIOutputs();
  ports.reserve(port_count);
  char name_buffer[256];
  for (int i = 0; i < port_count; ++i) {
    if (!GetMIDIOutputName(i, name_buffer, std::size(name_buffer))) {
      continue;
    }
    name_buffer[std::size(name_buffer) - 1] = '\0';
    ports.push_back(absl::WrapUnique(new MidiOut(i, name_buffer)));
  }
  return ports;
}

MidiOut::~MidiOut() { Close(); }

bool MidiOut::Open(RunRegistry& registry) {
  if (port_ != nullptr) {
    return true;
  }
  port_ = CreateMIDIOutput(index_, false, nullptr);
  if (port_ == nullptr) {
    LOG(ERROR) << "Failed to open MIDI output port " << index_ << " (" << name_
               << ")";
    return false;
  }
  run_handle_ =
      registry.AddRunnable([this](const RunTime& time) { Run(time); });
  LOG(INFO) << "Opened MIDI output port " << index_ << " (" << name_ << ")";
  return true;
}

void MidiOut::Close() {
  if (port_ == nullptr) {
    return;
  }
  run_handle_ = {};
  port_->Destroy();
  port_ = nullptr;
  message_queue_.clear();
  pending_.clear();
  pending_keys_.clear();
  last_sent_.clear();
  LOG(INFO) << "Closed MIDI output port " << index_ << " (" << name_ << ")";
}

void MidiOut::QueueMessage(const MidiMessage& message) {
  if (!run_handle_.IsRegistered()) {
    return;
  }

  // Always queue the message for sending.
  message_queue_.push_back(message);

  // If this message corresponds to a supported state type, update the
  // last-sent state and remove any pending state change (since this explicit
  // message will be sent unconditionally and supersedes it).
  if (auto info = GetStateInfo(message); info.has_value()) {
    UpdateLastSent(*info, message);
  }
}

void MidiOut::UpdateState(const MidiMessage& message) {
  if (!run_handle_.IsRegistered()) {
    return;
  }

  auto info = GetStateInfo(message);
  if (!info.has_value()) {
    return;
  }

  // Check against the last-sent state to determine if this needs to be queued.
  if (auto last_it = last_sent_.find(info->key); last_it != last_sent_.end()) {
    const MidiMessage& last = last_it->second;

    switch (info->type) {
      case StateType::kNote: {
        // For notes, we only send if the on/off state changes:
        //   - If last sent was note-on, only queue note-off changes.
        //   - If last sent was note-off, only queue note-on changes.
        bool last_was_on = IsNoteOn(last);
        bool new_is_on = IsNoteOn(message);
        if (last_was_on == new_is_on) {
          return;
        }
        break;
      }
      case StateType::kCc7: {
        // For CC, compare the value byte (data2).
        if (last.data2 == message.data2) {
          return;
        }
        break;
      }
      case StateType::kPitchBend: {
        // For pitch bend, compare the full 14-bit value (data1 + data2).
        if (last.data1 == message.data1 && last.data2 == message.data2) {
          return;
        }
        break;
      }
    }
  }

  // Queue the state change (replacing any prior pending change for this key).
  pending_[info->key] = message;
  pending_keys_.insert(info->key);
}

void MidiOut::ResetNoteState(uint8_t channel, uint8_t note) {
  uint16_t key =
      MakeStateKey(static_cast<int>(StateType::kNote), channel, note);
  last_sent_.erase(key);
  pending_.erase(key);
  pending_keys_.erase(key);
}

void MidiOut::ResetCc7State(uint8_t channel, uint8_t cc) {
  uint16_t key = MakeStateKey(static_cast<int>(StateType::kCc7), channel, cc);
  last_sent_.erase(key);
  pending_.erase(key);
  pending_keys_.erase(key);
}

void MidiOut::ResetPitchBendState(uint8_t channel) {
  uint16_t key =
      MakeStateKey(static_cast<int>(StateType::kPitchBend), channel, 0);
  last_sent_.erase(key);
  pending_.erase(key);
  pending_keys_.erase(key);
}

void MidiOut::ResetAllState() {
  last_sent_.clear();
  pending_.clear();
  pending_keys_.clear();
}

void MidiOut::Run(const RunTime& time) {
  if (port_ == nullptr) {
    return;
  }

  // First, send all queued explicit messages in FIFO order.
  for (const MidiMessage& message : message_queue_) {
    port_->Send(message.status, message.data1, message.data2, -1);

    // Update last-sent state for any messages that are supported state types.
    // This ensures that even after a ResetAllState() call between
    // QueueMessage() and Run(), the actually-sent message is reflected in the
    // state.
    if (auto info = GetStateInfo(message); info.has_value()) {
      last_sent_[info->key] = message;
    }
  }
  message_queue_.clear();

  // Then, send all pending state changes.
  for (uint16_t key : pending_keys_) {
    auto it = pending_.find(key);
    if (it == pending_.end()) {
      continue;
    }
    const MidiMessage& message = it->second;
    port_->Send(message.status, message.data1, message.data2, -1);
    last_sent_[key] = message;
  }
  pending_.clear();
  pending_keys_.clear();
}

}  // namespace jpr