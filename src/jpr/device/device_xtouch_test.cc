// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_xtouch.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_output_handle.h"
#include "jpr/device/testing/fake_xtouch.h"

namespace jpr {
namespace {

using ::testing::_;
using ::testing::ElementsAre;

using Button = FakeXTouch::Button;
using Led = FakeXTouch::Led;
using Light = FakeXTouch::Light;
using Ring = FakeXTouch::Ring;
using ScribbleColor = FakeXTouch::ScribbleColor;
using StripButton = FakeXTouch::StripButton;

constexpr int kStripCount = 8;

//==============================================================================
// The X-Touch's controls, and what each is on the hardware.
//==============================================================================

// Each button only the X-Touch has.
struct NamedButton {
  std::string_view name;
  Button button;
};

constexpr NamedButton kButtons[] = {
    {DeviceXTouch::kAssignTrack, Button::kAssignTrack},
    {DeviceXTouch::kAssignSend, Button::kAssignSend},
    {DeviceXTouch::kAssignPan, Button::kAssignPan},
    {DeviceXTouch::kAssignPlugin, Button::kAssignPlugin},
    {DeviceXTouch::kAssignEQ, Button::kAssignEQ},
    {DeviceXTouch::kAssignInst, Button::kAssignInst},
    {DeviceXTouch::kBankLeft, Button::kBankLeft},
    {DeviceXTouch::kBankRight, Button::kBankRight},
    {DeviceXTouch::kChannelLeft, Button::kChannelLeft},
    {DeviceXTouch::kChannelRight, Button::kChannelRight},
    {DeviceXTouch::kFlip, Button::kFlip},
    {DeviceXTouch::kGlobal, Button::kGlobal},
    {DeviceXTouch::kShowNameValue, Button::kShowNameValue},
    {DeviceXTouch::kShowTimeBeats, Button::kShowTimeBeats},
    {DeviceXTouch::kF1, Button::kF1},
    {DeviceXTouch::kF2, Button::kF2},
    {DeviceXTouch::kF3, Button::kF3},
    {DeviceXTouch::kF4, Button::kF4},
    {DeviceXTouch::kF5, Button::kF5},
    {DeviceXTouch::kF6, Button::kF6},
    {DeviceXTouch::kF7, Button::kF7},
    {DeviceXTouch::kF8, Button::kF8},
    {DeviceXTouch::kViewMIDI, Button::kViewMIDI},
    {DeviceXTouch::kViewInputs, Button::kViewInputs},
    {DeviceXTouch::kViewAudio, Button::kViewAudio},
    {DeviceXTouch::kViewInst, Button::kViewInst},
    {DeviceXTouch::kViewAux, Button::kViewAux},
    {DeviceXTouch::kViewBuses, Button::kViewBuses},
    {DeviceXTouch::kViewOutputs, Button::kViewOutputs},
    {DeviceXTouch::kViewUser, Button::kViewUser},
    {DeviceXTouch::kShift, Button::kShift},
    {DeviceXTouch::kOption, Button::kOption},
    {DeviceXTouch::kControl, Button::kControl},
    {DeviceXTouch::kAlt, Button::kAlt},
    {DeviceXTouch::kAutoRead, Button::kAutoRead},
    {DeviceXTouch::kAutoWrite, Button::kAutoWrite},
    {DeviceXTouch::kAutoTrim, Button::kAutoTrim},
    {DeviceXTouch::kAutoTouch, Button::kAutoTouch},
    {DeviceXTouch::kAutoLatch, Button::kAutoLatch},
    {DeviceXTouch::kAutoGroup, Button::kAutoGroup},
    {DeviceXTouch::kSave, Button::kSave},
    {DeviceXTouch::kUndo, Button::kUndo},
    {DeviceXTouch::kCancel, Button::kCancel},
    {DeviceXTouch::kEnter, Button::kEnter},
    {DeviceXTouch::kMarker, Button::kMarker},
    {DeviceXTouch::kNudge, Button::kNudge},
    {DeviceXTouch::kCycle, Button::kCycle},
    {DeviceXTouch::kDrop, Button::kDrop},
    {DeviceXTouch::kReplace, Button::kReplace},
    {DeviceXTouch::kClick, Button::kClick},
    {DeviceXTouch::kSolo, Button::kSolo},
    {DeviceXTouch::kRewind, Button::kRewind},
    {DeviceXTouch::kForward, Button::kForward},
    {DeviceXTouch::kStop, Button::kStop},
    {DeviceXTouch::kPlay, Button::kPlay},
    {DeviceXTouch::kRecord, Button::kRecord},
    {DeviceXTouch::kUp, Button::kUp},
    {DeviceXTouch::kDown, Button::kDown},
    {DeviceXTouch::kScrub, Button::kScrub},
    {DeviceXTouch::kZoom, Button::kZoom},
    {DeviceXTouch::kLeft, Button::kLeft},
    {DeviceXTouch::kRight, Button::kRight},
};

// Each button on a strip, and its control's name on the strip.
struct NamedStripButton {
  std::string (*name)(int strip);
  StripButton button;
};

constexpr NamedStripButton kStripButtons[] = {
    {&DeviceXTouch::Rec, StripButton::kRec},
    {&DeviceXTouch::Solo, StripButton::kSolo},
    {&DeviceXTouch::Mute, StripButton::kMute},
    {&DeviceXTouch::Select, StripButton::kSelect},
    {&DeviceXTouch::PotButton, StripButton::kPotButton},
};

// Each light with no button, which only the X-Touch has.
struct NamedLed {
  std::string_view name;
  Led led;
};

constexpr NamedLed kLeds[] = {
    {DeviceXTouch::kSmpteLed, Led::kSmpte},
    {DeviceXTouch::kBeatsLed, Led::kBeats},
    {DeviceXTouch::kSoloLed, Led::kSolo},
};

// The controls on each strip: the four buttons, the pot and its button, the
// fader, the meter, and the scribble strip's two lines and color.
constexpr int kStripControlCount = 11;

// A meter's peak, and the level it shows.
struct MeterLevel {
  double peak;
  int level;
};

constexpr MeterLevel kMeterLevels[] = {
    {0.0, 0x0},      //
    {0.003, 0x0},    // Below -50 dB.
    {0.00316, 0x2},  // -50 dB
    {0.01, 0x4},     // -40 dB
    {0.0316, 0x6},   // -30 dB
    {0.1, 0x8},      // -20 dB
    {0.2, 0xA},      // -14 dB
    {0.398, 0xB},    // -8 dB
    {0.631, 0xD},    // -4 dB
    {1.0, 0xE},      // Clipping
};

// A color, and the scribble strip's palette color it shows as.
struct PaletteColor {
  Color color;
  ScribbleColor shown;
};

//==============================================================================
// Test fixtures
//==============================================================================

class DeviceXTouchTest : public ::testing::Test {
 protected:
  explicit DeviceXTouchTest(DeviceXTouch::Type type = DeviceXTouch::Type::kFull)
      : type_(type),
        xtouch_(reaper_,
                type == DeviceXTouch::Type::kFull ? FakeXTouch::Type::kFull
                                                  : FakeXTouch::Type::kExtender,
                "X-Touch"),
        device_(type, runner_, ports_.OpenInput("X-Touch"),
                ports_.OpenOutput("X-Touch")) {}

