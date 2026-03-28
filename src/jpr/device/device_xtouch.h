// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/runner.h"
#include "jpr/device/device.h"

namespace jpr {

class DeviceXTouch final : public Device {
 public:
  //----------------------------------------------------------------------------
  // Per-track controls
  //----------------------------------------------------------------------------

  static constexpr std::string_view kRec1 = "Rec1";
  static constexpr std::string_view kSolo1 = "Solo1";
  static constexpr std::string_view kMute1 = "Mute1";
  static constexpr std::string_view kSelect1 = "Select1";
  static constexpr std::string_view kFader1 = "Fader1";
  static constexpr std::string_view kPot1 = "Pot1";
  static constexpr std::string_view kPotButton1 = "PotButton1";
  static constexpr std::string_view kMeter1 = "Meter1";
  static constexpr std::string_view kScribble1Line1 = "Scribble1Line1";
  static constexpr std::string_view kScribble1Line2 = "Scribble1Line2";
  static constexpr std::string_view kScribble1Color = "Scribble1Color";

  static constexpr std::string_view kRec2 = "Rec2";
  static constexpr std::string_view kSolo2 = "Solo2";
  static constexpr std::string_view kMute2 = "Mute2";
  static constexpr std::string_view kSelect2 = "Select2";
  static constexpr std::string_view kFader2 = "Fader2";
  static constexpr std::string_view kPot2 = "Pot2";
  static constexpr std::string_view kPotButton2 = "PotButton2";
  static constexpr std::string_view kMeter2 = "Meter2";
  static constexpr std::string_view kScribble2Line1 = "Scribble2Line1";
  static constexpr std::string_view kScribble2Line2 = "Scribble2Line2";
  static constexpr std::string_view kScribble2Color = "Scribble2Color";

  static constexpr std::string_view kRec3 = "Rec3";
  static constexpr std::string_view kSolo3 = "Solo3";
  static constexpr std::string_view kMute3 = "Mute3";
  static constexpr std::string_view kSelect3 = "Select3";
  static constexpr std::string_view kFader3 = "Fader3";
  static constexpr std::string_view kPot3 = "Pot3";
  static constexpr std::string_view kPotButton3 = "PotButton3";
  static constexpr std::string_view kMeter3 = "Meter3";
  static constexpr std::string_view kScribble3Line1 = "Scribble3Line1";
  static constexpr std::string_view kScribble3Line2 = "Scribble3Line2";
  static constexpr std::string_view kScribble3Color = "Scribble3Color";

  static constexpr std::string_view kRec4 = "Rec4";
  static constexpr std::string_view kSolo4 = "Solo4";
  static constexpr std::string_view kMute4 = "Mute4";
  static constexpr std::string_view kSelect4 = "Select4";
  static constexpr std::string_view kFader4 = "Fader4";
  static constexpr std::string_view kPot4 = "Pot4";
  static constexpr std::string_view kPotButton4 = "PotButton4";
  static constexpr std::string_view kMeter4 = "Meter4";
  static constexpr std::string_view kScribble4Line1 = "Scribble4Line1";
  static constexpr std::string_view kScribble4Line2 = "Scribble4Line2";
  static constexpr std::string_view kScribble4Color = "Scribble4Color";

  static constexpr std::string_view kRec5 = "Rec5";
  static constexpr std::string_view kSolo5 = "Solo5";
  static constexpr std::string_view kMute5 = "Mute5";
  static constexpr std::string_view kSelect5 = "Select5";
  static constexpr std::string_view kFader5 = "Fader5";
  static constexpr std::string_view kPot5 = "Pot5";
  static constexpr std::string_view kPotButton5 = "PotButton5";
  static constexpr std::string_view kMeter5 = "Meter5";
  static constexpr std::string_view kScribble5Line1 = "Scribble5Line1";
  static constexpr std::string_view kScribble5Line2 = "Scribble5Line2";
  static constexpr std::string_view kScribble5Color = "Scribble5Color";

