// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/testing/fake_xtouch.h"

#include <cstdint>
#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

using Button = FakeXTouch::Button;
using Led = FakeXTouch::Led;
using Light = FakeXTouch::Light;
using Ring = FakeXTouch::Ring;
using ScribbleColor = FakeXTouch::ScribbleColor;
using StripButton = FakeXTouch::StripButton;

//==============================================================================
// The fake against the protocol's raw bytes, from JPRSurf's end of its ports.
//==============================================================================

class FakeXTouchTest : public ::testing::Test {
 protected:
  explicit FakeXTouchTest(FakeXTouch::Type type = FakeXTouch::Type::kFull)
      : id_(type == FakeXTouch::Type::kFull ? 0x14 : 0x15),
        xtouch_(reaper_, type, "X-Touch") {
    input_->start();
  }
  ~FakeXTouchTest() override {
    input_->Destroy();
    output_->Destroy();
  }

  // Sends a message to the hardware: three bytes with Send(), as JPRSurf's
  // MIDI output sends short messages, and anything else with SendMsg().
  void Send(const std::vector<uint8_t>& bytes) {
    if (bytes.size() == 3) {
      output_->Send(bytes[0], bytes[1], bytes[2], /*frame_offset=*/-1);
      return;
    }
    FakeMidiEventList events;
    events.Add(bytes, /*frame_offset=*/-1);
    int bpos = 0;
    output_->SendMsg(events.EnumItems(&bpos), /*frame_offset=*/-1);
  }

  // Sends this model's sysex `command` with `data`.
  void SendSysex(uint8_t command, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> bytes = {0xF0, 0x00, 0x00, 0x66, id_, command};
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.push_back(0xF7);
    Send(bytes);
  }

  // Returns every message the hardware sent since the last call.
  std::vector<std::vector<uint8_t>> ReadInput() {
    input_->SwapBufsPrecise(0, ::time_precise());
    std::vector<std::vector<uint8_t>> messages;
    int bpos = 0;
    for (MIDI_event_t* event = input_->GetReadBuf()->EnumItems(&bpos);
         event != nullptr; event = input_->GetReadBuf()->EnumItems(&bpos)) {
      messages.emplace_back(event->midi_message,
                            event->midi_message + event->size);
    }
    return messages;
  }

  const uint8_t id_;  // The model's sysex device ID.
  FakeReaper reaper_;
  FakeXTouch xtouch_;
  midi_Input* input_ = ::CreateMIDIInput(0);
  midi_Output* output_ = ::CreateMIDIOutput(0, false, nullptr);
};

class FakeXTouchExtenderTest : public FakeXTouchTest {
 protected:
  FakeXTouchExtenderTest() : FakeXTouchTest(FakeXTouch::Type::kExtender) {}
};

//------------------------------------------------------------------------------
// Into JPRSurf
//------------------------------------------------------------------------------

TEST_F(FakeXTouchTest, ButtonsSendTheirNotes) {
  xtouch_.Press(Button::kPlay);
  xtouch_.Release(Button::kPlay);
  xtouch_.Press(Button::kAssignTrack);
  xtouch_.Press(Button::kF8);
  xtouch_.Press(Button::kRight);
  EXPECT_THAT(
      ReadInput(),
      ElementsAre(ElementsAre(0x90, 0x5E, 0x7F), ElementsAre(0x90, 0x5E, 0x00),
                  ElementsAre(0x90, 0x28, 0x7F), ElementsAre(0x90, 0x3D, 0x7F),
                  ElementsAre(0x90, 0x65, 0x7F)));
}

TEST_F(FakeXTouchTest, StripButtonsSendTheirNotes) {
  xtouch_.Press(StripButton::kRec, 0);
  xtouch_.Press(StripButton::kSolo, 1);
  xtouch_.Press(StripButton::kMute, 2);
  xtouch_.Press(StripButton::kSelect, 3);
  xtouch_.Press(StripButton::kPotButton, 7);
  xtouch_.Release(StripButton::kPotButton, 7);
  EXPECT_THAT(
      ReadInput(),
      ElementsAre(ElementsAre(0x90, 0x00, 0x7F), ElementsAre(0x90, 0x09, 0x7F),
                  ElementsAre(0x90, 0x12, 0x7F), ElementsAre(0x90, 0x1B, 0x7F),
                  ElementsAre(0x90, 0x27, 0x7F),
                  ElementsAre(0x90, 0x27, 0x00)));
}