  // Advances the fake's clock by one run, then runs the device, then MIDI input
  // and output, as PluginSurface::OnRun() does (which runs the scene after
  // input).
  void Run() {
    reaper_.AdvanceTime(FakeReaper::GetRunTime());
    const RunTime time = RunTime::Now();
    runner_.Run(time);
    ports_.RunInput(time);
    ports_.RunOutput(time);
  }

  // Registers an input of `type` on `control`, to read it.
  ControlInputHandle Read(Control* control, ControlInput::Type type) {
    return control->RegisterInput({.input_type = type}, &input_changed_);
  }

  // Returns how many faders this model has: the strips', and the master
  // fader on the X-Touch.
  int GetFaderCount() const {
    return type_ == DeviceXTouch::Type::kFull ? kStripCount + 1 : kStripCount;
  }

  // Returns the name of the fader's control.
  static std::string GetFaderName(int fader) {
    return fader == FakeXTouch::kMasterFader
               ? std::string(DeviceXTouch::kMasterFader)
               : DeviceXTouch::Fader(fader);
  }

  // Presses the control with `press`, then releases it with `release`, and
  // returns whether it is pressed after each.
  std::vector<bool> PressAndRelease(Control* control,
                                    absl::FunctionRef<void()> press,
                                    absl::FunctionRef<void()> release) {
    ControlInputHandle input = Read(control, ControlInput::Type::kPress);
    std::vector<bool> pressed;
    press();
    Run();
    pressed.push_back(control->IsPressed(input.GetId()));
    release();
    Run();
    pressed.push_back(control->IsPressed(input.GetId()));
    return pressed;
  }

