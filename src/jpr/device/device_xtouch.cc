// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_xtouch.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "jpr/common/midi_port.h"
#include "jpr/common/midi_sysex.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_midi.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_output_midi.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

namespace {

//==============================================================================
// Control names
//==============================================================================

// The names of the fields in the catalogs that the constructor also names
// controls by. The buttons and lights are named by their tables below.
constexpr std::string_view kStrip = "strip";
constexpr std::string_view kFader = "fader";
constexpr std::string_view kPot = "pot";
constexpr std::string_view kMeter = "meter";
constexpr std::string_view kScribbleLines[] = {"scribble_top",
                                               "scribble_bottom"};
constexpr std::string_view kScribbleColor = "scribble_color";
constexpr std::string_view kMasterFader = "master_fader";
constexpr std::string_view kTimecode = "timecode";

// The constructor names these controls by DeviceXTouch's constants, which
// hide these in its scope.
static_assert(kMasterFader == DeviceXTouch::kMasterFader);
static_assert(kTimecode == DeviceXTouch::kTimecode);

// Returns the name of the field's control on the strip.
std::string GetStripControlName(int strip, std::string_view field) {
  return absl::StrCat(kStrip, "[", strip, "].", field);
}

//==============================================================================
// Buttons and lights
//==============================================================================

// A button on every strip, which sends the note on the first strip, and one
// more on each strip after it.
struct StripButton {
  std::string_view name;
  uint8_t note;
  bool has_light;
};

constexpr StripButton kStripButtons[] = {
    {"rec", 0x00, true},         //
    {"solo", 0x08, true},        //
    {"mute", 0x10, true},        //
    {"select", 0x18, true},      //
    {"pot_button", 0x20, false}  //
};

// A button or light that only the X-Touch has, and the note it sends or shows.
struct NamedNote {
  std::string_view name;
  uint8_t note;
};

// The buttons, apart from the function keys. Each one has a light.
constexpr NamedNote kButtons[] = {
    // Assign buttons
    {"assign_track", 0x28},
    {"assign_send", 0x29},
    {"assign_pan", 0x2A},
    {"assign_plugin", 0x2B},
    {"assign_eq", 0x2C},
    {"assign_inst", 0x2D},
    // Channel navigation buttons.
    {"bank_left", 0x2E},
    {"bank_right", 0x2F},
    {"channel_left", 0x30},
    {"channel_right", 0x31},
    // Master fader buttons.
    {"flip", 0x32},
    {"global", 0x33},
    // Display buttons.
    {"show_name_value", 0x34},
    {"show_time_beats", 0x35},
    // View buttons.
    {"view_midi", 0x3E},
    {"view_inputs", 0x3F},
    {"view_audio", 0x40},
    {"view_inst", 0x41},
    {"view_aux", 0x42},
    {"view_buses", 0x43},
    {"view_outputs", 0x44},
    {"view_user", 0x45},
    // Modifier buttons.
    {"shift", 0x46},
    {"option", 0x47},
    {"control", 0x48},
    {"alt", 0x49},
    // Automation buttons.
    {"auto_read", 0x4A},
    {"auto_write", 0x4B},
    {"auto_trim", 0x4C},
    {"auto_touch", 0x4D},
    {"auto_latch", 0x4E},
    {"auto_group", 0x4F},
    // Utility buttons.
    {"save", 0x50},
    {"undo", 0x51},
    {"cancel", 0x52},
    {"enter", 0x53},
    // Transport buttons.
    {"marker", 0x54},
    {"nudge", 0x55},
    {"cycle", 0x56},
    {"drop", 0x57},
    {"replace", 0x58},
    {"click", 0x59},
    {"solo", 0x5A},
    {"rewind", 0x5B},
    {"forward", 0x5C},
    {"stop", 0x5D},
    {"play", 0x5E},
    {"record", 0x5F},
    // Navigation buttons.
    {"up", 0x60},
    {"down", 0x61},
    {"scrub", 0x62},
    {"zoom", 0x63},
    {"left", 0x64},
    {"right", 0x65},
};

// The function keys, F1 to F8, are the array `function`, each with a light,
// from this note.
constexpr std::string_view kFunctionKeys = "function";
constexpr uint8_t kFunctionKeyNote = 0x36;
constexpr int kFunctionKeyCount = 8;

// The lights with no button.
constexpr NamedNote kLights[] = {
    {"smpte_light", 0x71},
    {"beats_light", 0x72},
    {"solo_light", 0x73},
};

//==============================================================================
// Catalogs
//==============================================================================

using Input = ControlInput::Type;
using Output = ControlOutput::Type;

// The modes of a pot's ring, those of ControlDValueOutputMidiCc::McuEncoder():
// the first four, then the same four with the outermost lights always on, then
// off.
constexpr std::string_view kRingModes[] = {
    "dot",       "center",       "fill",       "spread",        //
    "dot_edges", "center_edges", "fill_edges", "spread_edges",  //
    "off"};
constexpr int kRingOffMode = 8;
static_assert(kRingModes[kRingOffMode] == "off");

// Returns the shape of a light, whose modes are those of
// ControlDValueOutputMidiNote::McuLight().
WidgetShape GetLightShape(Control::Inputs inputs) {
  return WidgetShape::ForControl(inputs, {Output::kDValue}, {"solid", "blink"});
}

// Returns the shape of a fader, with touch.
WidgetShape GetFaderShape() {
  return WidgetShape::ForControl({Input::kValue, Input::kPress},
                                 {Output::kCValue});
}

// Returns the shape of a strip, the same on both models.
WidgetShape CreateStripShape() {
  std::vector<WidgetShape::Field> fields;
  for (const StripButton& button : kStripButtons) {
    fields.push_back({std::string(button.name),
                      button.has_light
                          ? GetLightShape({Input::kPress})
                          : WidgetShape::ForControl({Input::kPress}, {})});
  }
  fields.push_back({std::string(kFader), GetFaderShape()});
  fields.push_back(
      {std::string(kPot), WidgetShape::ForControl(
                              {Input::kDelta}, {Output::kDValue},
                              {std::begin(kRingModes), std::end(kRingModes)})});
  fields.push_back(
      {std::string(kMeter), WidgetShape::ForControl({}, {Output::kCValue})});
  const WidgetShape text = WidgetShape::ForControl({}, {Output::kText});
  for (std::string_view line : kScribbleLines) {
    fields.push_back({std::string(line), text});
  }
  fields.push_back({std::string(kScribbleColor),
                    WidgetShape::ForControl({}, {Output::kColor})});
  return WidgetShape::ForStruct(std::move(fields));
}

WidgetShape CreateCatalog(DeviceXTouch::Type type) {
  WidgetShape strips =
      WidgetShape::ForArray(CreateStripShape(), DeviceXTouch::kStripCount);
  if (type == DeviceXTouch::Type::kExtender) {
    return WidgetShape::ForStruct({{std::string(kStrip), std::move(strips)}});
  }

  const WidgetShape button = GetLightShape({Input::kPress});
  std::vector<WidgetShape::Field> fields;
  for (const NamedNote& entry : kButtons) {
    fields.push_back({std::string(entry.name), button});
  }
  fields.push_back({std::string(kFunctionKeys),
                    WidgetShape::ForArray(button, kFunctionKeyCount)});
  fields.push_back({std::string(kMasterFader), GetFaderShape()});
  fields.push_back(
      {std::string(kTimecode), WidgetShape::ForControl({}, {Output::kText})});
  for (const NamedNote& light : kLights) {
    fields.push_back({std::string(light.name), GetLightShape({})});
  }
  fields.push_back({std::string(kStrip), std::move(strips)});
  return WidgetShape::ForStruct(std::move(fields));
}

//==============================================================================
// VU meters
//==============================================================================

// Converts a linear peak amplitude to an MCU VU meter nibble value (0x0-0xE).
// Input is the linear amplitude from Track_GetPeakInfo (1.0 = 0dBFS).
uint8_t PeakToMcuMeter(double peak) {
  if (peak >= 1.0) {
    return 0xE;  // Clip
  }
  if (peak >= 0.631) {
    return 0xD;  // >= -4 dB
  }
  if (peak >= 0.398) {
    return 0xB;  // >= -8 dB
  }
  if (peak >= 0.200) {
    return 0xA;  // >= -14 dB
  }
  if (peak >= 0.100) {
    return 0x8;  // >= -20 dB
  }
  if (peak >= 0.0316) {
    return 0x6;  // >= -30 dB
  }
  if (peak >= 0.0100) {
    return 0x4;  // >= -40 dB
  }
  if (peak >= 0.00316) {
    return 0x2;  // >= -50 dB
  }
  return 0;
}

// inline constexpr uint8_t kTrackToLevel[8] = {0x02, 0x04, 0x06, 0x08,
//                                              0x0A, 0x0B, 0x0D, 0x0E};

// Output-only control that sends MCU VU meter updates for a single channel
// strip. Must be polled every run cycle; the hardware handles decay.
class XTouchTrackMeter final : public ControlCValueOutput {
 public:
  XTouchTrackMeter(MidiOut* midi_out, int track)
      : midi_out_(midi_out), track_(track) {}
  XTouchTrackMeter(const XTouchTrackMeter&) = delete;
  XTouchTrackMeter& operator=(const XTouchTrackMeter&) = delete;
  ~XTouchTrackMeter() override = default;

