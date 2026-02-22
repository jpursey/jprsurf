// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reaper_plugin.h"
#include "runner.h"

namespace jpr {

//==============================================================================
// MidiMessage
//==============================================================================

// Defines a standard MIDI message, which consists of a status byte and up to
// two data bytes. Whether the data bytes are used depends on the status byte
// (if they are not used they must be zero). This supports all MIDI messages
// other than SysEx messages which are handled separately.
struct MidiMessage {
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

//==============================================================================
// MidiListener
//==============================================================================

// This is the interface for receiving MIDI messages from a MIDI input port.
//
// This can be implemented by any class that wants to receive MIDI messages, and
// can be registered to receive messages from a MidiIn port by calling
// Subscribe() on the port with the desired status and data values (see
// MidiIn::Subscribe()).
class MidiListener {
 public:
  virtual ~MidiListener() = default;

  // This is called when a MIDI message is received on the port that this
  // listener is registered to. The time parameter is the time that the message
  // was received, in seconds.
  virtual void OnMidiMessage(double time, const MidiMessage& message) = 0;
};

//==============================================================================
// MidiIn
//==============================================================================

// This class represents a MIDI input port.
//
// It can be used to read MIDI messages from the port, and will automatically
// poll for new messages when Open() is called, and stop polling when Close() is
// called. The port will be automatically closed when the object is destroyed.
class MidiIn final {
 public:
  // Enumerates the available MIDI input ports available in Reaper and returns a
  // list of constructed MidiIn objects. The ports are not opened yet, so Open()
  // must be called on any desired port.
  static std::vector<std::unique_ptr<MidiIn>> GetPorts();

  MidiIn(const MidiIn&) = delete;
  MidiIn& operator=(const MidiIn&) = delete;
  ~MidiIn();

  // Returns the index of this MIDI port within Reaper, which can be used to
  // identify the port when opening it.
  int GetIndex() const { return index_; }

  // Returns the name of this MIDI port in Reaper, which may be overriden by the
  // user. It is not necessarily the device name from the OS.
  std::string_view GetName() const { return name_; }

  // Opens the MIDI input port and starts polling for messages.
  //
  // Returns true if the port was successfully opened.
  bool Open(RunRegistry& registry);

  // Closes the MIDI input port and stops polling for messages. After this is
  // called, the port will no longer receive any MIDI messages.
  //
  // Close() will be automatically called when the object is destroyed, so it
  // does not need to be called explicitly.
  void Close();

  // Subscribes a listener to a specific MIDI message. If data is specified, the
  // listener will only be called for messages with matching the first data
  // byte. If they are not specified, the listener will be called for messages
  // with any value with the specified status.
  void Subscribe(MidiListener* listener, uint8_t status);
  void Subscribe(MidiListener* listener, uint8_t status, uint8_t data);

  // Unsubscribes a listener from all MIDI messages. The listener will no longer
  // receive any MIDI messages from this port.
  void Unsubscribe(MidiListener* listener);

 private:
  MidiIn(int index, std::string name) : index_(index), name_(std::move(name)) {}

  void DoSubscribe(MidiListener* listener, uint16_t key);

  void Poll(const RunTime& time);

  int index_ = -1;
  std::string name_;
  midi_Input* port_ = nullptr;
  RunHandle run_handle_;
  absl::flat_hash_map<uint16_t, std::vector<MidiListener*>> listeners_;
  absl::flat_hash_map<MidiListener*, std::vector<uint16_t>> listener_keys_;
};

//==============================================================================
// MidiOut
//==============================================================================

// This class represents a MIDI output port.
//
// It can be used to send MIDI messages from the port, and will automatically
// manage the state of sent messages to avoid sending redundant messages. The
// port will be automatically closed when the object is destroyed, which will
// stop any pending messages from being sent.
class MidiOut final {
 public:
  // Enumerates the available MIDI output ports available in Reaper and returns
  // a list of constructed MidiOut objects. The ports are not opened yet, so
  // Open() must be called on any desired port.
  static std::vector<std::unique_ptr<MidiOut>> GetPorts();

  MidiOut(const MidiOut&) = delete;
  MidiOut& operator=(const MidiOut&) = delete;
  ~MidiOut();

