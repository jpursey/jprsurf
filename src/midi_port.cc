// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "midi_port.h"

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "reaper_plugin_functions.h"

namespace jpr {

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

void MidiIn::Poll(const RunTime& time) {
  if (port_ == nullptr) {
    return;
  }
  port_->SwapBufsPrecise(time.coarse, time.precise);

  // TODO: Process MIDI messages here.
}

}  // namespace jpr