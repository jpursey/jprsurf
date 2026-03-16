// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_port.h"

#include <cstring>

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

// Supported 7-bit CC numbers
bool IsSupportedCc(uint8_t cc) {
  return cc < 120;
}

// A note-on with velocity 0 is treated as a note-off per the MIDI spec.
bool IsNoteOn(const MidiMessage& message) {
  return (message.status & 0xF0) == 0x90 && message.data2 != 0;
}

// State key encoding: type_id (3 bits) | channel (4 bits) | value (8 bits),
// where value is note number, CC number, or 0 for pitch bend / channel
// pressure.
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
    case 0xA0:  // Polyphonic pressure
      return StateInfo{
          .type = StateType::kPolyPressure,
          .key = MakeStateKey(static_cast<int>(StateType::kPolyPressure),
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
    case 0xD0:  // Channel pressure
      return StateInfo{
          .type = StateType::kChannelPressure,
          .key = MakeStateKey(static_cast<int>(StateType::kChannelPressure),
                              channel, 0)};
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
  sysex_states_.clear();
  LOG(INFO) << "Closed MIDI output port " << index_ << " (" << name_ << ")";
}

void MidiOut::QueueMessage(const MidiMessage& message) {
  if (!run_handle_.IsRegistered()) {
    return;
  }

  // Always queue the message for sending.
  message_queue_.emplace_back(message);

  // If this message corresponds to a supported state type, update the
  // last-sent state and remove any pending state change (since this explicit
  // message will be sent unconditionally and supersedes it).
  if (auto info = GetStateInfo(message); info.has_value()) {
    UpdateLastSent(*info, message);
  }
}

bool MidiOut::IsStateMessage(const MidiMessage& message) const {
  return GetStateInfo(message).has_value();
}

void MidiOut::QueueMessage(const SysexMessage& message) {
  if (!run_handle_.IsRegistered()) {
    return;
  }

  // Always queue the message for sending.
  message_queue_.emplace_back(message);

  // Update the queued sysex state and replay pending messages.
  UpdateSysexQueuedState(message);
}

bool MidiOut::IsStateMessage(const SysexMessage& message) const {
  return SysexMessageType::Get(message.GetPrefix()) != nullptr;
}

void MidiOut::UpdateState(const SysexMessage& message) {
  if (!run_handle_.IsRegistered()) {
    return;
  }

  SysexMessageType* type = SysexMessageType::Get(message.GetPrefix());
  if (type == nullptr) {
    return;
  }

  SysexState& state = sysex_states_[message.GetPrefix()];

  // Build pending state if it doesn't exist yet.
  if (state.pending == nullptr) {
    if (state.queued != nullptr) {
      state.pending = state.queued->Clone();
    } else {
      // No queued state either -- create fresh from the message. The initial
      // CreateState represents the state as if the message were sent, so the
      // message should be queued for sending.
      state.pending = type->CreateState(message);
      if (state.pending != nullptr) {
        state.pending_messages.push_back(message);
      }
      return;
    }
  }

  // Update the pending state. If Update() returns true, the message changes
  // something not yet sent, so append it to the pending list.
  if (state.pending->Update(message)) {
    state.pending_messages.push_back(message);
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
      case StateType::kChannelPressure: {
        // For channel pressure, compare the pressure byte (data1).
        if (last.data1 == message.data1) {
          return;
        }
        break;
      }
      case StateType::kPolyPressure: {
        // For polyphonic pressure, compare the pressure byte (data2).
        if (last.data2 == message.data2) {
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

void MidiOut::ResetChannelPressureState(uint8_t channel) {
  uint16_t key =
      MakeStateKey(static_cast<int>(StateType::kChannelPressure), channel, 0);
  last_sent_.erase(key);
  pending_.erase(key);
  pending_keys_.erase(key);
}

void MidiOut::ResetPolyPressureState(uint8_t channel, uint8_t note) {
  uint16_t key =
      MakeStateKey(static_cast<int>(StateType::kPolyPressure), channel, note);
  last_sent_.erase(key);
  pending_.erase(key);
  pending_keys_.erase(key);
}

void MidiOut::ResetSysexState(const SysexPrefix& prefix) {
  sysex_states_.erase(prefix);
}

void MidiOut::ResetAllState() {
  last_sent_.clear();
  pending_.clear();
  pending_keys_.clear();
  sysex_states_.clear();
}

void MidiOut::SendSysexMessage(const SysexMessage& message) {
  absl::Span<const uint8_t> bytes = message.GetBytes();
  // Allocate a MIDI_event_t with enough room for the sysex bytes. The struct
  // has a 4-byte midi_message tail, so we only need extra space beyond that.
  const int extra = static_cast<int>(bytes.size()) > 4
                        ? static_cast<int>(bytes.size()) - 4
                        : 0;
  std::vector<uint8_t> buffer(sizeof(MIDI_event_t) + extra);
  auto* event = reinterpret_cast<MIDI_event_t*>(buffer.data());
  event->frame_offset = -1;
  event->size = static_cast<int>(bytes.size());
  std::memcpy(event->midi_message, bytes.data(), bytes.size());
  port_->SendMsg(event, -1);
}

void MidiOut::UpdateSysexQueuedState(const SysexMessage& message) {
  SysexMessageType* type = SysexMessageType::Get(message.GetPrefix());
  if (type == nullptr) {
    return;
  }

  SysexState& state = sysex_states_[message.GetPrefix()];

  // Update (or create) the queued state.
  if (state.queued != nullptr) {
    state.queued->Update(message);
  } else {
    state.queued = type->CreateState(message);
  }

  // Rebuild the pending state from the queued state and replay pending
  // messages, dropping any that no longer apply.
  if (!state.pending_messages.empty()) {
    state.pending = state.queued->Clone();
    std::vector<SysexMessage> surviving;
    for (const SysexMessage& pending_msg : state.pending_messages) {
      if (state.pending->Update(pending_msg)) {
        surviving.push_back(pending_msg);
      }
    }
    state.pending_messages = std::move(surviving);
    if (state.pending_messages.empty()) {
      state.pending.reset();
    }
  } else {
    state.pending.reset();
  }
}

void MidiOut::Run(const RunTime& time) {
  if (port_ == nullptr) {
    return;
  }

  // First, send all queued explicit messages in FIFO order.
  for (const auto& entry : message_queue_) {
    std::visit(
        [this](const auto& message) {
          using T = std::decay_t<decltype(message)>;
          if constexpr (std::is_same_v<T, MidiMessage>) {
            port_->Send(message.status, message.data1, message.data2, -1);
            if (auto info = GetStateInfo(message); info.has_value()) {
              last_sent_[info->key] = message;
            }
          } else {
            SendSysexMessage(message);
          }
        },
        entry);
  }
  message_queue_.clear();

  // Then, send all pending MidiMessage state changes.
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

  // Finally, send all pending sysex messages and update state.
  for (auto& [prefix, state] : sysex_states_) {
    if (state.pending_messages.empty()) {
      continue;
    }
    // Start from the best available state to rebuild queued state as messages
    // are sent.
    if (state.pending != nullptr) {
      state.queued = std::move(state.pending);
    } else if (state.queued == nullptr) {
      // No state at all -- build fresh from the first pending message.
      SysexMessageType* type = SysexMessageType::Get(prefix);
      if (type != nullptr && !state.pending_messages.empty()) {
        state.queued = type->CreateState(state.pending_messages[0]);
        SendSysexMessage(state.pending_messages[0]);
        // Process remaining messages through the new state.
        for (size_t i = 1; i < state.pending_messages.size(); ++i) {
          if (state.queued->Update(state.pending_messages[i])) {
            SendSysexMessage(state.pending_messages[i]);
          }
        }
        state.pending_messages.clear();
        continue;
      }
    }
    // Send each pending message, updating queued state as we go.
    for (const SysexMessage& message : state.pending_messages) {
      SendSysexMessage(message);
      if (state.queued != nullptr) {
        state.queued->Update(message);
      }
    }
    state.pending_messages.clear();
  }
}

}  // namespace jpr