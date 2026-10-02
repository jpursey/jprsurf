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

MidiPorts::MidiPorts()
    : inputs_(MidiIn::GetPorts()), outputs_(MidiOut::GetPorts()) {
  LOG(INFO) << "MIDI input ports: " << JoinNames(inputs_);
  LOG(INFO) << "MIDI output ports: " << JoinNames(outputs_);
}

MidiPorts::~MidiPorts() {
  if (std::ranges::none_of(outputs_,
                           [](const auto& port) { return port->IsOpen(); })) {
    return;
  }
  RunOutput(RunTime::Now());

  // MIDI still being sent when a port is destroyed is lost (the X-Touch
  // Extender, whose port was destroyed first, did not clear on shutdown without
  // this). REAPER has no way to flush a port, so give them time to finish
  // sending.
  absl::SleepFor(absl::Milliseconds(100));
}

template <typename Port>
Port* MidiPorts::Open(std::string_view name, std::string_view kind,
                      RunRegistry& registry,
                      const std::vector<std::unique_ptr<Port>>& ports) {
  for (const auto& port : ports) {
    if (port->GetName() == name) {
      return port->Open(registry) ? port.get() : nullptr;
    }
  }
  LOG(INFO) << "No MIDI " << kind << " port named \"" << name << "\"";
  return nullptr;
}

MidiIn* MidiPorts::OpenInput(std::string_view name) {
  return Open(name, "input", input_runner_, inputs_);
}

MidiOut* MidiPorts::OpenOutput(std::string_view name) {
  return Open(name, "output", output_runner_, outputs_);
}

}  // namespace jpr