  // Turns the control's light on, then blinking, then off, and returns what
  // `light` shows after each.
  std::vector<Light> CycleLight(Control* control,
                                absl::FunctionRef<Light()> light) {
    std::vector<Light> lights;
    control->SetDValue(1);
    Run();
    lights.push_back(light());
    control->SetDValue(1, /*mode=*/1);
    Run();
    lights.push_back(light());
    control->SetDValue(0);
    Run();
    lights.push_back(light());
    return lights;
  }

  const DeviceXTouch::Type type_;
  FakeReaper reaper_;
  FakeXTouch xtouch_;
  MidiPorts ports_;
  Runner runner_{"Device"};
  DeviceXTouch device_;
  bool input_changed_ = false;  // Every input sets it, and no test reads it.
};

class DeviceXTouchExtenderTest : public DeviceXTouchTest {
 protected:
  DeviceXTouchExtenderTest()
      : DeviceXTouchTest(DeviceXTouch::Type::kExtender) {}
};

// For what both models have, run on each.
class DeviceXTouchModelTest
    : public DeviceXTouchTest,
      public ::testing::WithParamInterface<DeviceXTouch::Type> {
 protected:
  DeviceXTouchModelTest() : DeviceXTouchTest(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(
    Models, DeviceXTouchModelTest,
    ::testing::Values(DeviceXTouch::Type::kFull, DeviceXTouch::Type::kExtender),
    [](const ::testing::TestParamInfo<DeviceXTouch::Type>& info) {
      return std::string(info.param == DeviceXTouch::Type::kFull ? "XTouch"
                                                                 : "Extender");
    });

//==============================================================================
// Controls
//==============================================================================

TEST_F(DeviceXTouchTest, HasEveryControl) {
  EXPECT_EQ(std::ssize(device_.GetControls()),
            kStripCount * kStripControlCount + std::ssize(kButtons) +
                std::ssize(kLeds) + 2);  // The master fader and timecode.
}

TEST_F(DeviceXTouchExtenderTest, HasOnlyStrips) {
  EXPECT_EQ(std::ssize(device_.GetControls()),
            kStripCount * kStripControlCount);
  for (const NamedButton& entry : kButtons) {
    EXPECT_EQ(device_.GetControl(entry.name), nullptr) << entry.name;
  }
  for (const NamedLed& entry : kLeds) {
    EXPECT_EQ(device_.GetControl(entry.name), nullptr) << entry.name;
  }
  EXPECT_EQ(device_.GetControl(DeviceXTouch::kMasterFader), nullptr);
  EXPECT_EQ(device_.GetControl(DeviceXTouch::kTimecode), nullptr);
}

//==============================================================================
// Buttons and lights
//==============================================================================

TEST_F(DeviceXTouchTest, ButtonsArePressedAndReleased) {
  for (const NamedButton& entry : kButtons) {
    SCOPED_TRACE(entry.name);
    Control* control = device_.GetControl(entry.name);
    ASSERT_NE(control, nullptr);
    EXPECT_THAT(PressAndRelease(
                    control, [&] { xtouch_.Press(entry.button); },
                    [&] { xtouch_.Release(entry.button); }),
                ElementsAre(true, false));
  }
}

TEST_F(DeviceXTouchTest, ButtonLightsAreOnOrBlinking) {
  for (const NamedButton& entry : kButtons) {
    SCOPED_TRACE(entry.name);
    Control* control = device_.GetControl(entry.name);
    ASSERT_NE(control, nullptr);
    EXPECT_THAT(
        CycleLight(control, [&] { return xtouch_.GetLight(entry.button); }),
        ElementsAre(Light::kOn, Light::kBlinking, Light::kOff));
  }
}

TEST_P(DeviceXTouchModelTest, StripButtonsArePressedAndReleased) {
  for (const NamedStripButton& entry : kStripButtons) {
    for (int strip = 0; strip < kStripCount; ++strip) {
      const std::string name = entry.name(strip);
      SCOPED_TRACE(name);
      Control* control = device_.GetControl(name);
      ASSERT_NE(control, nullptr);
      EXPECT_THAT(PressAndRelease(
                      control, [&] { xtouch_.Press(entry.button, strip); },
                      [&] { xtouch_.Release(entry.button, strip); }),
                  ElementsAre(true, false));
    }
  }
}

TEST_P(DeviceXTouchModelTest, StripButtonLightsAreOnOrBlinking) {
  for (const NamedStripButton& entry : kStripButtons) {
    for (int strip = 0; strip < kStripCount; ++strip) {
      const std::string name = entry.name(strip);
      SCOPED_TRACE(name);
      Control* control = device_.GetControl(name);
      ASSERT_NE(control, nullptr);
      if (entry.button == StripButton::kPotButton) {
        // The pot's button has no light.
        EXPECT_FALSE(control->GetOutputs().IsSet(ControlOutput::Type::kDValue));
        continue;
      }
      EXPECT_THAT(
          CycleLight(control,
                     [&] { return xtouch_.GetLight(entry.button, strip); }),
          ElementsAre(Light::kOn, Light::kBlinking, Light::kOff));
    }
  }
}

TEST_F(DeviceXTouchTest, LedsAreOnOrBlinking) {
  for (const NamedLed& entry : kLeds) {
    SCOPED_TRACE(entry.name);
    Control* control = device_.GetControl(entry.name);
    ASSERT_NE(control, nullptr);
    EXPECT_THAT(
        CycleLight(control, [&] { return xtouch_.GetLight(entry.led); }),
        ElementsAre(Light::kOn, Light::kBlinking, Light::kOff));
  }
}

//==============================================================================
// Faders
//==============================================================================

TEST_P(DeviceXTouchModelTest, EachFaderIsMoved) {
  std::vector<Control*> controls;
  std::vector<ControlInputHandle> values;
  for (int fader = 0; fader < GetFaderCount(); ++fader) {
    controls.push_back(device_.GetControl(GetFaderName(fader)));
    ASSERT_NE(controls.back(), nullptr) << GetFaderName(fader);
    values.push_back(Read(controls.back(), ControlInput::Type::kValue));
  }
  for (int fader = 0; fader < GetFaderCount(); ++fader) {
    SCOPED_TRACE(GetFaderName(fader));
    xtouch_.MoveFader(fader, FakeXTouch::kFaderMax);
    Run();
    for (int other = 0; other < GetFaderCount(); ++other) {
      EXPECT_EQ(controls[other]->GetValue(values[other].GetId()),
                other <= fader ? 1.0 : 0.0)
          << GetFaderName(other);
    }
  }
}

TEST_P(DeviceXTouchModelTest, EachFaderIsSet) {
  for (int fader = 0; fader < GetFaderCount(); ++fader) {
    SCOPED_TRACE(GetFaderName(fader));
    Control* control = device_.GetControl(GetFaderName(fader));
    ASSERT_NE(control, nullptr);
    control->SetCValue(1.0);
    Run();
    for (int other = 0; other < GetFaderCount(); ++other) {
      EXPECT_EQ(xtouch_.GetFader(other),
                other <= fader ? FakeXTouch::kFaderMax : 0)
          << GetFaderName(other);
    }
  }
}

TEST_P(DeviceXTouchModelTest, FaderTouchesArePresses) {
  for (int fader = 0; fader < GetFaderCount(); ++fader) {
    SCOPED_TRACE(GetFaderName(fader));
    Control* control = device_.GetControl(GetFaderName(fader));
    ASSERT_NE(control, nullptr);
    if (type_ == DeviceXTouch::Type::kExtender && fader == 2) {
      // See ExtendersThirdFaderHasNoTouch.
      continue;
    }
    EXPECT_THAT(PressAndRelease(
                    control, [&] { xtouch_.TouchFader(fader); },
                    [&] { xtouch_.ReleaseFader(fader); }),
                ElementsAre(true, false));
  }
}

TEST_F(DeviceXTouchTest, FadersAreHeldWhileTouched) {
  for (int fader : {0, FakeXTouch::kMasterFader}) {
    SCOPED_TRACE(GetFaderName(fader));
    Control* control = device_.GetControl(GetFaderName(fader));
    ASSERT_NE(control, nullptr);
    xtouch_.TouchFader(fader);
    Run();
    control->SetCValue(1.0);
    Run();
    EXPECT_EQ(xtouch_.GetFader(fader), 0);

    // The release is read after the device runs, so the fader is set on the
    // run after it.
    xtouch_.ReleaseFader(fader);
    Run();
    Run();
    EXPECT_EQ(xtouch_.GetFader(fader), FakeXTouch::kFaderMax);
  }
}

// The user's extender has lost its third fader's touch, so it has none, and
// isn't set until the hand has stopped moving it.
TEST_F(DeviceXTouchExtenderTest, ExtendersThirdFaderHasNoTouch) {
  Control* control = device_.GetControl(DeviceXTouch::kFader3);
  ASSERT_NE(control, nullptr);
  EXPECT_FALSE(control->GetInputs().IsSet(ControlInput::Type::kPress));

  // The control only sees the fader move while something reads it.
  ControlInputHandle value = Read(control, ControlInput::Type::kValue);
  xtouch_.MoveFader(2, 1000);
  Run();
  control->SetCValue(1.0);
  Run();
  EXPECT_EQ(xtouch_.GetFader(2), 1000);
  reaper_.AdvanceTime(absl::Seconds(1));
  Run();
  EXPECT_EQ(xtouch_.GetFader(2), FakeXTouch::kFaderMax);
}

//==============================================================================
// Pots
//==============================================================================

TEST_P(DeviceXTouchModelTest, PotTurnsAreDeltas) {
  std::vector<Control*> controls;
  std::vector<ControlInputHandle> deltas;
  for (int strip = 0; strip < kStripCount; ++strip) {
    controls.push_back(device_.GetControl(DeviceXTouch::Pot(strip)));
    ASSERT_NE(controls.back(), nullptr) << DeviceXTouch::Pot(strip);
    deltas.push_back(Read(controls.back(), ControlInput::Type::kDelta));
  }

  // Each strip turns a different number of clicks, clockwise and then
  // counterclockwise. A full turn of 63 clicks is 1.0.
  for (int first_clicks : {1, -63}) {
    SCOPED_TRACE(absl::StrCat("from ", first_clicks, " clicks"));
    for (int strip = 0; strip < kStripCount; ++strip) {
      xtouch_.TurnPot(strip, first_clicks + strip);
    }
    Run();
    for (int strip = 0; strip < kStripCount; ++strip) {
      EXPECT_DOUBLE_EQ(controls[strip]->GetDelta(deltas[strip].GetId()),
                       (first_clicks + strip) / 63.0)
          << DeviceXTouch::Pot(strip);
    }
  }
}

TEST_P(DeviceXTouchModelTest, PotRingsShowEachMode) {
  for (int strip = 0; strip < kStripCount; ++strip) {
    SCOPED_TRACE(DeviceXTouch::Pot(strip));
    Control* control = device_.GetControl(DeviceXTouch::Pot(strip));
    ASSERT_NE(control, nullptr);

    // Modes 0-7 are the ring's own. Spread (3 and 7) has 6 positions, and the
    // rest have 11.
    for (int mode = 0; mode < 8; ++mode) {
      SCOPED_TRACE(absl::StrCat("mode ", mode));
      const int max_value = control->GetDValueMaxValue(mode);
      EXPECT_EQ(max_value, mode % 4 == 3 ? 5 : 10);
      control->SetDValue(0, mode);
      Run();
      EXPECT_EQ(xtouch_.GetRing(strip), (Ring{.mode = mode, .position = 1}));
      control->SetDValue(max_value, mode);
      Run();
      EXPECT_EQ(xtouch_.GetRing(strip),
                (Ring{.mode = mode, .position = max_value + 1}));
    }

    // Mode 8 turns the ring off.
    control->SetDValue(0, /*mode=*/8);
    Run();
    EXPECT_EQ(xtouch_.GetRing(strip), Ring{});
  }
}

TEST_F(DeviceXTouchTest, PotRingsAreClearedOff) {
  Control* control = device_.GetControl(DeviceXTouch::kPot1);
  ASSERT_NE(control, nullptr);
  ControlOutputHandle writer = control->RegisterOutputWriter();
  control->SetDValue(5, /*mode=*/1);
  Run();
  EXPECT_EQ(xtouch_.GetRing(0), (Ring{.mode = 1, .position = 6}));

  writer = ControlOutputHandle();
  Run();
  EXPECT_EQ(xtouch_.GetRing(0), Ring{});
}

//==============================================================================
// Meters
//==============================================================================

TEST_F(DeviceXTouchTest, MetersShowEachLevel) {
  Control* control = device_.GetControl(DeviceXTouch::kMeter1);
  ASSERT_NE(control, nullptr);
  for (const MeterLevel& entry : kMeterLevels) {
    SCOPED_TRACE(entry.peak);
    control->SetCValue(entry.peak);
    Run();
    EXPECT_EQ(xtouch_.GetMeter(0), entry.level);
  }
}

TEST_P(DeviceXTouchModelTest, EachStripHasAMeter) {
  for (int strip = 0; strip < kStripCount; ++strip) {
    Control* control = device_.GetControl(DeviceXTouch::Meter(strip));
    ASSERT_NE(control, nullptr) << DeviceXTouch::Meter(strip);
    control->SetCValue(kMeterLevels[strip + 2].peak);
  }
  Run();
  for (int strip = 0; strip < kStripCount; ++strip) {
    EXPECT_EQ(xtouch_.GetMeter(strip), kMeterLevels[strip + 2].level)
        << DeviceXTouch::Meter(strip);
  }
}

// The hardware's meters fall on their own, so a meter is sent each run, even
// when it hasn't changed.
TEST_F(DeviceXTouchTest, MetersHoldWhileTheyAreSentEachRun) {
  Control* control = device_.GetControl(DeviceXTouch::kMeter1);
  ASSERT_NE(control, nullptr);
  for (int run = 0; run < 10; ++run) {
    control->SetCValue(0.5);
    Run();
    EXPECT_EQ(xtouch_.GetMeter(0), 0xB);
  }
  Run();
  EXPECT_EQ(xtouch_.GetMeter(0), 0xA);
}

//==============================================================================
// Scribble strips
//==============================================================================

TEST_P(DeviceXTouchModelTest, ScribbleStripsShowBothLines) {
  for (int strip = 0; strip < kStripCount; ++strip) {
    for (int line = 0; line < 2; ++line) {
      Control* control =
          device_.GetControl(DeviceXTouch::Scribble(strip, line));
      ASSERT_NE(control, nullptr) << DeviceXTouch::Scribble(strip, line);
      control->SetText(absl::StrCat("S", strip, " L", line));
    }
  }
  Run();
  for (int strip = 0; strip < kStripCount; ++strip) {
    for (int line = 0; line < 2; ++line) {
      EXPECT_EQ(xtouch_.GetScribble(strip, line),
                absl::StrCat("S", strip, " L", line, "  "))
          << DeviceXTouch::Scribble(strip, line);
    }
  }
}

TEST_F(DeviceXTouchTest, ScribbleTextIsCutToFit) {
  Control* control = device_.GetControl(DeviceXTouch::kScribble1Line1);
  ASSERT_NE(control, nullptr);
  control->SetText("Vocal Bus");
  Run();
  EXPECT_EQ(xtouch_.GetScribble(0, 0), "Vocal B");
  EXPECT_EQ(xtouch_.GetScribble(1, 0), "       ");

  control->SetText("");
  Run();
  EXPECT_EQ(xtouch_.GetScribble(0, 0), "       ");
}

TEST_P(DeviceXTouchModelTest, EachStripHasAScribbleColor) {
  // Each strip a palette color, all sent in one message.
  constexpr PaletteColor kStripColors[kStripCount] = {
      {{255, 0, 0}, ScribbleColor::kRed},
      {{0, 255, 0}, ScribbleColor::kGreen},
      {{255, 255, 0}, ScribbleColor::kYellow},
      {{0, 0, 255}, ScribbleColor::kBlue},
      {{255, 0, 255}, ScribbleColor::kMagenta},
      {{0, 255, 255}, ScribbleColor::kCyan},
      {{255, 255, 255}, ScribbleColor::kWhite},
      {{0, 0, 0}, ScribbleColor::kBlack},
  };
  for (int strip = 0; strip < kStripCount; ++strip) {
    Control* control = device_.GetControl(DeviceXTouch::ScribbleColor(strip));
    ASSERT_NE(control, nullptr) << DeviceXTouch::ScribbleColor(strip);
    control->SetColor(kStripColors[strip].color);
  }
  Run();
  for (int strip = 0; strip < kStripCount; ++strip) {
    EXPECT_EQ(xtouch_.GetScribbleColor(strip), kStripColors[strip].shown)
        << DeviceXTouch::ScribbleColor(strip);
  }
}

// Colors are brightened, then each of red, green, and blue is on or off.
TEST_F(DeviceXTouchTest, ScribbleColorsAreTheNearestInThePalette) {
  // Each is shown differently from the one before it, so each is sent.
  constexpr PaletteColor kPaletteColors[] = {
      {{64, 0, 0}, ScribbleColor::kRed},         // Dark red.
      {{128, 128, 128}, ScribbleColor::kWhite},  // Gray.
      {{255, 128, 128}, ScribbleColor::kRed},    // Pale red.
      {{255, 160, 0}, ScribbleColor::kYellow},   // Amber.
      {{255, 100, 0}, ScribbleColor::kRed},      // Orange.
      {{40, 40, 200}, ScribbleColor::kBlue},     // Muted blue.
      {{0, 0, 0}, ScribbleColor::kBlack},
  };
  Control* control = device_.GetControl(DeviceXTouch::kScribble1Color);
  ASSERT_NE(control, nullptr);
  for (const PaletteColor& entry : kPaletteColors) {
    SCOPED_TRACE(FormatColor(entry.color));
    control->SetColor(entry.color);
    Run();
    EXPECT_EQ(xtouch_.GetScribbleColor(0), entry.shown);
  }
}

//==============================================================================
// Timecode
//==============================================================================

// The fake's project is at 120 BPM in 4/4, with 30 frames and 44100 samples a
// second.
class DeviceXTouchTimecodeTest : public DeviceXTouchTest {
 protected:
  // A timeline position, and what the display shows for it.
  struct Timecode {
    int mode;  // See ControlTextOutput::SetTimelineText().
    double position;
    std::string_view shown;
  };

  // Shows each of `timecodes` in turn, and checks what the display shows.
  void ExpectTimecodes(absl::Span<const Timecode> timecodes) {
    Control* control = device_.GetControl(DeviceXTouch::kTimecode);
    ASSERT_NE(control, nullptr);
    for (const Timecode& entry : timecodes) {
      SCOPED_TRACE(absl::StrCat("mode ", entry.mode, " at ", entry.position));
      control->SetTimelineText(TimelinePosition(entry.position), entry.mode);
      Run();
      EXPECT_EQ(xtouch_.GetTimecode(), entry.shown);
    }
  }
};

TEST_F(DeviceXTouchTimecodeTest, ShowsEachTimelineMode) {
  constexpr Timecode kTimecodes[] = {
      {1, 3.5, "  2 4.00   "},       // Beats: measure 2, beat 4.
      {2, 3.5, "    0.03.500"},      // Time.
      {2, 3725.5, "  1.02.05.500"},  // Time, with hours.
      {3, 3725.5, " 01.02.05.15 "},  // Frames.
      {4, 3.5, "    154350"},        // Samples.
  };
  ExpectTimecodes(kTimecodes);
}

// Before the start, beats and frames count measures and hours down from 0, and
// time is the distance from the start.
TEST_F(DeviceXTouchTimecodeTest, ShowsPositionsBeforeTheStart) {
  constexpr Timecode kTimecodes[] = {
      {1, -0.25, "  0 4.50   "},      // Beats: "0.4.50".
      {1, -65.5, "-32 2.00   "},      // Beats: "-32.2.00".
      {2, -65.5, "   -1.05.500"},     // Time: "-1:05.500".
      {2, -3725.5, " -1.02.05.500"},  // Time: "-1:02:05.500".
      {3, -0.05, "-01.59.59.28 "},    // Frames: "-1:59:59:28".
      {3, -7300.0, "-03.58.20.00 "},  // Frames: "-3:58:20:00".
  };
  ExpectTimecodes(kTimecodes);
}

// Measures and hours have 3 digits, one of them the sign before the start, and
// show "---" when they don't fit.
TEST_F(DeviceXTouchTimecodeTest, ShowsDashesForMeasuresAndHoursThatDontFit) {
  constexpr Timecode kTimecodes[] = {
      {1, 1997.5, "999 4.00   "},       // Beats: "999.4.00".
      {1, 1998.0, "--- 1.00   "},       // Beats: "1000.1.00".
      {1, -199.5, "-99 2.00   "},       // Beats: "-99.2.00".
      {1, -201.5, "--- 2.00   "},       // Beats: "-100.2.00".
      {2, 3599999.5, "999.59.59.500"},  // Time: "999:59:59.500".
      {2, 3600000.5, "---.00.00.500"},  // Time: "1000:00:00.500".
      {2, -359999.5, "-99.59.59.500"},  // Time: "-99:59:59.500".
      {2, -360000.5, "---.00.00.500"},  // Time: "-100:00:00.500".
      {3, 3599999.5, "999.59.59.15 "},  // Frames: "999:59:59:15".
      {3, 3600000.5, "---.00.00.15 "},  // Frames: "1000:00:00:15".
      {3, -352800.5, "-99.59.59.15 "},  // Frames: "-99:59:59:15".
      {3, -356400.5, "---.59.59.15 "},  // Frames: "-100:59:59:15".
  };
  ExpectTimecodes(kTimecodes);
}

TEST_F(DeviceXTouchTimecodeTest, ShowsText) {
  Control* control = device_.GetControl(DeviceXTouch::kTimecode);
  ASSERT_NE(control, nullptr);

  // Text is right aligned, and a '.' lights the dot of the character before
  // it.
  control->SetText("AB.C");
  Run();
  EXPECT_EQ(xtouch_.GetTimecode(), "       AB.C");

  // The display has '@' to '_', and ' ' to '?'. Anything else is a space.
  control->SetText("@_ ?a");
  Run();
  EXPECT_EQ(xtouch_.GetTimecode(), "     @_ ? ");

  // Text that doesn't fit keeps its right end.
  control->SetText("123456789012");
  Run();
  EXPECT_EQ(xtouch_.GetTimecode(), "3456789012");
}

//==============================================================================
// Raw messages
//==============================================================================

TEST_P(DeviceXTouchModelTest, SendsTheProtocolsMessages) {
  const uint8_t id = (type_ == DeviceXTouch::Type::kFull ? 0x14 : 0x15);
  FakeMidiOutput* output = xtouch_.GetOutputPort();
  output->SetRecording(true);

  device_.GetControl(DeviceXTouch::kMute2)->SetDValue(1);
  Run();
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0x90, 0x11, 0x7F)));

