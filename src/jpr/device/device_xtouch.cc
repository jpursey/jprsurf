// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/device/device_xtouch.h"

#include "jpr/common/midi_port.h"
#include "jpr/device/control_input_midi.h"
#include "jpr/device/control_output_midi.h"

namespace jpr {

namespace {

struct Button {
  std::string_view name;
  uint8_t note;
  bool has_light;
};

const Button kButtons[] = {
    // Track record arm buttons.
    {"Rec1", 0x00, true},
    {"Rec2", 0x01, true},
    {"Rec3", 0x02, true},
    {"Rec4", 0x03, true},
    {"Rec5", 0x04, true},
    {"Rec6", 0x05, true},
    {"Rec7", 0x06, true},
    {"Rec8", 0x07, true},
    // Track solo buttons.
    {"Solo1", 0x08, true},
    {"Solo2", 0x09, true},
    {"Solo3", 0x0A, true},
    {"Solo4", 0x0B, true},
    {"Solo5", 0x0C, true},
    {"Solo6", 0x0D, true},
    {"Solo7", 0x0E, true},
    {"Solo8", 0x0F, true},
    // Track mute buttons.
    {"Mute1", 0x10, true},
    {"Mute2", 0x11, true},
    {"Mute3", 0x12, true},
    {"Mute4", 0x13, true},
    {"Mute5", 0x14, true},
    {"Mute6", 0x15, true},
    {"Mute7", 0x16, true},
    {"Mute8", 0x17, true},
    // Track select buttons.
    {"Select1", 0x18, true},
    {"Select2", 0x19, true},
    {"Select3", 0x1A, true},
    {"Select4", 0x1B, true},
    {"Select5", 0x1C, true},
    {"Select6", 0x1D, true},
    {"Select7", 0x1E, true},
    {"Select8", 0x1F, true},
    // Track pot buttons.
    {"PotButton1", 0x20, false},
    {"PotButton2", 0x21, false},
    {"PotButton3", 0x22, false},
    {"PotButton4", 0x23, false},
    {"PotButton5", 0x24, false},
    {"PotButton6", 0x25, false},
    {"PotButton7", 0x26, false},
    {"PotButton8", 0x27, false},
    // Assign buttons
    {"AssignTrack", 0x28, true},
    {"AssignSend", 0x29, true},
    {"AssignPan", 0x2A, true},
    {"AssignPlugin", 0x2B, true},
    {"AssignEQ", 0x2C, true},
    {"AssignInst", 0x2D, true},
    // Channel navigation buttons.
    {"BankLeft", 0x2E, true},
    {"BankRight", 0x2F, true},
    {"ChannelLeft", 0x30, true},
    {"ChannelRight", 0x31, true},
    // Master fader buttons.
    {"Global", 0x32, true},
    {"Flip", 0x33, true},
    // Display buttons.
    {"ShowNameValue", 0x34, true},
    {"ShowTimeBeats", 0x35, true},
    // Function buttons.
    {"F1", 0x36, true},
    {"F2", 0x37, true},
    {"F3", 0x38, true},
    {"F4", 0x39, true},
    {"F5", 0x3A, true},
    {"F6", 0x3B, true},
    {"F7", 0x3C, true},
    {"F8", 0x3D, true},
    // View buttons.
    {"ViewMIDI", 0x3E, true},
    {"ViewInputs", 0x3F, true},
    {"ViewAudio", 0x40, true},
    {"ViewInst", 0x41, true},
    {"ViewAux", 0x42, true},
    {"ViewBuses", 0x43, true},
    {"ViewOutputs", 0x44, true},
    {"ViewUser", 0x45, true},
    // Modifier buttons.
    {"Shift", 0x46, true},
    {"Option", 0x47, true},
    {"Control", 0x48, true},
    {"Alt", 0x49, true},
    // Automation buttons.
    {"AutoRead", 0x4A, true},
    {"AutoWrite", 0x4B, true},
    {"AutoTrim", 0x4C, true},
    {"AutoTouch", 0x4D, true},
    {"AutoLatch", 0x4E, true},
    {"AutoGroup", 0x4F, true},
    // Utility buttons.
    {"Save", 0x50, true},
    {"Undo", 0x51, true},
    {"Cancel", 0x52, true},
    {"Enter", 0x53, true},
    // Transport buttons.
    {"Markers", 0x54, true},
    {"Nudge", 0x55, true},
    {"Cycle", 0x56, true},
    {"Drop", 0x57, true},
    {"Replace", 0x58, true},
    {"Click", 0x59, true},
    {"Solo", 0x5A, true},
    {"Rewind", 0x5B, true},
    {"Forward", 0x5C, true},
    {"Stop", 0x5D, true},
    {"Play", 0x5E, true},
    {"Record", 0x5F, true},
    // Navigation buttons.
    {"Up", 0x60, true},
    {"Down", 0x61, true},
    {"Scrub", 0x62, true},
    {"Zoom", 0x63, true},
    {"Left", 0x64, true},
    {"Right", 0x65, true},
};

}  // namespace

DeviceXTouch::DeviceXTouch(MidiIn* midi_in, MidiOut* midi_out) : Device() {
  for (const auto& button : kButtons) {
    Control::Options options = {.name = std::string(button.name)};
    options.press_input = std::make_unique<ControlPressInputMidiMsg>(
        midi_in, MidiNoteOn(/*channel=*/0, button.note, /*velocity=*/127),
        MidiNoteOn(/*channel=*/0, button.note, /*velocity=*/0));
    if (button.has_light) {
      options.dvalue_output = std::make_unique<ControlDValueOutputMidiNote>(
          midi_out, /*channel=*/0, button.note, /*use_note_off=*/false);
    }
    AddControl(std::make_unique<Control>(std::move(options)));
  }
}

}  // namespace jpr
