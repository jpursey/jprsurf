// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/testing/fake_xtouch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_message.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {

namespace {

//==============================================================================
// The Mackie Control protocol, as the X-Touch speaks it
//==============================================================================

// Every message but sysex is on MIDI channel 0, except the faders, which are
// on the fader's own channel.
constexpr uint8_t kSysexStart = 0xF0;
constexpr uint8_t kSysexEnd = 0xF7;

// A button or fader touch sends its note at velocity 127 when pressed, and 0
// when released. A light is sent its note at one of these.
constexpr uint8_t kPressed = 0x7F;
constexpr uint8_t kReleased = 0x00;
constexpr uint8_t kLightOff = 0x00;
constexpr uint8_t kLightBlinking = 0x01;
constexpr uint8_t kLightOn = 0x7F;

// The first note of each strip button, in StripButton order. Strip n's is
// this plus n.
constexpr uint8_t kStripButtonNotes[] = {
    0x00,  // kRec
    0x08,  // kSolo
    0x10,  // kMute
    0x18,  // kSelect
    0x20,  // kPotButton (V-Select), which has no light.
};

// The X-Touch's buttons' notes, in Button order. Every one has a light.
constexpr uint8_t kButtonNotes[] = {
    0x28,  // kAssignTrack
    0x29,  // kAssignSend
    0x2A,  // kAssignPan
    0x2B,  // kAssignPlugin
    0x2C,  // kAssignEQ
    0x2D,  // kAssignInst
    0x2E,  // kBankLeft
    0x2F,  // kBankRight
    0x30,  // kChannelLeft
    0x31,  // kChannelRight
    0x32,  // kFlip
    0x33,  // kGlobal
    0x34,  // kShowNameValue
    0x35,  // kShowTimeBeats
    0x36,  // kF1
    0x37,  // kF2
    0x38,  // kF3
    0x39,  // kF4
    0x3A,  // kF5
    0x3B,  // kF6
    0x3C,  // kF7
    0x3D,  // kF8
    0x3E,  // kViewMIDI
    0x3F,  // kViewInputs
    0x40,  // kViewAudio
    0x41,  // kViewInst
    0x42,  // kViewAux
    0x43,  // kViewBuses
    0x44,  // kViewOutputs
    0x45,  // kViewUser
    0x46,  // kShift
    0x47,  // kOption
    0x48,  // kControl
    0x49,  // kAlt
    0x4A,  // kAutoRead
    0x4B,  // kAutoWrite
    0x4C,  // kAutoTrim
    0x4D,  // kAutoTouch
    0x4E,  // kAutoLatch
    0x4F,  // kAutoGroup
    0x50,  // kSave
    0x51,  // kUndo
    0x52,  // kCancel
    0x53,  // kEnter
    0x54,  // kMarker
    0x55,  // kNudge
    0x56,  // kCycle
    0x57,  // kDrop
    0x58,  // kReplace
    0x59,  // kClick
    0x5A,  // kSolo
    0x5B,  // kRewind
    0x5C,  // kForward
    0x5D,  // kStop
    0x5E,  // kPlay
    0x5F,  // kRecord
    0x60,  // kUp
    0x61,  // kDown
    0x62,  // kScrub
    0x63,  // kZoom
    0x64,  // kLeft
    0x65,  // kRight
};
static_assert(static_cast<int>(std::size(kButtonNotes)) ==
              static_cast<int>(FakeXTouch::Button::kRight) + 1);

// The fader touch notes: fader n's is this plus n, so the master fader's is
// 0x70. They have no lights.
constexpr uint8_t kFaderTouchNote = 0x68;

// The lights with no button, in Led order.
constexpr uint8_t kLedNotes[] = {
    0x71,  // kSmpte
    0x72,  // kBeats
    0x73,  // kSolo
};

// Each strip's encoder turns send CC kPotCc plus the strip, and its ring is
// sent CC kRingCc plus the strip.
constexpr uint8_t kPotCc = 0x10;
constexpr uint8_t kRingCc = 0x30;

// A turn's value is the clicks in bits 0-5, with bit 6 set for
// counterclockwise.
constexpr int kMaxClicks = 0x3F;
constexpr uint8_t kCounterclockwise = 0x40;

// A ring's value is the position in bits 0-3, and the mode in bits 4-6.
constexpr int kMaxRingPosition = 11;

// The timecode display's digits are CC kTimecodeCc (the rightmost digit) to
// kTimecodeCc + 9 (the leftmost). Each is a display code in bits 0-5, and
// kTimecodeDot lights its dot.
constexpr uint8_t kTimecodeCc = 0x40;
constexpr uint8_t kTimecodeDot = 0x40;

// A meter level is the strip in bits 4-6, and the level in bits 0-3.
constexpr int kMaxMeterLevel = 0xE;

// The sysex header, before the device ID, and the commands that follow it.
constexpr uint8_t kSysexHeader[] = {kSysexStart, 0x00, 0x00, 0x66};
constexpr uint8_t kXTouchId = 0x14;
constexpr uint8_t kExtenderId = 0x15;
constexpr uint8_t kScribbleTextCommand = 0x12;
constexpr uint8_t kScribbleColorCommand = 0x72;

// Each control's note.
int ButtonNote(FakeXTouch::Button button) {
  return kButtonNotes[static_cast<int>(button)];
}
int StripButtonNote(FakeXTouch::StripButton button, int strip) {
  return kStripButtonNotes[static_cast<int>(button)] + strip;
}
int FaderTouchNote(int fader) { return kFaderTouchNote + fader; }

// Returns the character a timecode display code shows: 0x00-0x1F are '@' to
// '_', and 0x20-0x3F are ASCII.
char DecodeTimecodeChar(uint8_t code) {
  return static_cast<char>(code < 0x20 ? code + 0x40 : code);
}

// What CheckFull() names for the buttons and lights only the X-Touch has.
constexpr std::string_view kMasterButtons = "buttons outside the strips";
constexpr std::string_view kMasterLights = "lights outside the strips";

}  // namespace

