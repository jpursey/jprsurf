// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/device/device_xtouch.h"

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/midi_sysex.h"
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

static constexpr int kScribbleLineLength = 56;
static constexpr int kScribbleTrackLineLength = 7;
static constexpr uint8_t kScribbleSysexPrefix[] = {0x00, 0x00, 0x66, 0x14,
                                                   0x12};

struct ScribbleConfig {
  int line;
  int offset;
  int length;
  std::string_view text;
};

ScribbleConfig ParseScribbleBytes(absl::Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    LOG(ERROR) << "Invalid X-Touch scribble strip message: empty message";
    return {.line = 0, .offset = 0, .length = 0};
  }
  if (bytes.size() > kScribbleLineLength + 1) {
    LOG(ERROR) << "Invalid X-Touch scribble strip message: message too long";
    return {.line = 0, .offset = 0, .length = 0};
  }
  int line = (bytes[0] < kScribbleLineLength ? 0 : 1);
  int offset = (line == 0 ? bytes[0] : bytes[0] - kScribbleLineLength);
  int length = std::clamp(static_cast<int>(bytes.size()) - 1, 0,
                          kScribbleLineLength - offset);
  const char* text_data = reinterpret_cast<const char*>(bytes.data() + 1);
  return {.line = line,
          .offset = offset,
          .length = length,
          .text = std::string_view(text_data, length)};
}

SysexMessage CreateScribbleMessage(ScribbleConfig config) {
  int line = std::clamp(config.line, 0, 1);
  int offset = std::clamp(config.offset, 0, kScribbleLineLength);
  int length = std::clamp(config.length, 0, kScribbleLineLength - offset);
  SysexMessage message(SysexPrefix(kScribbleSysexPrefix), length + 1);
  message.GetMutableData()[0] = line * kScribbleLineLength + offset;
  char* text_data =
      reinterpret_cast<char*>(message.GetMutableData().data() + 1);
  std::fill(text_data, text_data + length, ' ');
  if (!config.text.empty()) {
    std::memcpy(text_data, config.text.data(),
                std::min(length, static_cast<int>(config.text.size())));
  }
  return message;
}

// Manages the state for an X-Touch scribble strip that is updated by X-Touch
// scribble strip sysex messages.
class XTouchScribbleState final : public SysexMessageState {
 public:
  XTouchScribbleState(const SysexMessage& message);
  XTouchScribbleState(const XTouchScribbleState& other);
  XTouchScribbleState& operator=(const XTouchScribbleState&);
  ~XTouchScribbleState() override = default;

  // Returns the entire text for the scribble strip.
  std::string_view GetTopLine() const { return line_[0]; }
  std::string_view GetBottomLine() const { return line_[1]; }

  // Overrides for SysexMessageState.
  std::unique_ptr<SysexMessageState> Clone() const override;
  bool Update(const SysexMessage& message) override;

 private:
  std::string line_[2];
};

XTouchScribbleState::XTouchScribbleState(const SysexMessage& message) {
  line_[0] = std::string(kScribbleLineLength, ' ');
  line_[1] = std::string(kScribbleLineLength, ' ');
  ScribbleConfig config = ParseScribbleBytes(message.GetData());
  std::memcpy(line_[config.line].data() + config.offset, config.text.data(),
              config.text.size());
}

XTouchScribbleState::XTouchScribbleState(const XTouchScribbleState& other) {
  line_[0] = other.line_[0];
  line_[1] = other.line_[1];
}

XTouchScribbleState& XTouchScribbleState::operator=(
    const XTouchScribbleState& other) {
  if (this != &other) {
    line_[0] = other.line_[0];
    line_[1] = other.line_[1];
  }
  return *this;
}

std::unique_ptr<SysexMessageState> XTouchScribbleState::Clone() const {
  return std::make_unique<XTouchScribbleState>(*this);
}

bool XTouchScribbleState::Update(const SysexMessage& message) {
  if (message.GetPrefix() != SysexPrefix(kScribbleSysexPrefix)) {
    return false;
  }
  ScribbleConfig config = ParseScribbleBytes(message.GetData());
  std::string_view old_text(line_[config.line].data() + config.offset,
                            config.length);
  if (old_text == config.text) {
    return false;
  }
  std::memcpy(line_[config.line].data() + config.offset, config.text.data(),
              config.text.size());
  return true;
}

class XTouchScribbleSysex : public SysexMessageType {
 public:
  XTouchScribbleSysex();
  XTouchScribbleSysex(const XTouchScribbleSysex&) = delete;
  XTouchScribbleSysex& operator=(const XTouchScribbleSysex&) = delete;
  ~XTouchScribbleSysex() override = default;

  // Overrides for SysexMessageType.
  std::unique_ptr<SysexMessageState> CreateState(
      const SysexMessage& message) const override;
};

