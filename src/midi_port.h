// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <vector>

#include "reaper_plugin.h"
#include "runner.h"

namespace jpr {

// Defines a standard MIDI message, which consists of a status byte and up to
// two data bytes. Whether the data bytes are used depends on the status byte
// (if they are not used they must be zero). This supports all MIDI messages
// other than SysEx messages which are handled separately.
struct MidiMessage {
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};

class MidiListener {
 public:
  virtual ~MidiListener() = default;

  // This is called when a MIDI message is received on the port that this
  // listener is registered to. The time parameter is the time that the message
  // was received, in seconds.
  virtual void OnMidiMessage(double time, const MidiMessage& message) = 0;
};
;

// This class represents a MIDI input port.
//
// It can be used to read MIDI messages from the port, and will automatically
// poll for new messages when Open() is called, and stop polling when Close() is
// called. The port will be automatically closed when the object is destroyed.
class MidiIn {
 public:
  // Enumerates the available MIDI input ports available in Reaper and returns a
  // list of constructed MidiIn objects. The ports are not opened yet, so Open()
  // must be called on any desired port.
  static std::vector<std::unique_ptr<MidiIn>> GetPorts();

  MidiIn(const MidiIn&) = delete;
  MidiIn& operator=(const MidiIn&) = delete;
  ~MidiIn();

  int GetIndex() const { return index_; }
  std::string_view GetName() const { return name_; }

  bool Open(RunRegistry& registry);
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

}  // namespace jpr