 protected:
  void OnValueChanged(double value, int mode) override {
    // value is a linear amplitude [0, 1+]. Convert to MCU nibble.
    uint8_t level = PeakToMcuMeter(value);
    // uint8_t level = kTrackToLevel[track_];
    uint8_t data = static_cast<uint8_t>((track_ << 4) | level);
    // Channel Pressure on MIDI channel 0: status=0xD0, data byte=sv.
    midi_out_->QueueMessage(MidiChannelPressure(/*channel=*/0, data));
  }

 private:
  MidiOut* midi_out_;
  int track_;  // 0-7
};

//==============================================================================
// Timecode display
//==============================================================================

// Number of digits in the MCU timecode display, right to left.
static constexpr int kTimecodeDigitCount = 10;

// Output control for the MCU 10-digit timecode display. Sends individual CC
// messages (CC 64-73, right to left) with dot bits encoded in the high nibble.
//
// The display has fixed field widths matching the XTouch hardware:
//   Field 0 (leftmost):  3 digits  -- bars / hours
//   Field 1:             2 digits  -- beats / minutes
//   Field 2:             2 digits  -- subdivisions / seconds
//   Field 3 (rightmost): 3 digits  -- ticks / frames
//
// A dot is placed after each field (on the last digit of each field) except
// the rightmost, which has no trailing dot.
class XTouchTimecodeDisplay final : public ControlTextOutput {
 public:
  explicit XTouchTimecodeDisplay(MidiOut* midi_out) : midi_out_(midi_out) {}
  XTouchTimecodeDisplay(const XTouchTimecodeDisplay&) = delete;
  XTouchTimecodeDisplay& operator=(const XTouchTimecodeDisplay&) = delete;
  ~XTouchTimecodeDisplay() override = default;