TEST_F(FakeXTouchTest, FaderTouchesSendTheirNotes) {
  xtouch_.TouchFader(0);
  xtouch_.ReleaseFader(7);
  xtouch_.TouchFader(FakeXTouch::kMasterFader);
  xtouch_.ReleaseFader(FakeXTouch::kMasterFader);
  EXPECT_THAT(ReadInput(), ElementsAre(ElementsAre(0x90, 0x68, 0x7F),
                                       ElementsAre(0x90, 0x6F, 0x00),
                                       ElementsAre(0x90, 0x70, 0x7F),
                                       ElementsAre(0x90, 0x70, 0x00)));
}

TEST_F(FakeXTouchTest, FaderMovesSendPitchBendAndMoveTheFader) {
  xtouch_.MoveFader(3, 12720);
  xtouch_.MoveFader(FakeXTouch::kMasterFader, FakeXTouch::kFaderMax);
  EXPECT_THAT(ReadInput(), ElementsAre(ElementsAre(0xE3, 0x30, 0x63),
                                       ElementsAre(0xE8, 0x7F, 0x7F)));
  EXPECT_EQ(xtouch_.GetFader(3), 12720);
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), FakeXTouch::kFaderMax);
}

TEST_F(FakeXTouchTest, ReleasedFaderGoesBackToWhereItWasSent) {
  Send({0xE0, 0x30, 0x63});  // 12720
  xtouch_.TouchFader(0);
  xtouch_.MoveFader(0, 1000);
  EXPECT_EQ(xtouch_.GetFader(0), 1000);
  xtouch_.ReleaseFader(0);
  EXPECT_EQ(xtouch_.GetFader(0), 12720);
}

TEST_F(FakeXTouchTest, PotTurnsSendTheirClicks) {
  xtouch_.TurnPot(0, 1);
  xtouch_.TurnPot(5, -3);
  xtouch_.TurnPot(7, 63);
  xtouch_.TurnPot(7, -63);
  EXPECT_THAT(ReadInput(), ElementsAre(ElementsAre(0xB0, 0x10, 0x01),
                                       ElementsAre(0xB0, 0x15, 0x43),
                                       ElementsAre(0xB0, 0x17, 0x3F),
                                       ElementsAre(0xB0, 0x17, 0x7F)));
}

TEST_F(FakeXTouchTest, InputsOutOfRangeFailTheTestAndSendNothing) {
  EXPECT_NONFATAL_FAILURE(xtouch_.Press(StripButton::kMute, 8),
                          "strip 8 is not 0-7");
  EXPECT_NONFATAL_FAILURE(xtouch_.TouchFader(9), "has no fader 9");
  EXPECT_NONFATAL_FAILURE(xtouch_.MoveFader(0, FakeXTouch::kFaderMax + 1),
                          "is outside 0-16383");
  EXPECT_NONFATAL_FAILURE(xtouch_.MoveFader(0, -1), "is outside 0-16383");
  EXPECT_NONFATAL_FAILURE(xtouch_.TurnPot(0, 0), "is not 1-63 either way");
  EXPECT_NONFATAL_FAILURE(xtouch_.TurnPot(0, -64), "is not 1-63 either way");
  EXPECT_THAT(ReadInput(), IsEmpty());
  EXPECT_EQ(xtouch_.GetFader(0), 0);
}

//------------------------------------------------------------------------------
// What the hardware shows
//------------------------------------------------------------------------------

TEST_F(FakeXTouchTest, StartsOffAndBlank) {
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kMute, 0), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Led::kSmpte), Light::kOff);
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), 0);
  EXPECT_EQ(xtouch_.GetRing(0), Ring{});
  EXPECT_EQ(xtouch_.GetMeter(0), 0);
  EXPECT_EQ(xtouch_.GetScribble(0, 0), "       ");
  EXPECT_EQ(xtouch_.GetScribble(7, 1), "       ");
  EXPECT_EQ(xtouch_.GetScribbleColor(0), ScribbleColor::kBlack);
  EXPECT_EQ(xtouch_.GetTimecode(), "          ");
}