//==============================================================================
// FakeXTouch
//==============================================================================

FakeXTouch::FakeXTouch(FakeReaper& reaper, Type type,
                       std::string_view port_name)
    : reaper_(reaper),
      type_(type),
      name_(type == Type::kFull ? "X-Touch" : "X-Touch Extender"),
      input_(reaper.AddMidiInput(port_name)),
      output_(reaper.AddMidiOutput(port_name)),
      scribble_(2 * kScribbleLineLength, ' ') {
  for (int strip = 0; strip < kStripCount; ++strip) {
    for (StripButton button : {StripButton::kRec, StripButton::kSolo,
                               StripButton::kMute, StripButton::kSelect}) {
      has_light_[StripButtonNote(button, strip)] = true;
    }
  }
  if (type_ == Type::kFull) {
    for (uint8_t note : kButtonNotes) {
      has_light_[note] = true;
    }
    for (uint8_t note : kLedNotes) {
      has_light_[note] = true;
    }
  }
  timecode_.fill(' ');
  output_->Connect([this](absl::Span<const uint8_t> bytes) { Receive(bytes); });
}

FakeXTouch::~FakeXTouch() { output_->Disconnect(); }

//------------------------------------------------------------------------------
// Into JPRSurf
//------------------------------------------------------------------------------

void FakeXTouch::Press(Button button) {
  if (CheckFull(kMasterButtons)) {
    SendNote(ButtonNote(button), /*pressed=*/true);
  }
}

void FakeXTouch::Press(StripButton button, int strip) {
  if (CheckStrip(strip)) {
    SendNote(StripButtonNote(button, strip), /*pressed=*/true);
  }
}

void FakeXTouch::Release(Button button) {
  if (CheckFull(kMasterButtons)) {
    SendNote(ButtonNote(button), /*pressed=*/false);
  }
}