 protected:
  // Overrides for ControlTextOutput.
  void OnTimelineTextChanged(TimelinePosition position,
                             TimelineMode mode) override;
  void OnTextChanged(std::string_view text, int mode) override;

 private:
  // Sends the 10 encoded digit bytes to the display.
  void SendCodes(const uint8_t text[kTimecodeDigitCount]);

  // Writes `value` right-justified into a 3 digit field, zero padded to at
  // least `min_digits`, or "---" if it doesn't fit.
  static void FormatWideField(char field[4], int value, int min_digits = 1) {
    if (value < -99 || value > 999) {
      snprintf(field, 4, "---");
    } else {
      snprintf(field, 4, "%3.*d", min_digits, value);
    }
  }

  // Encodes one character for the MCU 7-segment display.
  // Bits 5-0: the character's display code, which is the low 6 bits of its
  // ASCII (0x20-0x5F): '@' to '_' are 0x00-0x1F, and ' ' to '?' are
  // 0x20-0x3F. Anything else is a space.
  // Bit 6: dot (decimal point on this digit).
  static uint8_t EncodeChar(char c, bool dot) {
    uint8_t encoded = (c >= 0x20 && c <= 0x5F) ? static_cast<uint8_t>(c & 0x3F)
                                               : static_cast<uint8_t>(' ');
    return encoded | (dot ? 0x40 : 0x00);
  }