  // Returns the index of this MIDI port within Reaper, which can be used to
  // identify the port when opening it.
  int GetIndex() const { return index_; }

  // Returns the name of this MIDI port in Reaper, which may be overriden by the
  // user. It is not necessarily the device name from the OS.
  std::string_view GetName() const { return name_; }

  // Opens the MIDI output port.
  //
  // Returns true if the port was successfully opened.
  bool Open(RunRegistry& registry);

  // Closes the MIDI output port. After this is called, the port will no longer
  // be available for sending MIDI messages.
  //
  // Close() will be automatically called when the object is destroyed, so it
  // does not need to be called explicitly.
  void Close();

  // Queues a MIDI message to be sent from this output port.
  //
  // This does nothing if the port is not currently open.
  //
  // All messages will be sent at the next Run() invocation after this is
  // called in FIFO order. This message is always sent, and ahead of any state
  // changes that might otherwise be pending.
  //
  // This will also update any related state (see UpdateState()), and any prior
  // pending state changes will be dequeued (as this message is sent
  // unconditionally). However, it is generally preferable to only use
  // UpdateState for any state-related messages, so only the latest value is
  // sent.
  void QueueMessage(const MidiMessage& message);

  // Updates the internal state of a MIDI note or control value.
  //
  // This does nothing if the port is not currently open.
  //
  // The message is compared to the last sent state (if there is one), and if it
  // is different, updates the internal state and queues it for sending. Pending
  // state changes are sent at the next Run() invocation after this is called,
  // after any messages queued via QueueMessage().
  //
  // This only supports note on/off messages, 7-bit control change (CC) messages
  // (no RPN/NRPN, and only MSB for potentially 14-bit CCs like volume, pan,
  // etc), and pitch bend messages (always 14-bit). If it is not one of these
  // messages, this will not do anything.
  void UpdateState(const MidiMessage& message);

  // Resets the internal state for the specified MIDI note.
  //
  // This sets the state of the note to unknown, and dequeues any pending state
  // changes for the note. Any subsequent call to UpdateState() for the note
  // will always result in the state being queued for sending.
  void ResetNoteState(uint8_t channel, uint8_t note);

  // Resets the internal state for the specified MIDI 7-bit control change.
  //
  // This sets the state of the control change to unknown, and dequeues any
  // pending state changes for the control change. Any subsequent call to
  // UpdateState() for the control change will always result in the state being
  // queued for sending.
  void ResetCc7State(uint8_t channel, uint8_t cc);

  // Resets the internal state for the specified MIDI pitch bend.
  //
  // This sets the state of the pitch bend to unknown, and dequeues any pending
  // state changes for the pitch bend. Any subsequent call to UpdateState() for
  // the pitch bend will always result in the state being queued for sending.
  void ResetPitchBendState(uint8_t channel);

  // Resets the internal state for all MIDI notes, control changes, and pitch
  // bends.
  //
  // This sets the state of all notes, control changes, and pitch bends to
  // unknown, and dequeues any pending state changes for all of them. Any
  // subsequent call to UpdateState() for a supported state will always result
  // in the state being queued for sending.
  void ResetAllState();

 private:
  MidiOut(int index, std::string name)
      : index_(index), name_(std::move(name)) {}

  enum class StateType { kNote, kCc7, kPitchBend };

  struct StateInfo {
    StateType type;
    uint16_t key;
  };

  // Attempts to classify a message as a state message. Returns std::nullopt if
  // the message is not a supported state type.
  std::optional<StateInfo> GetStateInfo(const MidiMessage& message) const;

  // Updates the last-sent state for the given key, and removes any pending
  // state change for it.
  void UpdateLastSent(const StateInfo& info, const MidiMessage& message);

  void Run(const RunTime& time);

  int index_ = -1;
  std::string name_;
  midi_Output* port_ = nullptr;
  RunHandle run_handle_;

  // FIFO queue of messages to send unconditionally (via QueueMessage).
  std::vector<MidiMessage> message_queue_;

  // State tracking: last-sent and pending state for each state key.
  absl::flat_hash_map<uint16_t, MidiMessage> last_sent_;
  absl::flat_hash_map<uint16_t, MidiMessage> pending_;
  absl::flat_hash_set<uint16_t> pending_keys_;
};

}  // namespace jpr