void FakeXTouch::Release(StripButton button, int strip) {
  if (CheckStrip(strip)) {
    SendNote(StripButtonNote(button, strip), /*pressed=*/false);
  }
}

void FakeXTouch::TouchFader(int fader) {
  if (CheckFader(fader)) {
    SendNote(FaderTouchNote(fader), /*pressed=*/true);
  }
}

void FakeXTouch::ReleaseFader(int fader) {
  if (CheckFader(fader)) {
    SendNote(FaderTouchNote(fader), /*pressed=*/false);
  }
}

void FakeXTouch::MoveFader(int fader, int position) {
  if (!CheckFader(fader)) {
    return;
  }
  if (position < 0 || position > kFaderMax) {
    ADD_FAILURE() << name_ << " fader position " << position << " is outside 0-"
                  << kFaderMax;
    return;
  }
  faders_[fader] = position;
  input_->Send(MidiPitchBend(static_cast<uint8_t>(fader),
                             static_cast<uint16_t>(position)));
}

void FakeXTouch::TurnPot(int strip, int clicks) {
  if (!CheckStrip(strip)) {
    return;
  }
  if (clicks == 0 || std::abs(clicks) > kMaxClicks) {
    ADD_FAILURE() << name_ << " pot turn of " << clicks << " clicks is not 1-"
                  << kMaxClicks << " either way";
    return;
  }
  const uint8_t value =
      static_cast<uint8_t>(clicks > 0 ? clicks : (kCounterclockwise | -clicks));
  input_->Send(
      MidiCc(/*channel=*/0, static_cast<uint8_t>(kPotCc + strip), value));
}

void FakeXTouch::SendNote(int note, bool pressed) {
  input_->Send(MidiNoteOn(/*channel=*/0, static_cast<uint8_t>(note),
                          pressed ? kPressed : kReleased));
}

//------------------------------------------------------------------------------
// What the hardware shows
//------------------------------------------------------------------------------

FakeXTouch::Light FakeXTouch::GetLight(Button button) const {
  if (!CheckFull(kMasterButtons)) {
    return Light::kOff;
  }
  return lights_[ButtonNote(button)];
}

FakeXTouch::Light FakeXTouch::GetLight(StripButton button, int strip) const {
  if (button == StripButton::kPotButton) {
    ADD_FAILURE() << name_ << " pot buttons have no light";
    return Light::kOff;
  }
  if (!CheckStrip(strip)) {
    return Light::kOff;
  }
  return lights_[StripButtonNote(button, strip)];
}

FakeXTouch::Light FakeXTouch::GetLight(Led led) const {
  if (!CheckFull(kMasterLights)) {
    return Light::kOff;
  }
  return lights_[kLedNotes[static_cast<int>(led)]];
}

int FakeXTouch::GetFader(int fader) const {
  return CheckFader(fader) ? faders_[fader] : 0;
}

FakeXTouch::Ring FakeXTouch::GetRing(int strip) const {
  return CheckStrip(strip) ? rings_[strip] : Ring{};
}

int FakeXTouch::GetMeter(int strip) const {
  if (!CheckStrip(strip)) {
    return 0;
  }

  // Whole runs since the level was sent, allowing for rounding in the clock.
  const Meter& meter = meters_[strip];
  const int runs = static_cast<int>(std::floor(
      (reaper_.GetTime() - meter.time) * FakeReaper::kRunsPerSecond + 1e-6));
  return std::max(meter.level - runs, 0);
}

std::string FakeXTouch::GetScribble(int strip, int line) const {
  if (!CheckStrip(strip)) {
    return "";
  }
  if (line < 0 || line > 1) {
    ADD_FAILURE() << name_ << " scribble strip line " << line
                  << " is not 0 or 1";
    return "";
  }
  return scribble_.substr(
      line * kScribbleLineLength + strip * kScribbleStripLength,
      kScribbleStripLength);
}

FakeXTouch::ScribbleColor FakeXTouch::GetScribbleColor(int strip) const {
  return CheckStrip(strip) ? colors_[strip] : ScribbleColor::kBlack;
}

