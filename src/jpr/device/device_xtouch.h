// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/device/device.h"

namespace jpr {

class DeviceXTouch final : public Device {
 public:
  DeviceXTouch(RunRegistry& run_registry, MidiIn* midi_in, MidiOut* midi_out);
  ~DeviceXTouch() override;
};

}  // namespace jpr