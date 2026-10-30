// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
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
  // Control names
  //
  // Each control is named by its path in its model's catalog (see
  // GetCatalog()). These are the names of the controls the plugin maps.
  //----------------------------------------------------------------------------

  static constexpr std::string_view kMasterFader = "master_fader";
  static constexpr std::string_view kTimecode = "timecode";
  static constexpr std::string_view kSmpteLed = "smpte_light";
  static constexpr std::string_view kBeatsLed = "beats_light";
  static constexpr std::string_view kSoloLed = "solo_light";
  static constexpr std::string_view kAssignTrack = "assign_track";
  static constexpr std::string_view kAssignSend = "assign_send";
  static constexpr std::string_view kBankLeft = "bank_left";
  static constexpr std::string_view kBankRight = "bank_right";
  static constexpr std::string_view kChannelLeft = "channel_left";
  static constexpr std::string_view kChannelRight = "channel_right";
  static constexpr std::string_view kGlobal = "global";
  static constexpr std::string_view kShowTimeBeats = "show_time_beats";
  static constexpr std::string_view kShift = "shift";
  static constexpr std::string_view kOption = "option";
  static constexpr std::string_view kControl = "control";
  static constexpr std::string_view kAlt = "alt";
  static constexpr std::string_view kAutoRead = "auto_read";
  static constexpr std::string_view kAutoWrite = "auto_write";
  static constexpr std::string_view kAutoTrim = "auto_trim";
  static constexpr std::string_view kAutoTouch = "auto_touch";
  static constexpr std::string_view kAutoLatch = "auto_latch";
  static constexpr std::string_view kAutoGroup = "auto_group";
  static constexpr std::string_view kSave = "save";
  static constexpr std::string_view kUndo = "undo";
  static constexpr std::string_view kCancel = "cancel";
  static constexpr std::string_view kEnter = "enter";
  static constexpr std::string_view kMarker = "marker";
  static constexpr std::string_view kNudge = "nudge";
  static constexpr std::string_view kCycle = "cycle";
  static constexpr std::string_view kClick = "click";
  static constexpr std::string_view kSolo = "solo";
  static constexpr std::string_view kRewind = "rewind";
  static constexpr std::string_view kForward = "forward";
  static constexpr std::string_view kStop = "stop";
  static constexpr std::string_view kPlay = "play";
  static constexpr std::string_view kRecord = "record";

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  // `overrides` name the inputs and outputs of controls the unit doesn't use
  // (see Device).
  DeviceXTouch(Type type, RunRegistry& run_registry, MidiIn* midi_in,
               MidiOut* midi_out,
               absl::Span<const ControlOverride> overrides = {});
  ~DeviceXTouch() override;

  //----------------------------------------------------------------------------
  // Catalogs
  //----------------------------------------------------------------------------

  // Returns the catalog of the model: a struct of its controls, with its
  // channel strips in the array `strip`. Both models have the same strips, so
  // one join can take strips from each.
  static const WidgetShape& GetCatalog(Type type);

  //----------------------------------------------------------------------------
  // Strip control names
  //
  // These return the name of a control on a strip (from 0), such as
  // "strip[2].fader".
  //----------------------------------------------------------------------------

  static std::string Rec(int strip) { return GetStripControl(strip, "rec"); }
  static std::string Solo(int strip) { return GetStripControl(strip, "solo"); }
  static std::string Mute(int strip) { return GetStripControl(strip, "mute"); }
  static std::string Select(int strip) {
    return GetStripControl(strip, "select");
  }
  static std::string Fader(int strip) {
    return GetStripControl(strip, "fader");
  }
  static std::string Pot(int strip) { return GetStripControl(strip, "pot"); }
  static std::string PotButton(int strip) {
    return GetStripControl(strip, "pot_button");
  }
  static std::string Meter(int strip) {
    return GetStripControl(strip, "meter");
  }
  // The top line is 0, and the bottom line 1.
  static std::string Scribble(int strip, int line) {
    return GetStripControl(strip,
                           line == 0 ? "scribble_top" : "scribble_bottom");
  }
  static std::string ScribbleColor(int strip) {
    return GetStripControl(strip, "scribble_color");
  }

 private:
  // Returns the name of the field's control on the strip.
  static std::string GetStripControl(int strip, std::string_view field);

  // Adds a button that sends the note, with a light on the same note if it
  // has one.
  void AddButton(std::string_view name, uint8_t note, bool has_light,
                 MidiIn* midi_in, MidiOut* midi_out);

  uint8_t scribble_colors_[kStripCount] = {};
};

}  // namespace jpr