std::string FakeXTouch::GetTimecode() const {
  if (!CheckFull("timecode display")) {
    return "";
  }
  std::string text;
  for (int digit = kTimecodeDigits - 1; digit >= 0; --digit) {
    const uint8_t code = timecode_[digit];
    text.push_back(DecodeTimecodeChar(code & 0x3F));
    if ((code & kTimecodeDot) != 0) {
      text.push_back('.');
    }
  }
  return text;
}

//------------------------------------------------------------------------------
// Decoding what is sent to the hardware
//------------------------------------------------------------------------------

void FakeXTouch::Receive(absl::Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    FailMessage(bytes, "it is empty");
    return;
  }
  const uint8_t status = bytes[0];
  if (status == kSysexStart) {
    ReceiveSysex(bytes);
    return;
  }

  // Channel pressure has one data byte, and the rest have two.
  const int data_size = (status == MidiChannelPressureStatus(0) ? 1 : 2);
  if (static_cast<int>(bytes.size()) < 1 + data_size) {
    FailMessage(bytes, "it is too short");
    return;
  }
  if (status == MidiNoteOnStatus(0)) {
    ReceiveNote(bytes);
  } else if (status == MidiCcStatus(0)) {
    ReceiveCc(bytes);
  } else if (status == MidiChannelPressureStatus(0)) {
    ReceiveMeter(bytes);
  } else if ((status & 0xF0) == MidiPitchBendStatus(0)) {
    ReceiveFader(bytes);
  } else {
    FailMessage(bytes, "the fake doesn't decode it");
  }
}

void FakeXTouch::ReceiveNote(absl::Span<const uint8_t> bytes) {
  const uint8_t note = bytes[1];
  if (note >= static_cast<int>(has_light_.size()) || !has_light_[note]) {
    FailMessage(bytes, "the note has no light");
    return;
  }
  switch (bytes[2]) {
    case kLightOff:
      lights_[note] = Light::kOff;
      break;
    case kLightBlinking:
      lights_[note] = Light::kBlinking;
      break;
    case kLightOn:
      lights_[note] = Light::kOn;
      break;
    default:
      FailMessage(bytes,
                  "a light's velocity is 0 (off), 1 (blinking), or 127 (on)");
      break;
  }
}

void FakeXTouch::ReceiveCc(absl::Span<const uint8_t> bytes) {
  const uint8_t control = bytes[1];
  const uint8_t value = bytes[2];
  if (control >= kRingCc && control < kRingCc + kStripCount) {
    const int position = value & 0x0F;
    if (position > kMaxRingPosition) {
      FailMessage(bytes, "a ring's position is 0-11");
      return;
    }
    rings_[control - kRingCc] = {.mode = (value >> 4) & 0x07,
                                 .position = position};
    return;
  }
  if (control >= kTimecodeCc && control < kTimecodeCc + kTimecodeDigits) {
    if (type_ != Type::kFull) {
      FailMessage(bytes, "the extender has no timecode display");
      return;
    }
    timecode_[control - kTimecodeCc] = value;
    return;
  }
  FailMessage(bytes, "the fake doesn't decode the control");
}

void FakeXTouch::ReceiveMeter(absl::Span<const uint8_t> bytes) {
  const uint8_t value = bytes[1];
  const int strip = value >> 4;
  const int level = value & 0x0F;
  if (strip >= kStripCount) {
    FailMessage(bytes, "there are 8 meters");
    return;
  }
  if (level > kMaxMeterLevel) {
    FailMessage(bytes, "a meter's level is 0x0-0xE");
    return;
  }
  meters_[strip] = {.level = level, .time = reaper_.GetTime()};
}

void FakeXTouch::ReceiveFader(absl::Span<const uint8_t> bytes) {
  const int fader = bytes[0] & 0x0F;
  if (!HasFader(fader)) {
    FailMessage(bytes, "there is no fader on the channel");
    return;
  }
  faders_[fader] = (bytes[2] << 7) | bytes[1];
}