TEST_F(FakeXTouchTest, LightsShowTheirNotesVelocity) {
  Send({0x90, 0x5E, 0x7F});
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOn);
  Send({0x90, 0x5E, 0x01});
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kBlinking);
  Send({0x90, 0x5E, 0x00});
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOff);

  Send({0x90, 0x1A, 0x7F});
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 2), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 1), Light::kOff);
  Send({0x90, 0x65, 0x7F});
  EXPECT_EQ(xtouch_.GetLight(Button::kRight), Light::kOn);
  Send({0x90, 0x71, 0x7F});
  Send({0x90, 0x73, 0x01});
  EXPECT_EQ(xtouch_.GetLight(Led::kSmpte), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Led::kBeats), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Led::kSolo), Light::kBlinking);
}

TEST_F(FakeXTouchTest, FadersShowTheirPitchBend) {
  Send({0xE2, 0x50, 0x63});
  Send({0xE8, 0x7F, 0x7F});
  EXPECT_EQ(xtouch_.GetFader(2), (0x63 << 7) | 0x50);
  EXPECT_EQ(xtouch_.GetFader(FakeXTouch::kMasterFader), FakeXTouch::kFaderMax);
}

TEST_F(FakeXTouchTest, RingsShowTheirModeAndPosition) {
  Send({0xB0, 0x33, 0x06});
  Send({0xB0, 0x30, 0x4B});
  Send({0xB0, 0x37, 0x7B});
  EXPECT_EQ(xtouch_.GetRing(3), (Ring{.mode = 0, .position = 6}));
  EXPECT_EQ(xtouch_.GetRing(0), (Ring{.mode = 4, .position = 11}));
  EXPECT_EQ(xtouch_.GetRing(7), (Ring{.mode = 7, .position = 11}));
  Send({0xB0, 0x37, 0x00});
  EXPECT_EQ(xtouch_.GetRing(7), (Ring{.mode = 0, .position = 0}));
}

TEST_F(FakeXTouchTest, MetersShowTheirLevel) {
  // With its one data byte, and as JPRSurf sends it, with a third byte.
  Send({0xD0, 0x2E});
  Send({0xD0, 0x75, 0x00});
  EXPECT_EQ(xtouch_.GetMeter(2), 0xE);
  EXPECT_EQ(xtouch_.GetMeter(7), 0x5);
}

TEST_F(FakeXTouchTest, MetersFallOneLevelEachRunUntilSentAgain) {
  const absl::Duration kRun = absl::Seconds(1) / FakeReaper::kRunsPerSecond;
  Send({0xD0, 0x0E, 0x00});
  reaper_.AdvanceTime(kRun / 2);
  EXPECT_EQ(xtouch_.GetMeter(0), 0xE);
  reaper_.AdvanceTime(kRun / 2);
  EXPECT_EQ(xtouch_.GetMeter(0), 0xD);
  reaper_.AdvanceTime(2 * kRun);
  EXPECT_EQ(xtouch_.GetMeter(0), 0xB);

  // Sending a level again, even the same one, holds it.
  Send({0xD0, 0x0E, 0x00});
  EXPECT_EQ(xtouch_.GetMeter(0), 0xE);
  reaper_.AdvanceTime(absl::Seconds(1));
  EXPECT_EQ(xtouch_.GetMeter(0), 0);
}

TEST_F(FakeXTouchTest, TimecodeShowsItsCodesAndDots) {
  // Left to right, the leftmost on CC 0x49.
  const uint8_t codes[] = {0x01, 0x02, 0x43, 0x31, 0x72,
                           0x00, 0x20, 0x2D, 0x3F, 0x30};
  for (int i = 0; i < 10; ++i) {
    Send({0xB0, static_cast<uint8_t>(0x49 - i), codes[i]});
  }
  EXPECT_EQ(xtouch_.GetTimecode(), "ABC.12.@ -?0");
}