XTouchScribbleSysex::XTouchScribbleSysex()
    : SysexMessageType(SysexPrefix(kScribbleSysexPrefix)) {
  Register();
}

std::unique_ptr<SysexMessageState> XTouchScribbleSysex::CreateState(
    const SysexMessage& message) const {
  if (message.GetPrefix() != SysexPrefix(kScribbleSysexPrefix)) {
    return nullptr;
  }
  return std::make_unique<XTouchScribbleState>(message);
}

class XTouchTrackScribbleText final : public ControlTextOutput {
 public:
  XTouchTrackScribbleText(MidiOut* midi_out, int track, int line);
  XTouchTrackScribbleText(const XTouchTrackScribbleText&) = delete;
  XTouchTrackScribbleText& operator=(const XTouchTrackScribbleText&) = delete;
  ~XTouchTrackScribbleText() override = default;

 protected:
  // Overrides for ControlTextOutput.
  void OnTextChanged(std::string_view text, int mode) override;

 private:
  MidiOut* midi_out_;
  ScribbleConfig config_;
};

XTouchTrackScribbleText::XTouchTrackScribbleText(MidiOut* midi_out, int track,
                                                 int line)
    : midi_out_(midi_out),
      config_({.line = line,
               .offset = track * kScribbleTrackLineLength,
               .length = kScribbleTrackLineLength}) {}

void XTouchTrackScribbleText::OnTextChanged(std::string_view text, int mode) {
  ScribbleConfig config = config_;
  config.text = text;
  midi_out_->UpdateState(CreateScribbleMessage(config));
}

void RegisterSysex() {
  static absl::NoDestructor<XTouchScribbleSysex> instance;
  if (!instance->IsRegistered()) {
    if (!instance->Register()) {
      LOG(ERROR)
          << "Failed to register X-Touch scribble strip sysex message type";
    }
  }
}

}  // namespace

DeviceXTouch::DeviceXTouch(RunRegistry& run_registry, MidiIn* midi_in,
                           MidiOut* midi_out)
    : Device(run_registry) {
  // Ensure all sysex message types are registered.
  RegisterSysex();

  // Add all button controls.
  for (const auto& button : kButtons) {
    Control::Options options = {.name = button.name};
    options.press_input = std::make_unique<ControlPressInputMidiMsg>(
        midi_in,
        ControlPressInputMidiMsg::Config{
            .press = MidiNoteOn(/*channel=*/0, button.note, /*velocity=*/127),
            .release = MidiNoteOn(/*channel=*/0, button.note, /*velocity=*/0)});
    if (button.has_light) {
      options.dvalue_output = std::make_unique<ControlDValueOutputMidiNote>(
          midi_out,
          ControlDValueOutputMidiNote::Config{
              .channel = 0, .note = button.note, .use_note_off = false});
    }
    AddControl(std::move(options));
  }

  // Additional per-track controls.
  for (int track = 0; track < 8; ++track) {
    // Pan pots.
    std::string name = absl::StrCat("Pot", track + 1);
    Control::Options pan_options = {.name = name};
    pan_options.delta_input = std::make_unique<ControlDeltaInputMidiCcOnesComp>(
        midi_in, ControlDeltaInputMidiCcOnesComp::McuEncoder(track));
    pan_options.dvalue_output = std::make_unique<ControlDValueOutputMidiCc>(
        midi_out, ControlDValueOutputMidiCc::McuEncoder(track));
    AddControl(std::move(pan_options));

    // Faders.
    name = absl::StrCat("Fader", track + 1);
    Control::Options fader_options = {.name = name};
    fader_options.value_input = std::make_unique<ControlValueInputMcuFader>(
        midi_in, ControlValueInputMcuFader::Track(track));
    fader_options.press_input = std::make_unique<ControlPressInputMidiMsg>(
        midi_in,
        ControlPressInputMidiMsg::Config{
            .press = MidiNoteOn(/*channel=*/0, 0x68 + track, /*velocity=*/127),
            .release =
                MidiNoteOn(/*channel=*/0, 0x68 + track, /*velocity=*/0)});
    fader_options.cvalue_output = std::make_unique<ControlCValueOutputMcuFader>(
        midi_out, ControlCValueOutputMcuFader::Track(track));
    fader_options.binding = Control::Binding::kMotorized;
    AddControl(std::move(fader_options));

    // Scribble strip text.
    for (int line = 0; line < 2; ++line) {
      name = absl::StrCat("Scribble", track + 1, "Line", line + 1);
      Control::Options scribble_options = {.name = name};
      scribble_options.text_output =
          std::make_unique<XTouchTrackScribbleText>(midi_out, track, line);
      AddControl(std::move(scribble_options));
    }
  }
}

DeviceXTouch::~DeviceXTouch() = default;

}  // namespace jpr