  static constexpr std::string_view kRec6 = "Rec6";
  static constexpr std::string_view kSolo6 = "Solo6";
  static constexpr std::string_view kMute6 = "Mute6";
  static constexpr std::string_view kSelect6 = "Select6";
  static constexpr std::string_view kFader6 = "Fader6";
  static constexpr std::string_view kPot6 = "Pot6";
  static constexpr std::string_view kPotButton6 = "PotButton6";
  static constexpr std::string_view kMeter6 = "Meter6";
  static constexpr std::string_view kScribble6Line1 = "Scribble6Line1";
  static constexpr std::string_view kScribble6Line2 = "Scribble3Line2";
  static constexpr std::string_view kScribble6Color = "Scribble6Color";

  static constexpr std::string_view kRec7 = "Rec7";
  static constexpr std::string_view kSolo7 = "Solo7";
  static constexpr std::string_view kMute7 = "Mute7";
  static constexpr std::string_view kSelect7 = "Select7";
  static constexpr std::string_view kFader7 = "Fader7";
  static constexpr std::string_view kPot7 = "Pot7";
  static constexpr std::string_view kPotButton7 = "PotButton7";
  static constexpr std::string_view kMeter7 = "Meter7";
  static constexpr std::string_view kScribble7Line1 = "Scribble7Line1";
  static constexpr std::string_view kScribble7Line2 = "Scribble7Line2";
  static constexpr std::string_view kScribble7Color = "Scribble7Color";

  static constexpr std::string_view kRec8 = "Rec8";
  static constexpr std::string_view kSolo8 = "Solo8";
  static constexpr std::string_view kMute8 = "Mute8";
  static constexpr std::string_view kSelect8 = "Select8";
  static constexpr std::string_view kFader8 = "Fader8";
  static constexpr std::string_view kPot8 = "Pot8";
  static constexpr std::string_view kPotButton8 = "PotButton8";
  static constexpr std::string_view kMeter8 = "Meter8";
  static constexpr std::string_view kScribble8Line1 = "Scribble8Line1";
  static constexpr std::string_view kScribble8Line2 = "Scribble8Line2";
  static constexpr std::string_view kScribble8Color = "Scribble8Color";

  // These convenience functions return the control names for per-track
  // controls. Track index and line are 0-based.
  static std::string Rec(int track_index) {
    return absl::StrCat("Rec", track_index + 1);
  }
  static std::string Solo(int track_index) {
    return absl::StrCat("Solo", track_index + 1);
  }
  static std::string Mute(int track_index) {
    return absl::StrCat("Mute", track_index + 1);
  }
  static std::string Select(int track_index) {
    return absl::StrCat("Select", track_index + 1);
  }
  static std::string Fader(int track_index) {
    return absl::StrCat("Fader", track_index + 1);
  }
  static std::string Pot(int track_index) {
    return absl::StrCat("Pot", track_index + 1);
  }
  static std::string PotButton(int track_index) {
    return absl::StrCat("PotButton", track_index + 1);
  }
  static std::string Meter(int track) {
    return absl::StrCat("Meter", track + 1);
  }
  static std::string Scribble(int track_index, int line) {
    return absl::StrCat("Scribble", track_index + 1, "Line", line + 1);
  }
  static std::string ScribbleColor(int track) {
    return absl::StrCat("Scribble", track + 1, "Color");
  }

  //----------------------------------------------------------------------------
  // XTouch only controls (not available on the Extender)
  //----------------------------------------------------------------------------

  // Master fader.
  static constexpr std::string_view kMasterFader = "MasterFader";