TEST_F(FakeXTouchTest, ScribbleTextIsWrittenAtItsOffset) {
  SendSysex(0x12, {7, 'D', 'r', 'u', 'm', 's', ' ', ' '});
  SendSysex(0x12, {56 + 14, 'B', 'a', 's', 's'});
  EXPECT_EQ(xtouch_.GetScribble(1, 0), "Drums  ");
  EXPECT_EQ(xtouch_.GetScribble(2, 1), "Bass   ");
  EXPECT_EQ(xtouch_.GetScribble(1, 1), "       ");

  // Text runs on into the next strip.
  SendSysex(0x12, {56 + 49, 'L', 'e', 'f', 't', 'R', 'i', 'g'});
  EXPECT_EQ(xtouch_.GetScribble(7, 1), "LeftRig");
}

TEST_F(FakeXTouchTest, ScribbleColorsAreSetTogether) {
  SendSysex(0x72, {0, 1, 2, 3, 4, 5, 6, 7});
  EXPECT_EQ(xtouch_.GetScribbleColor(0), ScribbleColor::kBlack);
  EXPECT_EQ(xtouch_.GetScribbleColor(1), ScribbleColor::kRed);
  EXPECT_EQ(xtouch_.GetScribbleColor(2), ScribbleColor::kGreen);
  EXPECT_EQ(xtouch_.GetScribbleColor(3), ScribbleColor::kYellow);
  EXPECT_EQ(xtouch_.GetScribbleColor(4), ScribbleColor::kBlue);
  EXPECT_EQ(xtouch_.GetScribbleColor(5), ScribbleColor::kMagenta);
  EXPECT_EQ(xtouch_.GetScribbleColor(6), ScribbleColor::kCyan);
  EXPECT_EQ(xtouch_.GetScribbleColor(7), ScribbleColor::kWhite);
}

TEST_F(FakeXTouchTest, OutputPortRecordsTheRawMessages) {
  FakeMidiOutput* port = xtouch_.GetOutputPort();
  port->SetRecording(true);
  Send({0x90, 0x5E, 0x7F});
  SendSysex(0x12, {0x00, 'A'});
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOn);
  EXPECT_THAT(port->TakeReceived(),
              ElementsAre(ElementsAre(0x90, 0x5E, 0x7F),
                          ElementsAre(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00,
                                      'A', 0xF7)));
  EXPECT_THAT(port->TakeReceived(), IsEmpty());
}

TEST_F(FakeXTouchTest, MessagesTheHardwareCantTakeFailTheTest) {
  EXPECT_NONFATAL_FAILURE(
      Send({0x90, 0x20, 0x7F}),
      "X-Touch can't take \"90 20 7F\": the note has no light");
  EXPECT_NONFATAL_FAILURE(Send({0x90, 0x70, 0x7F}), "the note has no light");
  EXPECT_NONFATAL_FAILURE(Send({0x90, 0x5E, 0x40}), "a light's velocity");
  EXPECT_NONFATAL_FAILURE(Send({0x80, 0x5E, 0x00}),
                          "the fake doesn't decode it");
  EXPECT_NONFATAL_FAILURE(Send({0x91, 0x5E, 0x7F}),
                          "the fake doesn't decode it");
  EXPECT_NONFATAL_FAILURE(Send({0xB0, 0x30, 0x0C}), "a ring's position");
  EXPECT_NONFATAL_FAILURE(Send({0xB0, 0x3C, 0x01}),
                          "the fake doesn't decode the control");
  EXPECT_NONFATAL_FAILURE(Send({0xB0, 0x4A, 0x30}),
                          "the fake doesn't decode the control");
  EXPECT_NONFATAL_FAILURE(Send({0xD0, 0x0F}), "a meter's level is 0x0-0xE");
  EXPECT_NONFATAL_FAILURE(Send({0xB0, 0x30}), "it is too short");
  EXPECT_NONFATAL_FAILURE(Send({0xE9, 0x00, 0x00}),
                          "there is no fader on the channel");
  EXPECT_NONFATAL_FAILURE(SendSysex(0x12, {110, 'A', 'B', 'C'}),
                          "runs past the end of the display");
  EXPECT_NONFATAL_FAILURE(SendSysex(0x72, {0, 1, 2, 3, 4, 5, 6}),
                          "one for each of the 8 strips");
  EXPECT_NONFATAL_FAILURE(SendSysex(0x72, {8, 1, 2, 3, 4, 5, 6, 7}),
                          "a scribble color is 0-7");
  EXPECT_NONFATAL_FAILURE(SendSysex(0x0A, {1}),
                          "doesn't decode the sysex command");
  EXPECT_NONFATAL_FAILURE(
      Send({0xF0, 0x00, 0x20, 0x32, 0x14, 0x4C, 0x00, 0xF7}),
      "doesn't decode the sysex");
  EXPECT_NONFATAL_FAILURE(
      Send({0xF0, 0x00, 0x00, 0x66, 0x15, 0x12, 0x00, 'A', 0xF7}),
      "the device ID is the other model's");

  // Nothing changed.
  EXPECT_EQ(xtouch_.GetLight(Button::kPlay), Light::kOff);
  EXPECT_EQ(xtouch_.GetRing(0), Ring{});
  EXPECT_EQ(xtouch_.GetMeter(0), 0);
  EXPECT_EQ(xtouch_.GetScribble(0, 0), "       ");
  EXPECT_EQ(xtouch_.GetScribbleColor(0), ScribbleColor::kBlack);
}

