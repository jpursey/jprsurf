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

//==============================================================================
// Buttons
//==============================================================================

struct Button {
  std::string_view name;
  uint8_t note;
  bool has_light;
};

const Button kButtons[] = {
    // Track record arm buttons.
    {DeviceXTouch::kRec1, 0x00, true},
    {DeviceXTouch::kRec2, 0x01, true},
    {DeviceXTouch::kRec3, 0x02, true},
    {DeviceXTouch::kRec4, 0x03, true},
    {DeviceXTouch::kRec5, 0x04, true},
    {DeviceXTouch::kRec6, 0x05, true},
    {DeviceXTouch::kRec7, 0x06, true},
    {DeviceXTouch::kRec8, 0x07, true},
    // Track solo buttons.
    {DeviceXTouch::kSolo1, 0x08, true},
    {DeviceXTouch::kSolo2, 0x09, true},
    {DeviceXTouch::kSolo3, 0x0A, true},
    {DeviceXTouch::kSolo4, 0x0B, true},
    {DeviceXTouch::kSolo5, 0x0C, true},
    {DeviceXTouch::kSolo6, 0x0D, true},
    {DeviceXTouch::kSolo7, 0x0E, true},
    {DeviceXTouch::kSolo8, 0x0F, true},
    // Track mute buttons.
    {DeviceXTouch::kMute1, 0x10, true},
    {DeviceXTouch::kMute2, 0x11, true},
    {DeviceXTouch::kMute3, 0x12, true},
    {DeviceXTouch::kMute4, 0x13, true},
    {DeviceXTouch::kMute5, 0x14, true},
    {DeviceXTouch::kMute6, 0x15, true},
    {DeviceXTouch::kMute7, 0x16, true},
    {DeviceXTouch::kMute8, 0x17, true},
    // Track select buttons.
    {DeviceXTouch::kSelect1, 0x18, true},
    {DeviceXTouch::kSelect2, 0x19, true},
    {DeviceXTouch::kSelect3, 0x1A, true},
    {DeviceXTouch::kSelect4, 0x1B, true},
    {DeviceXTouch::kSelect5, 0x1C, true},
    {DeviceXTouch::kSelect6, 0x1D, true},
    {DeviceXTouch::kSelect7, 0x1E, true},
    {DeviceXTouch::kSelect8, 0x1F, true},
    // Track pot buttons.
    {DeviceXTouch::kPotButton1, 0x20, false},
    {DeviceXTouch::kPotButton2, 0x21, false},
    {DeviceXTouch::kPotButton3, 0x22, false},
    {DeviceXTouch::kPotButton4, 0x23, false},
    {DeviceXTouch::kPotButton5, 0x24, false},
    {DeviceXTouch::kPotButton6, 0x25, false},
    {DeviceXTouch::kPotButton7, 0x26, false},
    {DeviceXTouch::kPotButton8, 0x27, false},
    // Assign buttons
    {DeviceXTouch::kAssignTrack, 0x28, true},
    {DeviceXTouch::kAssignSend, 0x29, true},
    {DeviceXTouch::kAssignPan, 0x2A, true},
    {DeviceXTouch::kAssignPlugin, 0x2B, true},
    {DeviceXTouch::kAssignEQ, 0x2C, true},
    {DeviceXTouch::kAssignInst, 0x2D, true},
    // Channel navigation buttons.
    {DeviceXTouch::kBankLeft, 0x2E, true},
    {DeviceXTouch::kBankRight, 0x2F, true},
    {DeviceXTouch::kChannelLeft, 0x30, true},
    {DeviceXTouch::kChannelRight, 0x31, true},
    // Master fader buttons.
    {DeviceXTouch::kGlobal, 0x32, true},
    {DeviceXTouch::kFlip, 0x33, true},
    // Display buttons.
    {DeviceXTouch::kShowNameValue, 0x34, true},
    {DeviceXTouch::kShowTimeBeats, 0x35, true},
    // Function buttons.
    {DeviceXTouch::kF1, 0x36, true},
    {DeviceXTouch::kF2, 0x37, true},
    {DeviceXTouch::kF3, 0x38, true},
    {DeviceXTouch::kF4, 0x39, true},
    {DeviceXTouch::kF5, 0x3A, true},
    {DeviceXTouch::kF6, 0x3B, true},
    {DeviceXTouch::kF7, 0x3C, true},
    {DeviceXTouch::kF8, 0x3D, true},
    // View buttons.
    {DeviceXTouch::kViewMIDI, 0x3E, true},
    {DeviceXTouch::kViewInputs, 0x3F, true},
    {DeviceXTouch::kViewAudio, 0x40, true},
    {DeviceXTouch::kViewInst, 0x41, true},
    {DeviceXTouch::kViewAux, 0x42, true},
    {DeviceXTouch::kViewBuses, 0x43, true},
    {DeviceXTouch::kViewOutputs, 0x44, true},
    {DeviceXTouch::kViewUser, 0x45, true},
    // Modifier buttons.
    {DeviceXTouch::kShift, 0x46, true},
    {DeviceXTouch::kOption, 0x47, true},
    {DeviceXTouch::kControl, 0x48, true},
    {DeviceXTouch::kAlt, 0x49, true},
    // Automation buttons.
    {DeviceXTouch::kAutoRead, 0x4A, true},
    {DeviceXTouch::kAutoWrite, 0x4B, true},
    {DeviceXTouch::kAutoTrim, 0x4C, true},
    {DeviceXTouch::kAutoTouch, 0x4D, true},
    {DeviceXTouch::kAutoLatch, 0x4E, true},
    {DeviceXTouch::kAutoGroup, 0x4F, true},
    // Utility buttons.
    {DeviceXTouch::kSave, 0x50, true},
    {DeviceXTouch::kUndo, 0x51, true},
    {DeviceXTouch::kCancel, 0x52, true},
    {DeviceXTouch::kEnter, 0x53, true},
    // Transport buttons.
    {DeviceXTouch::kMarkers, 0x54, true},
    {DeviceXTouch::kNudge, 0x55, true},
    {DeviceXTouch::kCycle, 0x56, true},
    {DeviceXTouch::kDrop, 0x57, true},
    {DeviceXTouch::kReplace, 0x58, true},
    {DeviceXTouch::kClick, 0x59, true},
    {DeviceXTouch::kSolo, 0x5A, true},
    {DeviceXTouch::kRewind, 0x5B, true},
    {DeviceXTouch::kForward, 0x5C, true},
    {DeviceXTouch::kStop, 0x5D, true},
    {DeviceXTouch::kPlay, 0x5E, true},
    {DeviceXTouch::kRecord, 0x5F, true},
    // Navigation buttons.
    {DeviceXTouch::kUp, 0x60, true},
    {DeviceXTouch::kDown, 0x61, true},
    {DeviceXTouch::kScrub, 0x62, true},
    {DeviceXTouch::kZoom, 0x63, true},
    {DeviceXTouch::kLeft, 0x64, true},
    {DeviceXTouch::kRight, 0x65, true},
};

//==============================================================================
// Scribble strip
//==============================================================================

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
  line_[0] = std::string(kScribbleLineLength, 0);
  line_[1] = std::string(kScribbleLineLength, 0);
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

//==============================================================================
// DeviceXTouch
//==============================================================================

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
    std::string name = Pot(track);
    Control::Options pan_options = {.name = name};
    pan_options.delta_input = std::make_unique<ControlDeltaInputMidiCcOnesComp>(
        midi_in, ControlDeltaInputMidiCcOnesComp::McuEncoder(track));
    pan_options.dvalue_output = std::make_unique<ControlDValueOutputMidiCc>(
        midi_out, ControlDValueOutputMidiCc::McuEncoder(track));
    AddControl(std::move(pan_options));

    // Faders.
    name = Fader(track);
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
      name = Scribble(track, line);
      Control::Options scribble_options = {.name = name};
      scribble_options.text_output =
          std::make_unique<XTouchTrackScribbleText>(midi_out, track, line);
      AddControl(std::move(scribble_options));
    }
  }
}

DeviceXTouch::~DeviceXTouch() = default;

}  // namespace jpr