void FakeXTouch::ReceiveSysex(absl::Span<const uint8_t> bytes) {
  // The header, device ID, command, and the end.
  constexpr int kHeaderSize = static_cast<int>(std::size(kSysexHeader));
  constexpr int kMinSize = kHeaderSize + 3;
  const int size = static_cast<int>(bytes.size());
  if (size < kMinSize || bytes.back() != kSysexEnd ||
      bytes.subspan(0, kHeaderSize) != absl::MakeConstSpan(kSysexHeader)) {
    FailMessage(bytes, "the fake doesn't decode the sysex");
    return;
  }
  const uint8_t id = (type_ == Type::kFull ? kXTouchId : kExtenderId);
  if (bytes[kHeaderSize] != id) {
    FailMessage(bytes, "the device ID is the other model's");
    return;
  }
  const uint8_t command = bytes[kHeaderSize + 1];
  const absl::Span<const uint8_t> data =
      bytes.subspan(kHeaderSize + 2, size - kMinSize);
  if (command == kScribbleTextCommand) {
    ReceiveScribbleText(bytes, data);
  } else if (command == kScribbleColorCommand) {
    ReceiveScribbleColors(bytes, data);
  } else {
    FailMessage(bytes, "the fake doesn't decode the sysex command");
  }
}

void FakeXTouch::ReceiveScribbleText(absl::Span<const uint8_t> bytes,
                                     absl::Span<const uint8_t> data) {
  if (data.empty()) {
    FailMessage(bytes, "scribble text needs an offset");
    return;
  }
  const int offset = data[0];
  const absl::Span<const uint8_t> text = data.subspan(1);
  if (offset + static_cast<int>(text.size()) >
      static_cast<int>(scribble_.size())) {
    FailMessage(bytes, "the text runs past the end of the display");
    return;
  }
  scribble_.replace(offset, text.size(),
                    reinterpret_cast<const char*>(text.data()), text.size());
}

void FakeXTouch::ReceiveScribbleColors(absl::Span<const uint8_t> bytes,
                                       absl::Span<const uint8_t> data) {
  if (static_cast<int>(data.size()) != kStripCount) {
    FailMessage(bytes, "scribble colors are one for each of the 8 strips");
    return;
  }
  for (uint8_t color : data) {
    if (color > static_cast<uint8_t>(ScribbleColor::kWhite)) {
      FailMessage(bytes, "a scribble color is 0-7");
      return;
    }
  }
  for (int strip = 0; strip < kStripCount; ++strip) {
    colors_[strip] = static_cast<ScribbleColor>(data[strip]);
  }
}

void FakeXTouch::FailMessage(absl::Span<const uint8_t> bytes,
                             std::string_view why) const {
  ADD_FAILURE() << name_ << " can't take \""
                << absl::StrJoin(bytes, " ",
                                 [](std::string* out, uint8_t byte) {
                                   absl::StrAppendFormat(out, "%02X", byte);
                                 })
                << "\": " << why;
}

//------------------------------------------------------------------------------
// Checks
//------------------------------------------------------------------------------

bool FakeXTouch::HasFader(int fader) const {
  return fader >= 0 && (fader < kMasterFader ||
                        (fader == kMasterFader && type_ == Type::kFull));
}

bool FakeXTouch::CheckFull(std::string_view what) const {
  if (type_ != Type::kFull) {
    ADD_FAILURE() << name_ << " has no " << what;
    return false;
  }
  return true;
}

bool FakeXTouch::CheckStrip(int strip) const {
  if (strip < 0 || strip >= kStripCount) {
    ADD_FAILURE() << name_ << " strip " << strip << " is not 0-"
                  << kStripCount - 1;
    return false;
  }
  return true;
}

bool FakeXTouch::CheckFader(int fader) const {
  if (!HasFader(fader)) {
    ADD_FAILURE() << name_ << " has no fader " << fader;
    return false;
  }
  return true;
}

}  // namespace jpr