  // Buttons
  static constexpr std::string_view kAssignTrack = "AssignTrack";
  static constexpr std::string_view kAssignSend = "AssignSend";
  static constexpr std::string_view kAssignPan = "AssignPan";
  static constexpr std::string_view kAssignPlugin = "AssignPlugin";
  static constexpr std::string_view kAssignEQ = "AssignEQ";
  static constexpr std::string_view kAssignInst = "AssignInst";
  static constexpr std::string_view kBankLeft = "BankLeft";
  static constexpr std::string_view kBankRight = "BankRight";
  static constexpr std::string_view kChannelLeft = "ChannelLeft";
  static constexpr std::string_view kChannelRight = "ChannelRight";
  static constexpr std::string_view kGlobal = "Global";
  static constexpr std::string_view kFlip = "Flip";
  static constexpr std::string_view kShowNameValue = "ShowNameValue";
  static constexpr std::string_view kShowTimeBeats = "ShowTimeBeats";
  static constexpr std::string_view kF1 = "F1";
  static constexpr std::string_view kF2 = "F2";
  static constexpr std::string_view kF3 = "F3";
  static constexpr std::string_view kF4 = "F4";
  static constexpr std::string_view kF5 = "F5";
  static constexpr std::string_view kF6 = "F6";
  static constexpr std::string_view kF7 = "F7";
  static constexpr std::string_view kF8 = "F8";
  static constexpr std::string_view kViewMIDI = "ViewMIDI";
  static constexpr std::string_view kViewInputs = "ViewInputs";
  static constexpr std::string_view kViewAudio = "ViewAudio";
  static constexpr std::string_view kViewInst = "ViewInst";
  static constexpr std::string_view kViewAux = "ViewAux";
  static constexpr std::string_view kViewBuses = "ViewBuses";
  static constexpr std::string_view kViewOutputs = "ViewOutputs";
  static constexpr std::string_view kViewUser = "ViewUser";
  static constexpr std::string_view kShift = "Shift";
  static constexpr std::string_view kOption = "Option";
  static constexpr std::string_view kControl = "Control";
  static constexpr std::string_view kAlt = "Alt";
  static constexpr std::string_view kAutoRead = "AutoRead";
  static constexpr std::string_view kAutoWrite = "AutoWrite";
  static constexpr std::string_view kAutoTrim = "AutoTrim";
  static constexpr std::string_view kAutoTouch = "AutoTouch";
  static constexpr std::string_view kAutoLatch = "AutoLatch";
  static constexpr std::string_view kAutoGroup = "AutoGroup";
  static constexpr std::string_view kSave = "Save";
  static constexpr std::string_view kUndo = "Undo";
  static constexpr std::string_view kCancel = "Cancel";
  static constexpr std::string_view kEnter = "Enter";
  static constexpr std::string_view kMarker = "Marker";
  static constexpr std::string_view kNudge = "Nudge";
  static constexpr std::string_view kCycle = "Cycle";
  static constexpr std::string_view kDrop = "Drop";
  static constexpr std::string_view kReplace = "Replace";
  static constexpr std::string_view kClick = "Click";
  static constexpr std::string_view kSolo = "Solo";
  static constexpr std::string_view kRewind = "Rewind";
  static constexpr std::string_view kForward = "Forward";
  static constexpr std::string_view kStop = "Stop";
  static constexpr std::string_view kPlay = "Play";
  static constexpr std::string_view kRecord = "Record";
  static constexpr std::string_view kUp = "Up";
  static constexpr std::string_view kDown = "Down";
  static constexpr std::string_view kScrub = "Scrub";
  static constexpr std::string_view kZoom = "Zoom";
  static constexpr std::string_view kLeft = "Left";
  static constexpr std::string_view kRight = "Right";

  //----------------------------------------------------------------------------
  // Construction / Destruction
  //----------------------------------------------------------------------------

  // The type of X-Touch device to construct. The full X-Touch has more controls
  // than the X-Touch Extender, so this determines which controls will be added
  // to the device.
  enum class Type { kFull, kExtender };

  DeviceXTouch(Type type, RunRegistry& run_registry, MidiIn* midi_in,
               MidiOut* midi_out);
  ~DeviceXTouch() override;

 private:
  uint8_t scribble_colors_[8] = {};
};

}  // namespace jpr