  device_.GetControl(DeviceXTouch::kPot2)->SetDValue(5, /*mode=*/1);
  Run();
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0xB0, 0x31, 0x16)));

  device_.GetControl(DeviceXTouch::kFader2)->SetCValue(1.0);
  Run();
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0xE1, 0x7F, 0x7F)));

  // Channel pressure has one data byte. The port records the three it is
  // passed.
  device_.GetControl(DeviceXTouch::kMeter2)->SetCValue(1.0);
  Run();
  EXPECT_THAT(output->TakeReceived(), ElementsAre(ElementsAre(0xD0, 0x1E, _)));

  device_.GetControl(DeviceXTouch::kScribble2Line2)->SetText("Bass");
  Run();
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0xF0, 0x00, 0x00, 0x66, id, 0x12, 63, 'B',
                                      'a', 's', 's', ' ', ' ', ' ', 0xF7)));

  device_.GetControl(DeviceXTouch::kScribble2Color)->SetColor({255, 0, 0});
  Run();
  EXPECT_THAT(output->TakeReceived(),
              ElementsAre(ElementsAre(0xF0, 0x00, 0x00, 0x66, id, 0x72, 0, 1, 0,
                                      0, 0, 0, 0, 0, 0xF7)));
}

}  // namespace
}  // namespace jpr
