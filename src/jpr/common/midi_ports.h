// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"

namespace jpr {

//==============================================================================
// MidiPorts
//==============================================================================

// This class opens MIDI ports by name, owns them, and runs them.
//
// Each port stays open while a handle to it lives (see MidiPortHandle), so it
// closes as the last thing using it goes. An output port closes once it is also
// done sending: its last handle goes with nothing queued, and nothing sent
// within the flush wait. Until then its runs send what is queued, and close it
// once the flush wait has passed. Destroying MidiPorts sends any MIDI output
// still queued, and waits the flush wait for the ports to finish sending. So
// anything that writes to the ports can queue its final output as it goes (for
// instance, clearing a surface on shutdown). Every handle must be gone before
// MidiPorts is: one left logs an error.
class MidiPorts final {
 public:
  // Sets and returns how long an output port may take to finish sending what
  // it sent (100ms by default), which a port waits before it closes. Ports
  // that send at once, such as the fake REAPER's, need none.
  static void SetFlushWait(absl::Duration wait);
  static absl::Duration GetFlushWait() { return s_flush_wait_; }

  // Lists the MIDI ports available in REAPER, none of them open yet.
  MidiPorts();

  MidiPorts(const MidiPorts&) = delete;
  MidiPorts& operator=(const MidiPorts&) = delete;
  ~MidiPorts();

  // Returns true if REAPER lists a MIDI input or output port with the given
  // name, open or not, without opening it.
  bool HasInput(std::string_view name) const;
  bool HasOutput(std::string_view name) const;

  // Opens the MIDI input or output port with the given name in REAPER, and
  // returns a handle to it, which keeps it open.
  //
  // Opening a port that is already open returns another handle to the same
  // port. Returns a null handle if there is no port with this name, or if it
  // fails to open.
  MidiInHandle OpenInput(std::string_view name);
  MidiOutHandle OpenOutput(std::string_view name);

  // Reads the MIDI input received since the last call, and passes it to the
  // input ports' listeners, with the run's time.
  void RunInput(const RunTime& time) { input_runner_.Run(time); }

  // Sends the MIDI output queued on the output ports.
  void RunOutput(const RunTime& time) { output_runner_.Run(time); }

 private:
  // Returns the port with the name, or null if there is none.
  template <typename Port>
  static Port* Find(std::string_view name,
                    const std::vector<std::unique_ptr<Port>>& ports);

  // Implements OpenInput() and OpenOutput() for either type of port.
  template <typename Port>
  static MidiPortHandle<Port> Open(
      std::string_view name, std::string_view kind, RunRegistry& registry,
      const std::vector<std::unique_ptr<Port>>& ports);

  // Logs an error for each port with a handle left.
  template <typename Port>
  static void ValidateNoHandles(
      std::string_view kind, const std::vector<std::unique_ptr<Port>>& ports);

  // How long destroying MidiPorts waits for its ports to finish sending (see
  // SetFlushWait()).
  static absl::Duration s_flush_wait_;

  // The runners must outlive the ports, which are registered with them.
  Runner input_runner_{"MidiIn"};
  Runner output_runner_{"MidiOut"};

  // Every port in REAPER, whether or not it is open.
  std::vector<std::unique_ptr<MidiIn>> inputs_;
  std::vector<std::unique_ptr<MidiOut>> outputs_;
};

}  // namespace jpr