  MidiOut* midi_out_;
};

void XTouchTimecodeDisplay::OnTimelineTextChanged(TimelinePosition position,
                                                  TimelineMode mode) {
  // The display has 4 fixed-width fields with dots between them:
  //   [FFF][FF][FF][FFF]   (widths: 3, 2, 2, 3)

  // Build 4 field strings, each right-justified into their fixed width.
  char fields[4][4] = {};  // +1 on each for null terminator from snprintf

  // Whether to place a dot after each of the first 3 fields.
  bool dots[3] = {false, false, false};

  switch (mode) {
    case TimelineMode::kBeats: {
      // Before the start, measures count down from 0, so a negative measure
      // is never 0.
      BeatsPosition b = position.ToBeats();
      FormatWideField(fields[0], b.negative ? -b.measure : b.measure);
      snprintf(fields[1], 3, "%2d", b.beat);
      dots[1] = true;
      snprintf(fields[2], 3, "%02d", b.division);
      snprintf(fields[3], 4, "   ");  // No ticks from REAPER beats API
      break;
    }
    case TimelineMode::kTime: {
      TimePosition t = position.ToTime();
      if (t.hours > 0) {
        FormatWideField(fields[0], t.negative ? -t.hours : t.hours);
        dots[0] = true;
        snprintf(fields[1], 3, "%02d", t.minutes);
      } else if (!t.negative) {
        snprintf(fields[0], 4, "   ");
        snprintf(fields[1], 3, "%2d", t.minutes);
      } else if (t.minutes < 10) {
        snprintf(fields[0], 4, "   ");
        snprintf(fields[1], 3, "-%d", t.minutes);
      } else {
        snprintf(fields[0], 4, "  -");
        snprintf(fields[1], 3, "%d", t.minutes);
      }
      dots[1] = true;
      snprintf(fields[2], 3, "%02d", t.seconds);
      dots[2] = true;
      snprintf(fields[3], 4, "%03d", t.milliseconds);
      break;
    }
    case TimelineMode::kFrames: {
      // Before the start, hours count down from 0, so a negative hour is never
      // 0.
      FramesPosition f = position.ToFrames();
      FormatWideField(fields[0], f.negative ? -f.hours : f.hours,
                      /*min_digits=*/2);
      dots[0] = true;
      snprintf(fields[1], 3, "%02d", f.minutes);
      dots[1] = true;
      snprintf(fields[2], 3, "%02d", f.seconds);
      dots[2] = true;
      snprintf(fields[3], 4, "%02d ", f.frames);
      break;
    }
    case TimelineMode::kSamples: {
      OnTextChanged(absl::StrCat(position.ToSamples()), 0);
      return;
    }
  }

  // Lay out the 4 fields into the 10-digit display, right to left.
  // Digit index 0 = CC 64 = rightmost display digit.
  // Field 3 occupies digits 0-2, field 2 digits 3-4, field 1 digits 5-6,
  // field 0 digits 7-9.
  // Dots appear on digit 3 (last digit of field 3->2 boundary),
  //                  digit 5 (last digit of field 2->1 boundary),
  //                  digit 7 (last digit of field 1->0 boundary).
  uint8_t codes[kTimecodeDigitCount] = {};

  // Field 3: codes 0-2 (rightmost field, width 3, no trailing dot)
  for (int i = 0; i < 3; ++i) {
    codes[i] = EncodeChar(fields[3][2 - i], false);
  }
  // Field 2: codes 3-4 (width 2, dot on digit 3 = last char of this field)
  codes[3] = EncodeChar(fields[2][1], /*dot=*/dots[2]);
  codes[4] = EncodeChar(fields[2][0], false);
  // Field 1: codes 5-6 (width 2, dot on digit 5 = last char of this field)
  codes[5] = EncodeChar(fields[1][1], /*dot=*/dots[1]);
  codes[6] = EncodeChar(fields[1][0], false);
  // Field 0: codes 7-9 (leftmost field, width 3, dot on digit 7)
  codes[7] = EncodeChar(fields[0][2], /*dot=*/dots[0]);
  codes[8] = EncodeChar(fields[0][1], false);
  codes[9] = EncodeChar(fields[0][0], false);

  SendCodes(codes);
}

void XTouchTimecodeDisplay::OnTextChanged(std::string_view text, int mode) {
  uint8_t codes[kTimecodeDigitCount] = {};
  int code_index = 0;
  bool next_dot = false;

  for (int i = static_cast<int>(text.size()) - 1;
       i >= 0 && code_index < kTimecodeDigitCount; --i) {
    char c = text[i];
    if (c == '.') {
      next_dot = true;
    } else {
      codes[code_index++] = EncodeChar(c, next_dot);
      next_dot = false;
    }
  }
  // Pad remaining codes with spaces.
  for (; code_index < kTimecodeDigitCount; ++code_index) {
    codes[code_index] = EncodeChar(' ', false);
  }

  SendCodes(codes);
}

void XTouchTimecodeDisplay::SendCodes(
    const uint8_t digits[kTimecodeDigitCount]) {
  for (int i = 0; i < kTimecodeDigitCount; ++i) {
    midi_out_->UpdateState(MidiCc(/*channel=*/0, 0x40 + i, digits[i]));
  }
}

//==============================================================================
// Scribble strip text
//==============================================================================

static constexpr int kScribbleLineLength = 56;
static constexpr int kScribbleTrackLineLength = 7;
static constexpr uint8_t kScribbleSysexPrefix[] = {0x00, 0x00, 0x66, 0x14,
                                                   0x12};
static constexpr uint8_t kScribbleExtSysexPrefix[] = {0x00, 0x00, 0x66, 0x15,
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

SysexMessage CreateScribbleMessage(SysexPrefix prefix, ScribbleConfig config) {
  int line = std::clamp(config.line, 0, 1);
  int offset = std::clamp(config.offset, 0, kScribbleLineLength);
  int length = std::clamp(config.length, 0, kScribbleLineLength - offset);
  SysexMessage message(prefix, length + 1);
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
  XTouchScribbleState(SysexPrefix prefix, const SysexMessage& message);
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
  SysexPrefix prefix_;
  std::string line_[2];
};

XTouchScribbleState::XTouchScribbleState(SysexPrefix prefix,
                                         const SysexMessage& message)
    : prefix_(prefix) {
  line_[0] = std::string(kScribbleLineLength, 0);
  line_[1] = std::string(kScribbleLineLength, 0);
  ScribbleConfig config = ParseScribbleBytes(message.GetData());
  std::memcpy(line_[config.line].data() + config.offset, config.text.data(),
              config.text.size());
}

XTouchScribbleState::XTouchScribbleState(const XTouchScribbleState& other)
    : prefix_(other.prefix_) {
  line_[0] = other.line_[0];
  line_[1] = other.line_[1];
}

XTouchScribbleState& XTouchScribbleState::operator=(
    const XTouchScribbleState& other) {
  if (this != &other) {
    prefix_ = other.prefix_;
    line_[0] = other.line_[0];
    line_[1] = other.line_[1];
  }
  return *this;
}

std::unique_ptr<SysexMessageState> XTouchScribbleState::Clone() const {
  return std::make_unique<XTouchScribbleState>(*this);
}

bool XTouchScribbleState::Update(const SysexMessage& message) {
  if (message.GetPrefix() != prefix_) {
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
  explicit XTouchScribbleSysex(SysexPrefix prefix);
  XTouchScribbleSysex(const XTouchScribbleSysex&) = delete;
  XTouchScribbleSysex& operator=(const XTouchScribbleSysex&) = delete;
  ~XTouchScribbleSysex() override = default;

  // Overrides for SysexMessageType.
  std::unique_ptr<SysexMessageState> CreateState(
      const SysexMessage& message) const override;
};

XTouchScribbleSysex::XTouchScribbleSysex(SysexPrefix prefix)
    : SysexMessageType(prefix) {
  Register();
}

std::unique_ptr<SysexMessageState> XTouchScribbleSysex::CreateState(
    const SysexMessage& message) const {
  if (message.GetPrefix() != GetPrefix()) {
    return nullptr;
  }
  return std::make_unique<XTouchScribbleState>(GetPrefix(), message);
}

class XTouchTrackScribbleText final : public ControlTextOutput {
 public:
  XTouchTrackScribbleText(SysexPrefix prefix, MidiOut* midi_out, int track,
                          int line);
  XTouchTrackScribbleText(const XTouchTrackScribbleText&) = delete;
  XTouchTrackScribbleText& operator=(const XTouchTrackScribbleText&) = delete;
  ~XTouchTrackScribbleText() override = default;

 protected:
  // Overrides for ControlTextOutput.
  void OnTextChanged(std::string_view text, int mode) override;

 private:
  SysexPrefix prefix_;
  MidiOut* midi_out_;
  ScribbleConfig config_;
};

XTouchTrackScribbleText::XTouchTrackScribbleText(SysexPrefix prefix,
                                                 MidiOut* midi_out, int track,
                                                 int line)
    : ControlTextOutput(/*mode_count=*/1, kScribbleTrackLineLength),
      prefix_(prefix),
      midi_out_(midi_out),
      config_({.line = line,
               .offset = track * kScribbleTrackLineLength,
               .length = kScribbleTrackLineLength}) {}

void XTouchTrackScribbleText::OnTextChanged(std::string_view text, int mode) {
  ScribbleConfig config = config_;
  config.text = text;
  midi_out_->UpdateState(CreateScribbleMessage(prefix_, config));
}

//==============================================================================
// Scribble strip colors
//==============================================================================

static constexpr uint8_t kColorSysexPrefix[] = {0x00, 0x00, 0x66, 0x14, 0x72};
static constexpr uint8_t kColorExtSysexPrefix[] = {0x00, 0x00, 0x66, 0x15,
                                                   0x72};
static constexpr int kColorChannelCount = 8;
static_assert(kColorChannelCount == DeviceXTouch::kStripCount);

// Maps a full RGB color to the nearest XTouch scribble strip palette color.
// The XTouch supports 8 colors corresponding to the corners of the RGB cube:
//   0x00=black, 0x01=red, 0x02=green, 0x03=yellow,
//   0x04=blue,  0x05=magenta, 0x06=cyan, 0x07=white
uint8_t RgbToXTouchColor(Color color) {
  if (color.r == 0 && color.g == 0 && color.b == 0) {
    return 0x00;  // Special case for black to avoid division by zero.
  }

  // Convert to normalized RGB values in the range [0, 1].
  float r = color.r / 255.0f;
  float g = color.g / 255.0f;
  float b = color.b / 255.0f;

  // Non-linearly darken all the colors by a fixed value to improve contrast
  // before scaling back up.
  float min = std::min({r, g, b}) / 2.0f;
  r -= min;
  g -= min;
  b -= min;

  // Scale the colors so that the brightest color is at full brightness, to
  // preserve the hue as much as possible when converting to the limited
  // palette.
  float scale = (1.0f / std::max({r, g, b}));
  r = std::min(r * scale, 1.0f);
  g = std::min(g * scale, 1.0f);
  b = std::min(b * scale, 1.0f);

  // Finally, convert to the nearest palette color by thresholding each channel
  // at 0.5 and encoding the result as bits in the output byte.
  return static_cast<uint8_t>((r > 0.5f ? 0x01 : 0x00) |
                              (g > 0.5f ? 0x02 : 0x00) |
                              (b > 0.5f ? 0x04 : 0x00));
}

// Manages the shared 8-channel color state for an X-Touch scribble strip
// color SysEx message (0x72 command). The wire message carries all 8 colors
// in a single payload; this state holds them and deduplicates the send.
class XTouchColorState final : public SysexMessageState {
 public:
  XTouchColorState(SysexPrefix prefix, const SysexMessage& message);
  XTouchColorState(const XTouchColorState& other);
  XTouchColorState& operator=(const XTouchColorState& other);
  ~XTouchColorState() override = default;

  std::unique_ptr<SysexMessageState> Clone() const override;
  bool Update(const SysexMessage& message) override;

 private:
  SysexPrefix prefix_;
  uint8_t colors_[kColorChannelCount] = {};
};

XTouchColorState::XTouchColorState(SysexPrefix prefix,
                                   const SysexMessage& message)
    : prefix_(prefix) {
  absl::Span<const uint8_t> data = message.GetData();
  int count = std::min(static_cast<int>(data.size()), kColorChannelCount);
  std::memcpy(colors_, data.data(), count);
}

XTouchColorState::XTouchColorState(const XTouchColorState& other)
    : prefix_(other.prefix_) {
  std::memcpy(colors_, other.colors_, kColorChannelCount);
}

XTouchColorState& XTouchColorState::operator=(const XTouchColorState& other) {
  if (this != &other) {
    prefix_ = other.prefix_;
    std::memcpy(colors_, other.colors_, kColorChannelCount);
  }
  return *this;
}

std::unique_ptr<SysexMessageState> XTouchColorState::Clone() const {
  return std::make_unique<XTouchColorState>(*this);
}

bool XTouchColorState::Update(const SysexMessage& message) {
  if (message.GetPrefix() != prefix_) {
    return false;
  }
  absl::Span<const uint8_t> data = message.GetData();
  int count = std::min(static_cast<int>(data.size()), kColorChannelCount);
  if (std::memcmp(colors_, data.data(), count) == 0) {
    return false;
  }
  std::memcpy(colors_, data.data(), count);
  return true;
}

class XTouchColorSysex : public SysexMessageType {
 public:
  explicit XTouchColorSysex(SysexPrefix prefix);
  XTouchColorSysex(const XTouchColorSysex&) = delete;
  XTouchColorSysex& operator=(const XTouchColorSysex&) = delete;
  ~XTouchColorSysex() override = default;

  std::unique_ptr<SysexMessageState> CreateState(
      const SysexMessage& message) const override;
};

XTouchColorSysex::XTouchColorSysex(SysexPrefix prefix)
    : SysexMessageType(prefix) {
  Register();
}

std::unique_ptr<SysexMessageState> XTouchColorSysex::CreateState(
    const SysexMessage& message) const {
  if (message.GetPrefix() != GetPrefix()) {
    return nullptr;
  }
  return std::make_unique<XTouchColorState>(GetPrefix(), message);
}

SysexMessage CreateColorMessage(SysexPrefix prefix,
                                const uint8_t colors[kColorChannelCount]) {
  SysexMessage message(prefix, kColorChannelCount);
  std::memcpy(message.GetMutableData().data(), colors, kColorChannelCount);
  return message;
}

// Per-channel color control for the X-Touch scribble strip. All 8 channel
// instances share the same colors array and midi_out; setting any one channel
// triggers a full 8-color SysEx send (suppressed by UpdateState if unchanged).
class XTouchTrackScribbleColor final : public ControlColorOutput {
 public:
  XTouchTrackScribbleColor(SysexPrefix prefix, MidiOut* midi_out, int track,
                           uint8_t* colors);
  XTouchTrackScribbleColor(const XTouchTrackScribbleColor&) = delete;
  XTouchTrackScribbleColor& operator=(const XTouchTrackScribbleColor&) = delete;
  ~XTouchTrackScribbleColor() override = default;

 protected:
  void OnColorChanged(Color color, int mode) override;

 private:
  SysexPrefix prefix_;
  MidiOut* midi_out_;
  int track_;
  uint8_t* colors_;  // Points into DeviceXTouch::scribble_colors_.
};

XTouchTrackScribbleColor::XTouchTrackScribbleColor(SysexPrefix prefix,
                                                   MidiOut* midi_out, int track,
                                                   uint8_t* colors)
    : prefix_(prefix), midi_out_(midi_out), track_(track), colors_(colors) {}

void XTouchTrackScribbleColor::OnColorChanged(Color color, int mode) {
  colors_[track_] = RgbToXTouchColor(color);
  midi_out_->UpdateState(CreateColorMessage(prefix_, colors_));
}

//==============================================================================
// Sysex registration
//==============================================================================

void RegisterSysex() {
  static absl::NoDestructor<XTouchScribbleSysex> full_instance{
      SysexPrefix(kScribbleSysexPrefix)};
  if (!full_instance->IsRegistered()) {
    if (!full_instance->Register()) {
      LOG(ERROR)
          << "Failed to register X-Touch scribble strip sysex message type";
    }
  }

  static absl::NoDestructor<XTouchScribbleSysex> ext_instance{
      SysexPrefix(kScribbleExtSysexPrefix)};
  if (!ext_instance->IsRegistered()) {
    if (!ext_instance->Register()) {
      LOG(ERROR) << "Failed to register X-Touch Extender scribble strip sysex "
                    "message type";
    }
  }

  static absl::NoDestructor<XTouchColorSysex> color_full_instance{
      SysexPrefix(kColorSysexPrefix)};
  if (!color_full_instance->IsRegistered()) {
    if (!color_full_instance->Register()) {
      LOG(ERROR) << "Failed to register X-Touch color sysex message type";
    }
  }

  static absl::NoDestructor<XTouchColorSysex> color_ext_instance{
      SysexPrefix(kColorExtSysexPrefix)};
  if (!color_ext_instance->IsRegistered()) {
    if (!color_ext_instance->Register()) {
      LOG(ERROR)
          << "Failed to register X-Touch Extender color sysex message type";
    }
  }
}

// The inputs and outputs a control may have, which the constructor builds only
// if the unit uses them (see Device::IsUsed()).
constexpr ControlInput::Type kValueInput = ControlInput::Type::kValue;
constexpr ControlInput::Type kDeltaInput = ControlInput::Type::kDelta;
constexpr ControlInput::Type kPressInput = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValueOutput = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValueOutput = ControlOutput::Type::kDValue;
constexpr ControlOutput::Type kTextOutput = ControlOutput::Type::kText;
constexpr ControlOutput::Type kColorOutput = ControlOutput::Type::kColor;

}  // namespace

//==============================================================================
// DeviceXTouch
//==============================================================================

DeviceXTouch::DeviceXTouch(Type type, RunRegistry& run_registry,
                           MidiInHandle input, MidiOutHandle output,
                           absl::Span<const ControlOverride> overrides)
    : Device(run_registry, overrides),
      midi_in_(std::move(input)),
      midi_out_(std::move(output)) {
  MidiIn* const midi_in = midi_in_.Get();
  MidiOut* const midi_out = midi_out_.Get();

  // Ensure all sysex message types are registered.
  RegisterSysex();

  // Add all button controls.
  for (const StripButton& button : kStripButtons) {
    for (int strip = 0; strip < kStripCount; ++strip) {
      AddButton(GetStripControlName(strip, button.name),
                static_cast<uint8_t>(button.note + strip), button.has_light,
                midi_in, midi_out);
    }
  }
  if (type != Type::kExtender) {
    for (const NamedNote& button : kButtons) {
      AddButton(button.name, button.note, /*has_light=*/true, midi_in,
                midi_out);
    }
    for (int key = 0; key < kFunctionKeyCount; ++key) {
      AddButton(absl::StrCat(kFunctionKeys, "[", key, "]"),
                static_cast<uint8_t>(kFunctionKeyNote + key),
                /*has_light=*/true, midi_in, midi_out);
    }

    // Timecode display.
    Control::Options timecode_options = {.name = kTimecode};
    if (IsUsed(kTimecode, kTextOutput)) {
      timecode_options.text_output =
          std::make_unique<XTouchTimecodeDisplay>(midi_out);
    }
    AddControl(std::move(timecode_options));

    for (const NamedNote& light : kLights) {
      Control::Options light_options = {.name = light.name};
      if (IsUsed(light.name, kDValueOutput)) {
        light_options.dvalue_output =
            std::make_unique<ControlDValueOutputMidiNote>(
                midi_out, ControlDValueOutputMidiNote::McuLight(light.note));
      }
      AddControl(std::move(light_options));
    }

    // Master fader.
    Control::Options master_fader_options = {.name = kMasterFader};
    if (IsUsed(kMasterFader, kValueInput)) {
      master_fader_options.value_input =
          std::make_unique<ControlValueInputMcuFader>(
              midi_in, ControlValueInputMcuFader::MasterFader());
    }
    if (IsUsed(kMasterFader, kPressInput)) {
      master_fader_options.press_input =
          std::make_unique<ControlPressInputMidiMsg>(
              midi_in, ControlPressInputMidiMsg::McuMasterFaderTouch());
    }
    if (IsUsed(kMasterFader, kCValueOutput)) {
      master_fader_options.cvalue_output =
          std::make_unique<ControlCValueOutputMcuFader>(
              midi_out, ControlCValueOutputMcuFader::MasterFader());
    }
    master_fader_options.binding = Control::Binding::kMotorized;
    AddControl(std::move(master_fader_options));
  }

  // Additional per-track controls.
  for (int track = 0; track < kStripCount; ++track) {
    // Pan pots.
    std::string name = GetStripControlName(track, kPot);
    Control::Options pan_options = {.name = name};
    if (IsUsed(name, kDeltaInput)) {
      pan_options.delta_input =
          std::make_unique<ControlDeltaInputMidiCcSignMagnitude>(
              midi_in, ControlDeltaInputMidiCcSignMagnitude::McuEncoder(track));
    }
    if (IsUsed(name, kDValueOutput)) {
      pan_options.dvalue_output = std::make_unique<ControlDValueOutputMidiCc>(
          midi_out, ControlDValueOutputMidiCc::McuEncoder(track));
      // Value 0 still lights an LED in most modes, but "off" turns them all
      // off.
      pan_options.dvalue_output->SetCleared(0, kRingOffMode);
    }
    AddControl(std::move(pan_options));

    // Faders.
    name = GetStripControlName(track, kFader);
    Control::Options fader_options = {.name = name};
    if (IsUsed(name, kValueInput)) {
      fader_options.value_input = std::make_unique<ControlValueInputMcuFader>(
          midi_in, ControlValueInputMcuFader::Track(track));
    }
    if (IsUsed(name, kPressInput)) {
      fader_options.press_input = std::make_unique<ControlPressInputMidiMsg>(
          midi_in, ControlPressInputMidiMsg::McuFaderTouch(track));
    }
    if (IsUsed(name, kCValueOutput)) {
      fader_options.cvalue_output =
          std::make_unique<ControlCValueOutputMcuFader>(
              midi_out, ControlCValueOutputMcuFader::Track(track));
    }
    fader_options.binding = Control::Binding::kMotorized;
    AddControl(std::move(fader_options));

    // VU meter.
    name = GetStripControlName(track, kMeter);
    Control::Options meter_options = {.name = name};
    if (IsUsed(name, kCValueOutput)) {
      meter_options.cvalue_output =
          std::make_unique<XTouchTrackMeter>(midi_out, track);
    }
    AddControl(std::move(meter_options));

    // Scribble strip text.
    SysexPrefix scribble_prefix =
        (type == Type::kFull ? SysexPrefix(kScribbleSysexPrefix)
                             : SysexPrefix(kScribbleExtSysexPrefix));
    for (int line = 0; line < std::ssize(kScribbleLines); ++line) {
      name = GetStripControlName(track, kScribbleLines[line]);
      Control::Options scribble_options = {.name = name};
      if (IsUsed(name, kTextOutput)) {
        scribble_options.text_output =
            std::make_unique<XTouchTrackScribbleText>(scribble_prefix, midi_out,
                                                      track, line);
      }
      AddControl(std::move(scribble_options));
    }

    // Scribble strip color.
    SysexPrefix color_prefix =
        (type == Type::kFull ? SysexPrefix(kColorSysexPrefix)
                             : SysexPrefix(kColorExtSysexPrefix));
    name = GetStripControlName(track, kScribbleColor);
    Control::Options color_options = {.name = name};
    if (IsUsed(name, kColorOutput)) {
      color_options.color_output = std::make_unique<XTouchTrackScribbleColor>(
          color_prefix, midi_out, track, scribble_colors_);
    }
    AddControl(std::move(color_options));
  }
}

DeviceXTouch::~DeviceXTouch() = default;

const WidgetShape& DeviceXTouch::GetCatalog(Type type) {
  if (type == Type::kFull) {
    static const absl::NoDestructor<WidgetShape> s_full(
        CreateCatalog(Type::kFull));
    return *s_full;
  }
  static const absl::NoDestructor<WidgetShape> s_extender(
      CreateCatalog(Type::kExtender));
  return *s_extender;
}

std::string DeviceXTouch::GetStripControl(int strip, std::string_view field) {
  return GetStripControlName(strip, field);
}

void DeviceXTouch::AddButton(std::string_view name, uint8_t note,
                             bool has_light, MidiIn* midi_in,
                             MidiOut* midi_out) {
  Control::Options options = {.name = name};
  if (IsUsed(name, kPressInput)) {
    options.press_input = std::make_unique<ControlPressInputMidiMsg>(
        midi_in, ControlPressInputMidiMsg::McuButton(note));
  }
  if (has_light && IsUsed(name, kDValueOutput)) {
    options.dvalue_output = std::make_unique<ControlDValueOutputMidiNote>(
        midi_out, ControlDValueOutputMidiNote::McuLight(note));
  }
  AddControl(std::move(options));
}

}  // namespace jpr
