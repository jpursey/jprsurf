// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
#include "jpr/device/widget_doc.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

class DeviceXTouch final : public Device {
 public:
  //----------------------------------------------------------------------------
  // Models
  //----------------------------------------------------------------------------

  // The model of X-Touch: the full X-Touch, with its master section, or the
  // extender, with just strips.
  enum class Type { kFull, kExtender };

  // The number of channel strips on each model.
  static constexpr int kStripCount = 8;

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  // The device keeps its ports open while it lives. `overrides` name the
  // inputs and outputs of controls the unit doesn't use (see Device).
  DeviceXTouch(Type type, RunRegistry& run_registry, MidiInHandle input,
               MidiOutHandle output,
               absl::Span<const ControlOverride> overrides = {});
  ~DeviceXTouch() override;

  //----------------------------------------------------------------------------
  // Catalogs
  //----------------------------------------------------------------------------

  // Returns the catalog of the model: a struct of its controls, with its
  // channel strips in the array `strip`. Both models have the same strips, so
  // one join can take strips from each. The device names each control by its
  // path in the catalog, such as "strip[2].fader" or "play".
  static const WidgetShape& GetCatalog(Type type);

  // Returns the documentation of the model's catalog (see CatalogDoc).
  static const CatalogDoc& GetCatalogDoc(Type type);

 private:
  // Adds a button that sends the note, with a light on the same note if it
  // has one.
  void AddButton(std::string_view name, uint8_t note, bool has_light,
                 MidiIn* midi_in, MidiOut* midi_out);

  // The ports, kept open while the device lives. They close before Device
  // destroys the controls, which only unsubscribe from the input as they go,
  // as a closed port allows.
  MidiInHandle midi_in_;
  MidiOutHandle midi_out_;

  uint8_t scribble_colors_[kStripCount] = {};
};

}  // namespace jpr
