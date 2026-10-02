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
#include "jpr/common/reset_test_key.h"
#include "jpr/common/runner.h"

namespace jpr {

//==============================================================================
// MidiPorts
//==============================================================================

// This class opens MIDI ports by name, owns them, and runs them.
//
// Ports stay open until MidiPorts is destroyed. Destroying it sends any MIDI
// output still queued, and waits briefly for the ports to finish sending, so
// anything that writes to the ports can queue its final output before then
// (for instance, clearing a surface on shutdown).
class MidiPorts final {
 public:
  // For tests only: sets how long destroying MidiPorts waits for its ports to
  // finish sending (100ms by default). The fake REAPER's ports send at once,
  // and tests never wait on real time, so it sets none.
  static void SetFlushWait(ResetTestKey, absl::Duration wait);

  // Lists the MIDI ports available in REAPER, none of them open yet.
  MidiPorts();

  MidiPorts(const MidiPorts&) = delete;
  MidiPorts& operator=(const MidiPorts&) = delete;
  ~MidiPorts();

  // Opens the MIDI input or output port with the given name in REAPER.
  //
  // Opening a port that is already open returns the same port. Returns null if
  // there is no port with this name, or if it fails to open.
  MidiIn* OpenInput(std::string_view name);
  MidiOut* OpenOutput(std::string_view name);

  // Reads the MIDI input received since the last call, and passes it to the
  // input ports' listeners, with the run's time.
  void RunInput(const RunTime& time) { input_runner_.Run(time); }

  // Sends the MIDI output queued on the output ports.
  void RunOutput(const RunTime& time) { output_runner_.Run(time); }

 private:
  // Implements OpenInput() and OpenOutput() for either type of port.
  template <typename Port>
  static Port* Open(std::string_view name, std::string_view kind,
                    RunRegistry& registry,
                    const std::vector<std::unique_ptr<Port>>& ports);

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