TEST_F(FakeXTouchTest, PotButtonsHaveNoLight) {
  EXPECT_NONFATAL_FAILURE(xtouch_.GetLight(StripButton::kPotButton, 0),
                          "pot buttons have no light");
}

//------------------------------------------------------------------------------
// The extender
//------------------------------------------------------------------------------

TEST_F(FakeXTouchExtenderTest, HasTheStrips) {
  xtouch_.Press(StripButton::kMute, 2);
  xtouch_.TouchFader(7);
  EXPECT_THAT(ReadInput(), ElementsAre(ElementsAre(0x90, 0x12, 0x7F),
                                       ElementsAre(0x90, 0x6F, 0x7F)));

  Send({0x90, 0x12, 0x7F});
  Send({0xE7, 0x7F, 0x7F});
  SendSysex(0x12, {0x00, 'E', 'x', 't'});
  SendSysex(0x72, {1, 1, 1, 1, 1, 1, 1, 1});
  EXPECT_EQ(xtouch_.GetLight(StripButton::kMute, 2), Light::kOn);
  EXPECT_EQ(xtouch_.GetFader(7), FakeXTouch::kFaderMax);
  EXPECT_EQ(xtouch_.GetScribble(0, 0), "Ext    ");
  EXPECT_EQ(xtouch_.GetScribbleColor(0), ScribbleColor::kRed);
}

TEST_F(FakeXTouchExtenderTest, HasNoMasterSection) {
  EXPECT_NONFATAL_FAILURE(xtouch_.Press(Button::kPlay),
                          "X-Touch Extender has no buttons outside the strips");
  EXPECT_NONFATAL_FAILURE(xtouch_.TouchFader(FakeXTouch::kMasterFader),
                          "X-Touch Extender has no fader 8");
  EXPECT_NONFATAL_FAILURE(xtouch_.GetLight(Button::kPlay),
                          "has no buttons outside the strips");
  EXPECT_NONFATAL_FAILURE(xtouch_.GetLight(Led::kSmpte),
                          "has no lights outside the strips");
  EXPECT_NONFATAL_FAILURE(xtouch_.GetTimecode(), "has no timecode display");
  EXPECT_THAT(ReadInput(), IsEmpty());

  EXPECT_NONFATAL_FAILURE(Send({0x90, 0x5E, 0x7F}), "the note has no light");
  EXPECT_NONFATAL_FAILURE(Send({0x90, 0x71, 0x7F}), "the note has no light");
  EXPECT_NONFATAL_FAILURE(Send({0xB0, 0x40, 0x30}),
                          "the extender has no timecode display");
  EXPECT_NONFATAL_FAILURE(Send({0xE8, 0x00, 0x00}),
                          "there is no fader on the channel");
  EXPECT_NONFATAL_FAILURE(
      Send({0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 'A', 0xF7}),
      "the device ID is the other model's");
}

}  // namespace
}  // namespace jpr
