// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/midi_ports.h"

#include <algorithm>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace jpr {

namespace {

template <typename Port>
std::string JoinNames(const std::vector<std::unique_ptr<Port>>& ports) {
  return absl::StrJoin(ports, ", ", [](std::string* out, const auto& port) {
    absl::StrAppend(out, "\"", port->GetName(), "\"");
  });
}

}  // namespace

absl::Duration MidiPorts::s_flush_wait_ = absl::Milliseconds(100);

void MidiPorts::SetFlushWait(absl::Duration wait) { s_flush_wait_ = wait; }

MidiPorts::MidiPorts()
    : inputs_(MidiIn::GetPorts()), outputs_(MidiOut::GetPorts()) {
  LOG(INFO) << "MIDI input ports: " << JoinNames(inputs_);
  LOG(INFO) << "MIDI output ports: " << JoinNames(outputs_);
}

MidiPorts::~MidiPorts() {
  ValidateNoHandles("input", inputs_);
  ValidateNoHandles("output", outputs_);
  if (std::ranges::none_of(outputs_,
                           [](const auto& port) { return port->IsOpen(); })) {
    return;
  }
  RunOutput(RunTime::Now());

  // MIDI still being sent when a port is destroyed is lost (the X-Touch
  // Extender, whose port was destroyed first, did not clear on shutdown without
  // this). REAPER has no way to flush a port, so give them time to finish
  // sending.
  absl::SleepFor(s_flush_wait_);
}

template <typename Port>
Port* MidiPorts::Find(std::string_view name,
                      const std::vector<std::unique_ptr<Port>>& ports) {
  for (const auto& port : ports) {
    if (port->GetName() == name) {
      return port.get();
    }
  }
  return nullptr;
}

template <typename Port>
MidiPortHandle<Port> MidiPorts::Open(
    std::string_view name, std::string_view kind, RunRegistry& registry,
    const std::vector<std::unique_ptr<Port>>& ports) {
  Port* port = Find(name, ports);
  if (port == nullptr) {
    LOG(INFO) << "No MIDI " << kind << " port named \"" << name << "\"";
    return {};
  }
  if (!port->Open(registry)) {
    return {};
  }
  ++port->handle_count_;
  return MidiPortHandle<Port>(port);
}

template <typename Port>
void MidiPorts::ValidateNoHandles(
    std::string_view kind, const std::vector<std::unique_ptr<Port>>& ports) {
  for (const auto& port : ports) {
    if (port->handle_count_ > 0) {
      LOG(ERROR) << "MIDI " << kind << " port \"" << port->GetName()
                 << "\" has " << port->handle_count_
                 << " handle(s) left as MidiPorts is destroyed";
    }
  }
}

bool MidiPorts::HasInput(std::string_view name) const {
  return Find(name, inputs_) != nullptr;
}

bool MidiPorts::HasOutput(std::string_view name) const {
  return Find(name, outputs_) != nullptr;
}

MidiInHandle MidiPorts::OpenInput(std::string_view name) {
  return Open(name, "input", input_runner_, inputs_);
}

MidiOutHandle MidiPorts::OpenOutput(std::string_view name) {
  return Open(name, "output", output_runner_, outputs_);
}

}  // namespace jpr
